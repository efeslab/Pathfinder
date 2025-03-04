#pragma once

#include "persistence_graph.hpp"

namespace pathfinder
{

class posix_node: public persistence_node {
public:
    posix_node(std::shared_ptr<trace_event> te) : persistence_node(te) {}

    bool is_equivalent(std::string function, const posix_node &other) const;
};

class posix_graph: public persistence_graph {
private:
    bool decompose_syscall_;
    /**
     * Constructs the graph nodes with type information.
     */
    std::vector<persistence_node*> create_nodes(void) const;

    /**
     * A function that checks happens-before relationships between two nodes.
     */
    bool is_dependent(const persistence_node* a, const persistence_node* b);

    /**
     * A function that checks happens-before relationship for decomposed syscalls.
     * The rules are derived from Ferrite paper (https://dl.acm.org/doi/10.1145/2872362.2872406)
    */
    bool is_dependent_decomposed(std::shared_ptr<trace_event> ea, std::shared_ptr<trace_event> eb);

    /**
     * Construct all ordering relationships.
     */
    graph_type construct_graph();

    std::tuple<graph_type*, vertex, std::unordered_map<vertex, vertex>> generate_subgraph(std::vector<vertex> vertex_list);

public:
    posix_graph(const trace &t, const boost::filesystem::path &output_dir, bool decompose_syscall);
};


} // namespace pathfinder