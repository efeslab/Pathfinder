#include "pm_graph.hpp"

using namespace std;
using namespace llvm;
namespace icl = boost::icl;
namespace fs = boost::filesystem;


namespace pathfinder
{

/* pm_node */

pm_node::pm_node(
    const Module &m,
    std::shared_ptr<trace_event> te,
    const type_crawler::type_info_set &ta)
: persistence_node(te), type_mapping_(), module_(m), type_associations(ta) {
    for (const type_info &ti : type_associations) {
        type_mapping_[ti.type] = &ti;
    }
}

bool pm_node::is_member_of(const Type *t) const {
    return !!type_mapping_.count(t);
}

uint64_t pm_node::offset_in(const Type *t) const {
    // I want this to raise an exception if it's not found.
    const type_info *ti = type_mapping_.at(t);
    assert(!ti->needs_interpolation());
    assert(ti->offset_in_type < ti->type_size() || ti->has_zero_sized_element());
    return ti->offset_in_type;
}

bool pm_node::field_is_array_type(const llvm::Type *t) const {
    const type_info *ti = type_mapping_.at(t);
    assert(!ti->needs_interpolation());

    uint64_t event_offset = offset_in(t);
    assert(event_offset < ti->type_size() || ti->has_zero_sized_element());

    /**
     * Iterate through the actual fields in this type, and return that field
     */

    // Just consider this field to be the "end".
    if (ti->has_zero_sized_element() && event_offset >= ti->type_size()) {
        return true;
    }


    if (const StructType *st = dyn_cast<StructType>(t)) {
        // cerr << "FIELD: [" << event_offset << ", " << (event_offset + event_->size) << ")\n";
        // llvm::errs() << "\t" << *t << "\n";
        uint64_t current_offset = 0;
        for (const Type *et : st->elements()) {
            uint64_t sz = module_.getDataLayout().getTypeSizeInBits(const_cast<Type*>(et)).getFixedSize() / 8;
            // cerr << "\t> [" << current_offset << ", " << (current_offset + sz) << ")\n";
            if (current_offset <= event_offset &&
                event_offset + event_->size <= current_offset + sz) {
                // cerr << "DING\n";
                return isa<ArrayType>(et);
            }

            current_offset += sz;
        }
    } else if (const ArrayType *aty = dyn_cast<ArrayType>(t)) {
        return isa<ArrayType>(aty->getElementType());
    }

    return false;
}

icl::discrete_interval<uint64_t> pm_node::field(const llvm::Type *t) const {
    const type_info *ti = type_mapping_.at(t);
    assert(!ti->needs_interpolation());

    uint64_t event_offset = offset_in(t);
    assert(event_offset < ti->type_size() || ti->has_zero_sized_element());

    /**
     * Iterate through the actual fields in this type, and return that field
     */

    // Just consider this field to be the "end".
    if (ti->has_zero_sized_element() && event_offset >= ti->type_size()) {
        return icl::interval<uint64_t>::right_open(ti->type_size(), ti->type_size() + 1);
    }


    const StructType *st = dyn_cast<StructType>(t);
    if (!st) {
        const ArrayType *aty = dyn_cast<ArrayType>(t);

        if (aty) {
            uint64_t sz = module_.getDataLayout().getTypeSizeInBits(
                const_cast<Type*>(aty->getElementType())).getFixedSize() / 8;

            for (uint64_t e = 0; e < aty->getNumElements(); ++e) {
                uint64_t current_offset = e * sz;
                if (current_offset <= event_offset &&
                    event_offset + event_->size <= current_offset + sz) {
                    return icl::interval<uint64_t>::right_open(current_offset, current_offset + sz);
                }

                current_offset += sz;
            }
        } else {
            return icl::interval<uint64_t>::right_open(0, ti->type_size());
        }

        return icl::interval<uint64_t>::right_open(event_offset, event_offset + event_->size);
    }

    // cerr << "FIELD: [" << event_offset << ", " << (event_offset + event_->size) << ")\n";
    // llvm::errs() << "\t" << *t << "\n";
    uint64_t current_offset = 0;
    for (const Type *et : st->elements()) {
        uint64_t sz = module_.getDataLayout().getTypeSizeInBits(const_cast<Type*>(et)).getFixedSize() / 8;
        // cerr << "\t> [" << current_offset << ", " << (current_offset + sz) << ")\n";
        if (current_offset <= event_offset &&
            event_offset + event_->size <= current_offset + sz) {
            // cerr << "DING\n";
            return icl::interval<uint64_t>::right_open(current_offset, current_offset + sz);
        }

        current_offset += sz;
    }

    // cerr << __PRETTY_FUNCTION__ << " | error: shouldn't reach!\n";
    // exit(EXIT_FAILURE);
    // If we get here, that means the application is doing something a bit non-standard,
    // so just let this go through.

    return icl::interval<uint64_t>::right_open(event_offset, event_offset + event_->size);
}

uint64_t pm_node::instance_address(const Type *t) const {
    return event_->address - offset_in(t);
}

bool pm_node::is_equivalent(
    const llvm::Type *t, const pm_node &other) const {

    /**
     * This ends up being way too specific. Need to go with fields, which is
     * how we split epochs and such anyways.
     */

    // uint64_t offset_a, offset_b;
    // uint64_t size_a, size_b;
    // // assert(is_member_of(t) && other.is_member_of(t));

    // offset_a = offset_in(t);
    // offset_b = other.offset_in(t);
    // size_a = event_->size;
    // size_b = other.event_->size;
    // return offset_a == offset_b && size_a == size_b;

    return field(t) == other.field(t);
}

/* pm_graph */

vector<persistence_node*> pm_graph::create_nodes(void) const {
    vector<persistence_node*> nodes;
    // Only stores get nodes. Everything else just makes edges.
    for (const auto te : trace_.stores()) {
        nodes.push_back(new pm_node(module_, te, type_crawler_.all_types(te)));
    }

    return nodes;
}

pm_graph::pm_graph(const Module &m, const class trace &t, const fs::path &output_dir)
    : persistence_graph(t, output_dir), module_(m), type_crawler_(m, t) {

    graph_ = construct_graph();
}

graph_type pm_graph::construct_graph() {
        // 1. Create all the nodes.
    nodes_ = create_nodes();
    // --- Just go ahead and add them to the graph, we add edges below.
    property_map pmap = boost::get(pnode_property_t(), graph_);
    for (const persistence_node *node : nodes_) {
        vertex v = boost::add_vertex(graph_);
        boost::put(pmap, v, node);
        vmap[node->event()] = v;
        const pm_node* n = dynamic_cast<const pm_node*>(boost::get(pmap, v));
        assert(n);
    }

    // 2. Traverse through the events tracking flushes/fences to add edges.
    typedef unordered_set<vertex> vertex_set;
    icl::interval_map<uint64_t, vertex_set> dirty_tree, flush_tree;
    vertex_set clean_list;

    // - Need to acutally iterate through all trace events, since they have
    // - the flushes/fences too.
    for (shared_ptr<trace_event> te : trace_.events()) {

        if (te->is_store()) {
            const vertex &curr_v = vmap[te];

            // A. Force flush the dirty range if there is overlap
            vertex_set implicit_flush;
            auto it = flush_tree.find(te->cacheline_range());
            if (it != flush_tree.end()) {
                implicit_flush = it->second;
            }

            if (!implicit_flush.empty()) {
                for (const vertex &clean : clean_list) {
                    for (const vertex &flushed : implicit_flush) {
                        /*
                        Now, order everything in "flushed" after everything in clean,
                        but only if the timestamp of what is clean is before what is
                        in the flushed list, because according to Intel TSO (which
                        is our target), if timeA < timeB, B cannot be before A.
                        */
                        const pm_node *clean_node = dynamic_cast<const pm_node*>(boost::get(pmap, clean));
                        const pm_node *flushed_node = dynamic_cast<const pm_node*>(boost::get(pmap, flushed));
                        assert(clean_node && flushed_node);
                        if (clean_node->ts() < flushed_node->ts()) {
                            boost::add_edge(clean, flushed, graph_);
                        }
                    }
                }

                flush_tree.subtract(*it);
            }

            // B. Do implicit orderings.
            // There are also some implicit ORDERINGS which are not implicit flushes.

            vertex_set implicit_order;
            it = dirty_tree.find(te->cacheline_range());
            if (it != dirty_tree.end()) {
                implicit_order = it->second;
            }

            if (!implicit_order.empty()) {
                for (const vertex &before : implicit_order) {
                    boost::add_edge(before, curr_v, graph_);
                }

                // Then clear the overlap.
                dirty_tree.subtract(*it);
            }

            // C. Add an edge between this store and anything that's clean.
            for (const vertex &prior : clean_list) {
                assert(get(pmap, prior)->ts() < get(pmap, curr_v)->ts());
                assert(prior < curr_v);
                boost::add_edge(prior, curr_v, graph_);
            }

            // D. Finally, dirty the range (do this last to avoid having stores flush themselves).
            auto mapping = make_pair(te->cacheline_range(), vertex_set({curr_v}));
            dirty_tree.add(mapping);
        } else if (te->is_flush()) {
            vertex_set flushed;
            const auto it = dirty_tree.find(te->cacheline_range());
            if (it != dirty_tree.end()) {
                flushed = it->second;
            }

            if (!flushed.empty()) {
                // No edges yet. We just add to the other tree.
                flush_tree.add(make_pair(te->cacheline_range(), flushed));

                // Then clear the overlap.
                dirty_tree.subtract(*it);
            }
        } else if (te->is_fence()) {
            /**
             * @brief We actually want to draw the edges later, i.e., every store
             * after something which is clean comes after everything that is clean.
             * Doing it this way waits to draw edges between stores until a store
             * is flushed and fenced, i.e.
             *
             * if store A, flush A, fence, store A again, there should be an edge
             * between the two store As, but this doesn't happen, since this waits
             * to do the edge until after the second store is flushed/fenced.
             *
             */
            // for (const vertex &before : clean_list) {
            //     for (const auto &p : flush_tree) {
            //         /*
            //         # Now, order everything in "flushed" after everything in clean,
            //         # but only if the timestamp of what is clean is before what is
            //         # in the flushed list, because according to Intel TSO (which
            //         # is our target), if timeA < timeB, B cannot be before A.
            //         */
            //         for (const vertex &persisted : p.second) {
            //             const pm_node *clean_node = boost::get(pmap, before);
            //             const pm_node *flushed_node = boost::get(pmap, persisted);
            //             if (clean_node->ts() < flushed_node->ts()) {
            //                 boost::add_edge(before, persisted, graph_);
            //             }
            //         }
            //     }
            // }

            // Now clean it all up.
            if (!flush_tree.empty()) {
                // clean_list.clear();
                for (const auto &p : flush_tree) {
                    clean_list.insert(p.second.begin(), p.second.end());
                }
                flush_tree.clear();
            }
        // for MMIO, we need to also take care of msync
        } else if (te->is_msync()) {
            // Find any stores that overlap with this msync (address + size) in the dirty tree
            // and add them to clean list
            vertex_set flushed;
            const auto it = dirty_tree.find(te->range());
            if (it != dirty_tree.end()) {
                flushed = it->second;
            }
            for (auto v : flushed) {
                clean_list.insert(v);
            }
        }
    }

    // 3. We're done creating the original graph! We have all the vertices and edges and types.
    // Now do transitive reduction and visualize before and after.
    visualize(output_dir_.string() + "/graph.dot", graph_);
    // transitive_reduction();
    // visualize(output_dir_.string() + "/graph_tr.dot", graph_);

    return graph_;
}


int pm_graph::get_event_idx(vertex v) const {
    const_property_map pmap = boost::get(pnode_property_t(), graph_);
    shared_ptr<trace_event> te = boost::get(pmap, v)->event();
    // The timestamp should be an acceptable proxy
    assert(trace_.events()[te->timestamp] == te);
    return te->timestamp;
}

} // namespace pathfinder