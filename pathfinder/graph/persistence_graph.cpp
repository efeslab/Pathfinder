#include "persistence_graph.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include <llvm/IR/Module.h>

using namespace std;
using namespace llvm;
namespace icl = boost::icl;
namespace fs = boost::filesystem;
namespace pathfinder {

/* utils */

bool has_path(const graph_type &g, vertex a, vertex b) {
    list<vertex> frontier{a};
    unordered_set<vertex> visited;
    while (!frontier.empty()) {
        vertex v = frontier.front();
        frontier.pop_front();

        graph_type::out_edge_iterator it, end;
        for (boost::tie(it, end) = boost::out_edges(v, g); it != end; ++it) {
            if (visited.count(it->m_target)) continue;
            visited.insert(it->m_target);
            if (b == it->m_target) return true;
            if (it->m_target < b) {
                // this part is PM specific. No back edges.
                frontier.push_back(it->m_target);
            }
        }
    }

    return false;
}

vector<vector<vertex>> split_vector(const vector<vertex> &verts,
    const vector<int> &idxs) {
    vector<vector<vertex>> ret;

    auto prior_it = verts.begin();
    for (int i : idxs) {
        auto current = verts.begin() + i + 1;
        vector<vertex> slice(prior_it, current);
        ret.push_back(slice);
        prior_it = current;
    }

    // Last split
    if (prior_it != verts.end()) {
        vector<vertex> slice(prior_it, verts.end());
        ret.push_back(slice);
    }

    return ret;
}

// bool TopologicalOrderGenerator::find_next() {

//     // Check if we have already found all possible orders
//     if (finished) return false;
//     bool all_visited = true;
//     vertex_iter vi, vi_end;
//     for (boost::tie(vi, vi_end) = vertices(subgraph); vi != vi_end; ++vi) {
//         vertex v = *vi;
//         if (in_degree[v] == 0 && !visited[v]) {
//             all_visited = false;
//             for (auto adj = adjacent_vertices(v, subgraph).first; adj != adjacent_vertices(v, subgraph).second; ++adj) {
//                 --in_degree[*adj];
//             }
//             order.push_back(v);
//             visited[v] = true;
//             pointers[v] = --order.end();
//             if (find_next()) return true;  // Found a complete ordering
//             visited[v] = false;
//             order.erase(pointers[v]);
//             for (auto adj = adjacent_vertices(v, subgraph).first; adj != adjacent_vertices(v, subgraph).second; ++adj) {
//                 ++in_degree[*adj];
//             }
//         }
//     }
//     if (order.size() == nodes) {
//         return true;  // A complete order has been found
//     }
//     if (all_visited) {
//         finished = true;
//     }
//     return false;  // No complete ordering found, continue searching
// }


// TopologicalOrderGenerator::TopologicalOrderGenerator(const graph_type& graph, int num_v)
//     : subgraph(graph), nodes(num_v), in_degree(num_v, 0), visited(num_v, false), pointers(num_v), num_vertices(num_v), finished(false) {
//     vertex_iter vi, vi_end;
//     for (boost::tie(vi, vi_end) = vertices(subgraph); vi != vi_end; ++vi) {
//         for (auto adj = adjacent_vertices(*vi, subgraph).first; adj != adjacent_vertices(*vi, subgraph).second; ++adj) {
//             ++in_degree[*adj];
//         }
//     }
// }

// vector<vertex> TopologicalOrderGenerator::next() {
//     if (finished) {
//         return {};  // If finished, return an empty vector indicating completion
//     }

//     if (order.size() == nodes) {
//         for (vertex v : order) {
//             visited[v] = false;
//             for (auto adj = adjacent_vertices(v, subgraph).first; adj != adjacent_vertices(v, subgraph).second; ++adj) {
//                 ++in_degree[*adj];
//             }
//         }
//         order.clear();
//     }
//     if (find_next()) {
//         return std::vector<vertex>(order.begin(), order.end());
//     }
//     return {};  // If no order is found, return an empty vector indicating completion
// }

void persistence_graph::visualize(const string &filename, const graph_type &graph) const {
    ofstream out(filename);
    boost::write_graphviz(out, graph, node_label_writer(graph));
}

pair<graph_type, unordered_map<vertex, vertex>> persistence_graph::transitive_reduction(graph_type& graph) {
    graph_type tr;
    map<graph_type::vertex_descriptor, graph_type::vertex_descriptor> orig_to_tr;
    vector<size_t> id_map(boost::num_vertices(graph));
    iota(id_map.begin(), id_map.end(), 0u);
    boost::transitive_reduction(graph, tr, boost::make_assoc_property_map(orig_to_tr), id_map.data());
    // for (auto &p : orig_to_tr) {
    //     tr[p.second] = graph_[p.first];
    // }

    // set node properties for the transitive reduction graph
    property_map pmap = boost::get(pnode_property_t(), tr);
    for (auto &p : orig_to_tr) {
        auto orig_v = p.first;
        auto tr_v = p.second;
        auto orig_node = boost::get(pnode_property_t(), graph, orig_v);
        boost::put(pmap, tr_v, orig_node);
    }
    // Check that the events are the same.
    for (auto &p : orig_to_tr) {
        auto orig_v = p.first;
        auto tr_v = p.second;
        auto orig_node = boost::get(pnode_property_t(), graph, orig_v);
        auto tr_node = boost::get(pnode_property_t(), tr, tr_v);
        assert((!orig_node && !tr_node) || (orig_node->event() == tr_node->event()));
    }
    // graph_ = tr;
    // update vmap
    // vmap.clear();
    // vertex_iter vi, vi_end;
    // unordered_map<shared_ptr<trace_event>, vertex> new_vmap;
    // for (boost::tie(vi, vi_end) = vertices(tr); vi != vi_end; ++vi) {
    //     new_vmap[boost::get(pnode_property_t(), tr, *vi)->event()] = *vi;
    // }
    unordered_map<vertex, vertex> tr_to_orig;
    for (auto &p : orig_to_tr) {
        tr_to_orig[p.second] = p.first;
    }
    return make_pair(tr, tr_to_orig);
}

void persistence_graph::output_stats(void) const {
    fs::path stats_file = output_dir_ / "graph_stats.txt";
    ofstream stats_out(stats_file.string(), ios::app);
    stats_out << "Graph stats:\n";
    stats_out << "  Vertices: " << boost::num_vertices(graph_) << "\n";
    stats_out << "  Edges: " << boost::num_edges(graph_) << "\n";
}

tuple<graph_type*, vertex, unordered_map<vertex, vertex>> persistence_graph::generate_subgraph(vector<vertex> vertex_list) {
    // extract a subgraph from the graph based on vertex_list
    graph_type *subgraph = new graph_type();
    unordered_map<vertex, vertex> old_to_new, new_to_old;
    // to resolve the problem of multiple nodes with no predecessors, we will create a shadow root node
    // and connect this shadow root node to all nodes with no predecessors 
    vertex shadow_root = add_vertex(*subgraph);

    for (auto v : vertex_list) {
        vertex new_v = add_vertex(boost::get(pnode_property_t(), graph_, v), *subgraph);
        // get the trace_event
        // shared_ptr<trace_event> te = boost::get(pnode_property_t(), graph_, v)->event();
        // cout << "vertex: " << v << " event: " << te->timestamp << endl;
        old_to_new[v] = new_v;
        new_to_old[new_v] = v;
    }

    for (auto v : vertex_list) {
        for (auto adj = adjacent_vertices(v, graph_).first; adj != adjacent_vertices(v, graph_).second; ++adj) {
            if (find(vertex_list.begin(), vertex_list.end(), *adj) != vertex_list.end()) {
                add_edge(old_to_new[v], old_to_new[*adj], *subgraph);
            }
        }
    }

    vector<int> in_degree(boost::num_vertices(*subgraph), 0);
    // Initialize in_degree for subgraph
    boost::graph_traits<graph_type>::vertex_iterator vi, vend;
    for (boost::tie(vi, vend) = vertices(*subgraph); vi != vend; ++vi) {
        vertex v = *vi;
        boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
        for (boost::tie(ei, edge_end) = out_edges(v, *subgraph); ei != edge_end; ++ei) {
            in_degree[target(*ei, *subgraph)]++;
        }
    }

    for (auto v : vertex_list) {
        if (in_degree[old_to_new[v]] == 0) {
            add_edge(shadow_root, old_to_new[v], *subgraph);
        }
    }
    // for debug, plot the subgraph
    stringstream ss;
    ss << "/subgraph_before_tr_" << subgraphs_.size() << ".dot";
    visualize(output_dir_.string() + ss.str(), *subgraph);

    // we do transitive reduction here when we get the subgraph
    // the tricky part is to update the vertex mapping as transitive reduction will re-index the vertices
    pair<graph_type, unordered_map<vertex, vertex>> res = transitive_reduction(*subgraph);
    *subgraph = res.first;
    unordered_map<vertex, vertex> tr_to_new = res.second;
    // update the vertex mapping
    unordered_map<vertex, vertex> new_to_old_updated;
    vertex new_shadow_root;
    // for (auto &p : new_to_old) {
    //     // get trace_event from orignal node
    //     shared_ptr<trace_event> te = boost::get(pnode_property_t(), graph_, p.second)->event();
    //     auto it = new_vmap.find(te);
    //     assert(it != new_vmap.end());
    //     transitive_new_to_new[it->second] = p.first;
    // }
    for (auto &p : tr_to_new) {
        new_to_old_updated[p.first] = new_to_old[p.second];
        if (p.second == shadow_root) {
            new_shadow_root = p.first;
        }
    }

    // for debug, plot the subgraph
    // ss.str("");
    // ss << "/subgraph_after_tr_" << subgraphs_.size() << ".dot";
    // visualize(output_dir_.string() + ss.str(), *subgraph);

    return make_tuple(subgraph, new_shadow_root, new_to_old_updated);
}

set<set<vertex>> persistence_graph::generate_all_orders(vector<vertex> vertex_list, atomic<bool>& cancel_flag) {
    tuple<graph_type*, vertex, unordered_map<vertex, vertex>> res = generate_subgraph(vertex_list);
    graph_type *subgraph = get<0>(res);
    vertex shadow_root = get<1>(res);
    unordered_map<vertex, vertex> new_to_old = get<2>(res);

    PartialOrderGenerator *og = new PartialOrderGenerator(*subgraph);

    // prevent memory leak
    order_generators_.push_back(og);
    subgraphs_.push_back(subgraph);
    stringstream ss;

    // some debugging visualization
    ss << "/subgraph_" << subgraphs_.size()-1 << ".dot";
    visualize(output_dir_.string() + ss.str(), *subgraph);

    set<set<vertex>> orders = og->generate_all_orders(cancel_flag, shadow_root);
    set<set<vertex>> processed_orders;
    for (auto order : orders) {
        if (order.size() == 0) continue;
        set<vertex> processed_order;
        for (auto v : order) {
            if (v == shadow_root) continue;
            processed_order.insert(new_to_old[v]);
        }
        processed_orders.insert(processed_order);
    }

    // free(og);
    // free(subgraph);
    
    return processed_orders;
}


}