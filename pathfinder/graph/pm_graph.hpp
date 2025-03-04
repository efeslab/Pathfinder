#pragma once

#include "persistence_graph.hpp"

namespace pathfinder
{

class pm_node: public persistence_node {
    std::unordered_map<const llvm::Type *, const type_info *> type_mapping_;
    const llvm::Module &module_;

public:
    const type_crawler::type_info_set &type_associations;

    pm_node(
        const llvm::Module &m,
        std::shared_ptr<trace_event> te,
        const type_crawler::type_info_set &ta);

    bool is_member_of(const llvm::Type *t) const;
    uint64_t offset_in(const llvm::Type *t) const;
    boost::icl::discrete_interval<uint64_t> field(const llvm::Type *t) const;
    bool field_is_array_type(const llvm::Type *t) const;
    uint64_t instance_address(const llvm::Type *t) const;

    size_t type_size(const llvm::Type *t) const {
        return module_.getDataLayout().getTypeSizeInBits(
            const_cast<llvm::Type*>(t)).getFixedSize() / 8;
    }

    bool is_equivalent(const llvm::Type *t, const pm_node &other) const;

    const llvm::Module &module(void) const { return module_; }
};

class pm_graph: public persistence_graph {

    const llvm::Module &module_;
    type_crawler type_crawler_;

    /**
     * Constructs the graph nodes with type information.
     */
    std::vector<persistence_node*> create_nodes(void) const;

    /**
     * A function that checks happens-before relationships between two nodes.
     */
    // bool is_dependent(persistence_node a, persistence_node b) const;

    /**
     * Construct all ordering relationships.
     */
    graph_type construct_graph();

public:
    pm_graph(const llvm::Module &m, const trace &t, const boost::filesystem::path &output_dir);

    const type_crawler &type_crawler(void) const { return type_crawler_; }
    const trace &trace(void) const { return trace_; }
    

    int get_event_idx(vertex v) const;

};

} // namespace pathfinder