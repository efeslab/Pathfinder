#include "posix_graph.hpp"

using namespace std;
using namespace llvm;
namespace icl = boost::icl;
namespace fs = boost::filesystem;


namespace pathfinder
{

/* posix node */

bool posix_node::is_equivalent(string function, const posix_node& other) const {
    // we search for stack frame, and if there is a match with file and line number under the given function, we consider them equivalent
    bool found_in_self = false;
    stack_frame sf_self;
    for (const auto &sf : event_->backtrace()) {
        if (sf.function == function) {
            found_in_self = true;
            sf_self = sf;
        }
    }
    if (!found_in_self) {
        // cerr << "Error: function " << function << " not found in the backtrace of the event" << endl;
        // exit(EXIT_FAILURE);

        // this may be the margin event we add at the end of update mechanism, as a best effort, we compare if the stack frame are all the same
        if (event_->backtrace().size() != other.event_->backtrace().size()) {
            return false;
        }
        for (int i = 0; i < event_->backtrace().size(); i++) {
            if (event_->backtrace()[i] != other.event_->backtrace()[i]) {
                return false;
            }
        }
        return true;
    }
    for (const auto &sf : other.event_->backtrace()) {
        if (sf.function == function) {
            if (sf_self == sf) {
                return true;
            }
            else {
                return false;
            }
        }
    }
    return false;
}

/* posix graph */

vector<persistence_node*> posix_graph::create_nodes(void) const {
    vector<persistence_node*> nodes;
    for (const auto te : trace_.events()) {
        // TODO: deal with msync, sync
        // if (te->is_fsync() || te->is_fdatasync()) continue;
        if (te->is_marker_event()) continue;
        nodes.push_back(new posix_node(te));
    }

    return nodes;


}

posix_graph::posix_graph(const class trace &t, const fs::path &output_dir, bool decompose_syscall=true)
    : persistence_graph(t, output_dir), decompose_syscall_(decompose_syscall) {
    graph_ = construct_graph();
}

bool posix_graph::is_dependent(const persistence_node *a, const persistence_node *b) {
    const posix_node *pa = dynamic_cast<const posix_node*>(a);
    const posix_node *pb = dynamic_cast<const posix_node*>(b);
    assert(pa && pb);
    shared_ptr<trace_event> ea = pa->event();
    shared_ptr<trace_event> eb = pb->event();
    if ((ea->event_idx() == 54552) && (eb->event_idx() == 54553)) {
        cout << "STOP" << endl;

    }

    // A: sync family events
    // A1: fsync
    // if a is a fsync event, b should observe the effect of a
    if (ea->is_fsync()) return true;
    if (eb->is_fsync()) {
        // any updates on the same file/dir should be persisted, we use both file path and fd to identify the file as fd can be reused
        // we assume per file fsync effect
        if (ea->file_path != "" && ea->file_path == eb->file_path && ea->fd == eb->fd) {
            return true;
        }
        // rename and a sync on dir
        if (ea->is_rename() && get_dir_name(ea->old_path) == eb->file_path) {
            return true;
        }
    }
    // A2: fdatasync
    if (ea->is_fdatasync()) return true;
    if (eb->is_fdatasync()) {
        // any updates on the file will be persisted
        // TODO: does fdatasync affect ftruncate and fallocate -> I dont think so
        // we consider per file fdatasync effect
        if ((ea->is_write_family() || eb->is_write_family()) && ea->file_path != "" && ea->file_path == eb->file_path && ea->fd == eb->fd) {
            return true;
        }
    }
    // A3: sync / syncfs, for syncfs we assume same semantic as sync since we assume data dir is in the same file system
    if (ea->is_sync() || eb->is_sync() || ea->is_syncfs() || eb->is_syncfs()) return true;
    // A4: sync_file_range
    // according to https://man7.org/linux/man-pages/man2/sync_file_range.2.html, usage with 'SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER' flags will ensure the data is persisted before the call returns, we now only consider this case
    // TODO: consider other flags?

    // if sync_file_range flags is not 0, then it is not a no-op
    // TODO: what happen if ea->is_sync_file_range()? is it still per file?
    if (ea->is_sync_file_range() && ea->flags) return true;
    if (eb->is_sync_file_range()) {
        if (eb->flags & (SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER)) {
            // any updates on the file with same blocks will be persisted
            if ((ea->is_write()) && ea->file_path != "" && ea->file_path == eb->file_path && ea->fd == eb->fd) {
                assert(ea->block_ids && eb->block_ids);
                pair<int, int> block_ids_a = *ea->block_ids;
                pair<int, int> block_ids_b = *eb->block_ids;
                return is_block_ids_overlapping(block_ids_a, block_ids_b);
            }
        }
    }

    // B: write family events
    // B1: for normal write, if the blocks are overlapping, then they are dependent
    // Yile: B1 is now moved to is_dependent_decomposed

    // if (ea->is_write() && eb->is_write() && ea->file_path == eb->file_path && ea->fd == eb->fd) {
    //     assert(ea->block_ids && eb->block_ids);
    //     pair<int, int> block_ids_a = *ea->block_ids;
    //     pair<int, int> block_ids_b = *eb->block_ids;
    //     return is_block_ids_overlapping(block_ids_a, block_ids_b);
    // }

    // C: metadata update events
    // C1: fallocate, ftruncate and unlink happens before a sync on dir
    if (ea->is_fallocate() || ea->is_ftruncate() || ea->is_unlink()) {
        if (eb->is_fsync() && eb->file_path == get_dir_name(ea->file_path)) return true;
    }
    // C2: for rename, fsync on both old and new dir should have happen-before with the rename
    if (ea->is_rename()) {
        if (eb->is_fsync() && (eb->file_path == get_dir_name(ea->file_path) || eb->file_path == get_dir_name(ea->new_path))) return true;
    }


    // D: open/close correctness
    // D1: open/creat should precede any subsequent call on this fd
    if (ea->is_open() || ea->is_creat()) {
        if (eb->fd == ea->fd && eb->event_idx() > ea->event_idx()) return true;
    }
    if ((ea->is_open() && ea->flags & O_CREAT) || ea->is_creat()) {
        // if the file is created, then any subsequent events preceding the open/creat should be persisted
        if (eb->file_path != "" && ea->file_path == eb->file_path) return true;
    }
    // D2: close should follow any previous call on this fd
    if (eb->is_close()) {
        if (eb->fd == ea->fd && eb->event_idx() > ea->event_idx()) return true;
    }
    // D3: to deal with open then close then open and possibly same fd, we add a close to open dependency for any same fd where timestamp of close is earlier than the timestamp of the second open
    if (ea->is_close() && eb->is_open() && ea->fd == eb->fd && ea->file_path == eb->file_path && ea->event_idx() < eb->event_idx()) return true;

    // Ad-hoc D4: rename to a new file name should happen before the open to this file(
    if (ea->is_rename() && eb->is_open() && ea->new_path == eb->file_path && ea->event_idx() < eb->event_idx()) return true; 
    // E: check dependencies arised from decomposed syscalls
    

    // we skip decomposing syscall for LOG files, as they represent global log that helps us detect missing fsync bugs
    if (decompose_syscall_ && ea->file_path.find("LOG") == string::npos && eb->file_path.find("LOG") == string::npos) {
        return is_dependent_decomposed(ea, eb);
    }
    else {
        return false;
    }
}

bool posix_graph::is_dependent_decomposed(shared_ptr<trace_event> ea, shared_ptr<trace_event> eb) {
    assert(ea->micro_events && eb->micro_events && "Must call decompose_trace_events in trace first!");
    bool result = false; 
    // A: check block-overlapping updates
    // We do this outside of check for micro_events, as fallocate/ftruncate/sync_file_range may also have block ids and thus dependencies
    // Here we only check block id overlapping if they happen on the same file, without worrying about micro events actually
    if (ea->block_ids && eb->block_ids && ea->file_path == eb->file_path) {
        pair<int, int> block_ids_a = *ea->block_ids;
        pair<int, int> block_ids_b = *eb->block_ids;
        result = is_block_ids_overlapping(block_ids_a, block_ids_b);
    }

    // if no micro events, then stop here
    if (ea->micro_events->empty() || eb->micro_events->empty()) return result;
    if (ea->is_write_family() && eb->is_write_family() && ea->file_path == eb->file_path) {

        // B: check if eb is an extend
        for (const auto &me : *eb->micro_events) {
            if (me.is_metadata_update()) {
                result = true;
                break;
            }
        }
    }
    for (const auto &me_a : *ea->micro_events) {
        // C: if metadata update on the same file
        if (me_a.is_metadata_update()) {
            bool found = false;
            for (const auto &me_b : *eb->micro_events) {
                if (me_b.is_metadata_update() && me_a.file_path == me_b.file_path) {
                    found = true;
                    break;
                }
            }
            if (found) {
                result = true;
                break;
            }
        }
        // D: if inode data update on the same dir
        if (me_a.is_inode_data_update()) {
            bool found = false;
            for (const auto &me_b : *eb->micro_events) {
                if (me_b.is_inode_data_update() && me_a.file_path == me_b.file_path) {
                    found = true;
                    break;
                }
            }
            if (found) {
                result = true;
                break;
            }
        }
        // E: metadata update and inode data update must happen after inode is created
        if (me_a.is_add_file_inode() || me_a.is_add_dir_inode()) {
            bool found = false;
            for (const auto &me_b : *eb->micro_events) {
                if ((me_b.is_metadata_update() || me_b.is_inode_data_update()) && me_a.file_path == me_b.file_path) {
                    found = true;
                    break;
                }
            }
            if (found) {
                result = true;
                break;
            }
        }
        // // F: file inode create must happen after dir inode create
        // if (me_a.is_add_dir_inode()) {
        //     // if (ea->event_idx() == 159 && eb->event_idx() == 161) {
        //     //     cout << "STOP" << endl;
        //     // }
        //     bool found = false;
        //     for (const auto &me_b : *eb->micro_events) {
        //         if (me_b.is_add_file_inode() && get_dir_name(me_b.file_path) == me_a.file_path) {
        //             found = true;
        //             break;
        //         }
        //     }
        //     if (found) {
        //         result = true;
        //         break;
        //     }
        // }
    } 
    return result;
}

graph_type posix_graph::construct_graph() {
    // 1. Create all the nodes.
    nodes_ = create_nodes();
    // --- Just go ahead and add them to the graph, we add edges below.
    property_map pmap = boost::get(pnode_property_t(), graph_);
    for (const persistence_node *node : nodes_) {
        vertex v = boost::add_vertex(graph_);
        boost::put(pmap, v, node);
        vmap[node->event()] = v;
    }

    // enumerate over all pairs of nodes and add edges based on dependency
    for (auto it1 = nodes_.begin(); it1 != nodes_.end(); ++it1) {
        for (auto it2 = it1 + 1; it2 != nodes_.end(); ++it2) {
            if (is_dependent(*it1, *it2)) {
                boost::add_edge(vmap[(*it1)->event()], vmap[(*it2)->event()], graph_);
            }
        }
    }

    // // 2. Traverse through the events tracking fsyncs/fdatasyncs/msyncs to add edges.
    // // As a way to reduce edges, we will add fsync/fdatasync/msync as nodes also
    // typedef unordered_set<vertex> vertex_set;
    // // map from file to the set of vertices that are dirty
    // unordered_map<string, vertex_set> dirty_map;
    // // set of vertices that are clean and visible to all other events
    // vertex_set clean_list_global;
    // // set of vertices that are clean and only visible to the current file
    // unordered_map<string, vertex_set> clean_map;
    // // interval map for dirty MMIO stores, in cacheline granularity
    // icl::interval_map<uint64_t, vertex_set> dirty_tree;
    // // per-file interval map for syscal write, in blcok granularity
    // unordered_map<string, icl::interval_map<uint64_t, vertex>> write_tree;
    // // file -> open event map, make sure open call precedes any subseqeuent call on this fd
    // unordered_map<string, vertex> open_map;

    // // contains all address ranges **in the trace** that have been memory-mapped
    // // TODO: update mapped when munmap is called?
    // list<pair<shared_ptr<trace_event>, icl::discrete_interval<uint64_t>>> mapped;

    // for (shared_ptr<trace_event> te : trace_.events()) {
    //     if (te->is_store()) {
    //         const vertex &curr_v = vmap[te];

    //         // A. Simplification: the only implicit ordering is overlapping cache lines.

    //         vertex_set implicit_order;
    //         auto it = dirty_tree.find(te->cacheline_range());
    //         if (it != dirty_tree.end()) {
    //             implicit_order = it->second;
    //         }
 
    //         if (!implicit_order.empty()) {
    //             for (const vertex &before : implicit_order) {
    //                 boost::add_edge(before, curr_v, graph_);
    //             }

    //             // Then clear the overlap.
    //             dirty_tree.subtract(*it);
    //         }

    //         // B. Add an edge between this store and anything that's clean.
    //         for (const vertex &prior : clean_list_global) {
    //             assert(get(pmap, prior)->ts() < get(pmap, curr_v)->ts());
    //             assert(prior < curr_v);
    //             boost::add_edge(prior, curr_v, graph_);
    //         }

    //         // C. Finally, dirty the range (do this last to avoid having stores flush themselves).
    //         auto mapping = make_pair(te->cacheline_range(), vertex_set({curr_v}));
    //         dirty_tree.add(mapping);

    //         // D. Add an edge between mmap region and the store
    //         for (const auto &p : mapped) {
    //             if (icl::intersects(p.second, te->cacheline_range())) {
    //                 boost::add_edge(vmap[p.first], curr_v, graph_);
    //             }
    //         }
    //     // for MMIO, add mmap region to a mapping for MMAP
    //     } else if (te->is_register_file())  {
    //         mapped.push_back(make_pair(te, te->range()));
    //     } else if (te->is_unregister_file())  {
    //         // traverse dirty_tree and add an edge for any intersecting range
    //         const auto entries = dirty_tree & te->range();
    //         for (auto entry : entries) {
    //             for (auto v : entry.second) {
    //                 boost::add_edge(v, vmap[te], graph_);
    //             }
    //         }
    //         // if (it != dirty_tree.end()) {
    //         //     for (auto v : it->second) {
    //         //         boost::add_edge(v, vmap[te], graph_);
    //         //     }
    //         // }
    //         // also check clean_list_global and add an edge for any intersecting range
    //         // for (auto v : clean_list_global) {
    //         //     if (te->is_store() && icl::intersects(it->first, te->range())) {
    //         //         boost::add_edge(v, vmap[te], graph_);
    //         //     }
    //         // }
    //     // for MMIO, we need to also take care of msync
    //     } else if (te->is_msync()) {
    //         // Find any stores that overlap with this msync (address + size) in the dirty tree
    //         // Add an edge between the store and the msync
    //         // Finally, add msync to the clean list
    //         vertex_set flushed;
    //         const auto entries = dirty_tree & te->range();
    //         for (auto entry : entries) {
    //             for (auto v : entry.second) {
    //                 boost::add_edge(v, vmap[te], graph_);
    //             }
    //         }
    //         // if (it != dirty_tree.end()) {
    //         //     flushed = it->second;
    //         // }
    //         // for (auto v : flushed) {
    //         //     boost::add_edge(v, vmap[te], graph_);
    //         // }
    //         clean_list_global.insert(vmap[te]);
    //     }
    //     else if (te->is_fsync()) {
    //         // we will assume fsync only operates on local file
    //         // Find any events associated with this file in the dirty map and add them to clean list
    //         vertex_set flushed;
    //         const auto it = dirty_map.find(te->file_path);
    //         if (it != dirty_map.end()) {
    //             flushed = it->second;
    //         }
    //         for (auto v : flushed) {
    //             boost::add_edge(v, vmap[te], graph_);
    //         }
    //         clean_map[te->file_path].insert(vmap[te]);
    //         dirty_map.erase(te->file_path);
    //     }
    //     else if (te->is_fdatasync()) {
    //         // Find any events associated with this file in the dirty map and add them to clean list
    //         vertex_set flushed;
    //         const auto it = dirty_map.find(te->file_path);
    //         if (it != dirty_map.end()) {
    //             flushed = it->second;
    //         }
    //         for (auto v : flushed) {
    //             boost::add_edge(v, vmap[te], graph_);
    //         }
    //         clean_map[te->file_path].insert(vmap[te]);
    //         dirty_map.erase(te->file_path);
    //     }
    //     // else if (te->is_write()) {

    //     // }
    //     else {
    //         // A. get the file associated with the event and add to dirty map
    //         assert(te->file_path != "");
    //         dirty_map[te->file_path].insert(vmap[te]);
    //         // collect all clean events, including those that are only visible to the current file and those that are global
    //         vertex_set clean_list = clean_list_global;
    //         auto it = clean_map.find(te->file_path);
    //         if (it != clean_map.end()) {
    //             clean_list.insert(it->second.begin(), it->second.end());
    //         }
    //         // B. Add an edge between this event and anything that's clean.
    //         for (const vertex &prior : clean_list) {
    //             const posix_node *prior_node = dynamic_cast<const posix_node*>(boost::get(pmap, prior));
    //             const posix_node *curr_node = dynamic_cast<const posix_node*>(boost::get(pmap, vmap[te]));
    //             assert(prior_node->ts() < curr_node->ts());
    //             assert(prior < vmap[te]);
    //             // if no edge exists between prior and curr, add one
    //             if (!has_path(graph_, prior, vmap[te])) {
    //                 boost::add_edge(prior, vmap[te], graph_);
    //             }
    //         }

    //         // make open call precede any subseqeuent call on this fd
    //         if (te->is_open() || te->is_creat()) {
    //             open_map[te->file_path] = vmap[te];
    //         }
    //         else if (te->is_register_file() || te->is_close() || te->is_ftruncate() || te->is_pwrite64() || te->is_write() || te->is_writev() || te->is_lseek() || te->is_fsync() || te->is_fdatasync() || te->is_fallocate() || te->is_syncfs()) {
    //             auto it = open_map.find(te->file_path);
    //             // assert(it != open_map.end());
    //             if (it == open_map.end()) {
    //                 // TODO: this is unsafe as we are missing the open call, but just hack for now
    //                 cerr << "Warning: file " << te->file_path << " not found in open_map" << endl;
    //                 continue;
    //             }
    //             boost::add_edge(it->second, vmap[te], graph_);
            
    //         }
    //     }
    // }

    // 3. We're done creating the original graph! We have all the vertices and edges and types.
    // Do transitive reduction and visualize before and after.
    visualize(output_dir_.string() + "/graph.dot", graph_);
    output_stats();
    // transitive_reduction();
    // output_stats();
    // visualize(output_dir_.string() + "/graph_tr.dot", graph_);

    return graph_;

}

tuple<graph_type*, vertex, unordered_map<vertex, vertex>> posix_graph::generate_subgraph(std::vector<vertex> vertex_list) {
    tuple<graph_type*, vertex, unordered_map<vertex, vertex>> res  = persistence_graph::generate_subgraph(vertex_list);
    graph_type *subgraph = get<0>(res);
    vertex shadow_root = get<1>(res);
    unordered_map<vertex, vertex> new_to_old = get<2>(res);
    // iterate over new_to_old map
    // unordered_map<vertex, vector<vertex>> sync_to_out_targets;
    // Do two passes over all vertices
    // // 1. First pass, collect all sync family out targets
    // for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
    //     vertex new_v = it->first;
    //     const posix_node *new_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), *subgraph), new_v));
    //     shared_ptr<trace_event> te = new_node->event();
    //     // if te is a sync family event, we need to add all events associated with the same file to the subgraph
    //     if (te->is_sync_family()) {
    //         // get out edge targets of new_v
    //         boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
    //         for (boost::tie(ei, edge_end) = out_edges(new_v, *subgraph); ei != edge_end; ++ei) {
    //             vertex out_target = target(*ei, *subgraph);
    //             sync_to_out_targets[new_v].push_back(out_target);
    //         }
    //     }
    // }
    // // 2. Second pass, for each vertex that connects to a sync family event, add an edge with this vertex and all sync family out targets
    // for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
    //     vertex new_v = it->first;
    //     const posix_node *new_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), *subgraph), new_v));
    //     shared_ptr<trace_event> te = new_node->event();
    //     boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
    //     for (boost::tie(ei, edge_end) = out_edges(new_v, *subgraph); ei != edge_end; ++ei) {
    //         vertex out_target = target(*ei, *subgraph);
    //         if (sync_to_out_targets.find(out_target) != sync_to_out_targets.end()) {
    //             for (auto target_v : sync_to_out_targets[out_target]) {
    //                 boost::add_edge(new_v, target_v, *subgraph);
    //             }
    //         }
    //     }
        
    // }

    unordered_map<vertex, vector<vertex>> vertex_to_incoming_vertex;
    // Step 1: derive incoming edges as our graph type does not support in_edges
    // iterate over subgraph, for each vertex, collect all incoming vertices
    // go through out edges, put the current vertex in vector of out target
    for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
        vertex new_v = it->first;
        boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
        for (boost::tie(ei, edge_end) = out_edges(new_v, *subgraph); ei != edge_end; ++ei) {
            vertex out_target = target(*ei, *subgraph);
            vertex_to_incoming_vertex[out_target].push_back(new_v);
        }
    }

    // Step 2: iterate over subgraph
    // for each vertex, if the vertex is an event in sync_family, add an edge between its incoming vertices and its out vertices
    for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
        vertex new_v = it->first;
        if (new_v == shadow_root) continue;
        const posix_node *new_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), *subgraph), new_v));
        shared_ptr<trace_event> te = new_node->event();
        if (te->is_sync_family()) {
            for (auto in_v : vertex_to_incoming_vertex[new_v]) {
                // use out_edge method to derive out vertices
                boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
                for (boost::tie(ei, edge_end) = out_edges(new_v, *subgraph); ei != edge_end; ++ei) {
                    vertex out_v = target(*ei, *subgraph);
                    // if no edge exists between in_v and out_v, add one
                    // if (!has_path(*subgraph, in_v, out_v)) {
                    //     boost::add_edge(in_v, out_v, *subgraph);
                    // }
                    boost::graph_traits<graph_type>::out_edge_iterator ei_in, edge_end_in;
                    bool edge_exists = false;
                    for (boost::tie(ei_in, edge_end_in) = out_edges(in_v, *subgraph); ei_in != edge_end_in; ++ei_in) {
                        vertex out_target = target(*ei_in, *subgraph);
                        if (out_target == out_v) {
                            edge_exists = true;
                            break;
                        }
                    }
                    if (!edge_exists) {
                        boost::add_edge(in_v, out_v, *subgraph);
                    }
                }
            }
        }
    }
    // remove sync family events
    for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
        vertex new_v = it->first;
        if (new_v == shadow_root) continue;
        const posix_node *new_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), *subgraph), new_v));
        shared_ptr<trace_event> te = new_node->event();
        if (te->is_sync_family()) {
            // remove edges in and out of new_v
            boost::clear_vertex(new_v, *subgraph);
            // remove new_v from subgraph
            boost::remove_vertex(new_v, *subgraph);
        }
    }
    // we have to update new_to_old map as remove_index with vecS as VertexList will reindex the vertices
    // we do so by a double for loop on identifying trace events
    unordered_map<vertex, vertex> new_to_old_updated;
    // get all vertices in subgraph
    boost::graph_traits<graph_type>::vertex_iterator vi, vi_end;
    for (boost::tie(vi, vi_end) = boost::vertices(*subgraph); vi != vi_end; ++vi) {
        vertex new_v = *vi;
        if (new_v == shadow_root) continue;
        const posix_node *new_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), *subgraph), new_v));
        shared_ptr<trace_event> te = new_node->event();
        for (auto it = new_to_old.begin(); it != new_to_old.end(); ++it) {
            vertex old_v = it->second;
            const posix_node *old_node = dynamic_cast<const posix_node*>(boost::get(boost::get(pnode_property_t(), graph_), old_v));
            if (old_node->event() == te) {
                new_to_old_updated[new_v] = old_v;
                break;
            }
        }
    }
    return make_tuple(subgraph, shadow_root, new_to_old_updated);
}

} // namespace pathfinder