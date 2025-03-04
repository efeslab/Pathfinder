#pragma once

#include <numeric>
#include <memory>
#include <tuple>
#include <unordered_map>

#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include <boost/filesystem.hpp>
#include <boost/icl/discrete_interval.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graphviz.hpp>
#include <boost/graph/transitive_reduction.hpp>

#include "../utils/common.hpp"
#include "../utils/file_utils.hpp"
#include "../trace/trace.hpp"
#include "../type_crawler/type_crawler.hpp"

namespace pathfinder
{


class persistence_node {
protected:
    const std::shared_ptr<trace_event> event_;

public:
    persistence_node(std::shared_ptr<trace_event> te) : event_(te) {}

    virtual ~persistence_node() {}

    const std::shared_ptr<trace_event> event(void) const { return event_; }


    uint64_t ts(void) const { return event_->timestamp; }

    bool is_store(void) const { return event_->is_store(); }
    bool is_flush(void) const { return event_->is_flush(); }
    bool is_fence(void) const { return event_->is_fence(); }

};

struct pnode_property_t {
    typedef boost::vertex_property_tag kind;
};

typedef boost::property<
    pnode_property_t /* Tag */,
    const persistence_node* /* Value */,
    boost::no_property /* next property */> persistence_node_property;

typedef boost::adjacency_list<
    boost::vecS /* OutEdgeList (default) */,
    boost::vecS /* VertexList (default) */,
    boost::directedS /* Directed? */,
    persistence_node_property /* VertexProperties */,
    boost::no_property /* EdgeProperties (default) */,
    boost::no_property /* GraphProperties (default) */,
    boost::listS /* EdgeList (default) */
> graph_type;

// for visualization
struct node_label_writer {
    const graph_type& g;

    node_label_writer(const graph_type& graph) : g(graph) {}

    template <class Vertex>
    void operator()(std::ostream& out, const Vertex& v) const {
        auto pnode_prop = get(pnode_property_t(), g, v);
        if (pnode_prop) { // Check if the property is not null
            // Assuming you want to use a 'name' field from the persistence_node
            out << "[label=\"" << pnode_prop->event()->event_idx() 
                << "\n " << event_type_to_str(pnode_prop->event()->type) << "\"]";
        } else {
            // Default case if the property is null or doesn't have a name
            out << "[label=\"\"]";
        }
    }
};

typedef std::pair<size_t, size_t> edge;
typedef boost::graph_traits<graph_type>::vertex_descriptor vertex;
typedef boost::graph_traits<graph_type>::vertex_iterator vertex_iter;

struct edge_hash {
    uint64_t operator()(const std::pair<vertex, vertex> &e) const {
        return std::hash<vertex>{}(e.first) ^ std::hash<vertex>{}(e.second);
    }
};

typedef boost::property_map<graph_type, pnode_property_t>::type property_map;
typedef boost::property_map<graph_type, pnode_property_t>::const_type const_property_map;

typedef std::vector<vertex> update_mechanism;
/**
 * @brief The representative is the first node.
 *
 */
typedef std::vector<update_mechanism> update_mechanism_group;


/**
 * @brief This uses specific properties of the persistence graph.
 *
 * @param g
 * @param a
 * @param b
 * @return true
 * @return false
 */
bool has_path(const graph_type &g, vertex a, vertex b);

/**
 * @brief Split the vertex at the points given, not including beginning and end.
 *
 * Split point is after the idx.
 *
 * split_vector([1,2,3], [1]) -> [[1,2], [3]]
 * split_vector([a,b,c,d], [0,2]) -> [[a], [b,c], [d]]
 *
 * @param verts
 * @param idxs
 * @return vector<vector<vertex>>
 */
std::vector<std::vector<vertex>> split_vector(
    const std::vector<vertex> &verts,
    const std::vector<int> &idxs);

// class TopologicalOrderGenerator {
// private:
//     graph_type subgraph;
//     std::vector<int> in_degree;
//     std::list<vertex> order;
//     std::vector<bool> visited;
//     std::vector<std::list<vertex>::iterator> pointers;
//     int num_vertices;
//     bool finished;
//     int nodes;

//     bool find_next();

// public:
//     TopologicalOrderGenerator(const graph_type& graph, int num_v);

//     std::vector<vertex> next();

//     void reset() {
//         std::fill(visited.begin(), visited.end(), false);
//         order.clear();
//         finished = false;
//     }

//     std::list<vertex> nextOrder() {
//         if (finished) {
//             return {};  // No more orders to generate
//         }

//         // Clear previous state
//         order.clear();
//         std::fill(visited.begin(), visited.end(), false);

//         // Attempt to build the next order
//         for (int i = 0; i < num_vertices; ++i) {
//             if (!visited[i] && in_degree[i] == 0) {
//                 dfs(i);  // Perform DFS from this vertex
//                 break;  // Only start from one vertex for each call
//             }
//         }

//         // Check if we've exhausted all starting points
//         if (order.empty()) {
//             finished = true;
//         }

//         return order;
//     }

//     void dfs(vertex v) {
//         visited[v] = true;
//         order.push_back(v);  // Add to current order

//         // Recursively visit all adjacent vertices
//         boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
//         for (boost::tie(ei, edge_end) = out_edges(v, subgraph); ei != edge_end; ++ei) {
//             vertex u = target(*ei, subgraph);
//             if (!visited[u]) {
//                 dfs(u);
//             }
//         }
//     }
// };

class PartialOrderGenerator {
private:
    graph_type& subgraph;
    std::vector<int> in_degree;
    std::set<vertex> order;
    std::set<std::set<vertex>> orders;
    std::vector<bool> visited;
    std::vector<std::set<vertex>::iterator> pointers;
    int num_vertices;

public:
    PartialOrderGenerator(graph_type& graph) : subgraph(graph), num_vertices(boost::num_vertices(subgraph)) {
        in_degree.resize(num_vertices, 0);
        visited.resize(num_vertices, false);
        pointers.resize(num_vertices);

        // Initialize in_degree
        boost::graph_traits<graph_type>::vertex_iterator vi, vend;
        for (boost::tie(vi, vend) = vertices(subgraph); vi != vend; ++vi) {
            boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
            for (boost::tie(ei, edge_end) = out_edges(*vi, subgraph); ei != edge_end; ++ei) {
                // in_degree[target(*ei, subgraph)]++;
                auto idx = boost::get(boost::vertex_index, subgraph, target(*ei, subgraph));
                if (idx < num_vertices) {
                    in_degree[idx]++;
                }
            }
        }
    }

    // Resets all orders and visited information to start generating from scratch
    void reset() {
        std::fill(visited.begin(), visited.end(), false);
        order.clear();
    }

    // Function to generate all partial orders
    bool generate_orders(vertex v, std::atomic<bool>& cancel_flag, bool root = true) {
        if (cancel_flag.load()) {
            return false;
        }
        if (!root) {
            // Decrement inDegrees of all successors
            boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
            for (boost::tie(ei, edge_end) = out_edges(v, subgraph); ei != edge_end; ++ei) {
                // vertex u = target(*ei, subgraph);
                auto idx = boost::get(boost::vertex_index, subgraph, target(*ei, subgraph));
                if (idx < num_vertices) {
                    in_degree[idx]--;
                }
            }

            visited[v] = true;
            // order.push_back(v);
            order.insert(v);
        }

        // Output the current partial order
        // for (Vertex u : order) {
        //     std::cout << u << " ";
        // }
        // std::cout << std::endl;
        bool existed = false;
        for (const auto &existing_order : orders) {
            if (order == existing_order)
                existed = true;
        }
        if (!existed)
            orders.insert(order);

        bool extended = false;
        boost::graph_traits<graph_type>::vertex_iterator vi, vend;
        for (boost::tie(vi, vend) = vertices(subgraph); vi != vend; ++vi) {
            if (in_degree[*vi] == 0 && !visited[*vi]) {
                extended = true;
                generate_orders(*vi, cancel_flag, false);  // Recursive call
            }
        }

        if (!root) {
            // Restore the state for other branches of the exploration
            boost::graph_traits<graph_type>::out_edge_iterator ei, edge_end;
            for (boost::tie(ei, edge_end) = out_edges(v, subgraph); ei != edge_end; ++ei) {
                auto idx = boost::get(boost::vertex_index, subgraph, target(*ei, subgraph));
                if (idx < num_vertices) {
                    in_degree[idx]++;
                }
            }
            visited[v] = false;
            // order.pop_back();
            order.erase(v);
        }

        return extended;
    }

    // Function to initiate the generation of orders
    std::set<std::set<vertex>> generate_all_orders(std::atomic<bool>& cancel_flag, vertex root=0) {
        reset(); // Reset state
        generate_orders(root, cancel_flag, true); // Start generation from root
        return orders;
    }

    // Function to print all orders
    void print_orders() {
        for (const auto &order : orders) {
            for (vertex u : order) {
                std::cout << u << " ";
            }
            std::cout << std::endl;
        }
    }
};

class persistence_graph {
protected:
    const trace &trace_;
    std::vector<persistence_node*> nodes_;
    graph_type graph_;
    std::vector<PartialOrderGenerator*> order_generators_;
    std::vector<graph_type*> subgraphs_;
    // map from trace_event to vertex
    // we may not need a map from vertex to trace event, since we can get the trace event from the vertex's node property
    std::unordered_map<std::shared_ptr<trace_event>, vertex> vmap;

    boost::filesystem::path output_dir_;

    virtual bool is_dependent(const persistence_node* a, const persistence_node* b) { return false; };

    /**
     * Constructs the graph nodes.
     */
    virtual std::vector<persistence_node*> create_nodes(void) const = 0;

    /**
     * A function that checks happens-before relationships between two nodes.
     */
    // virtual bool is_dependent(persistence_node a, persistence_node b) const = 0;

    /**
     * Construct all ordering relationships.
     */
    virtual graph_type construct_graph(void) = 0;

    /**
     * Given a list of vertices, generate a subgraph.
    */
    virtual std::tuple<graph_type*, vertex, std::unordered_map<vertex, vertex>> generate_subgraph(std::vector<vertex> vertex_list);

public:
    persistence_graph(const trace &t, const boost::filesystem::path output_dir) : trace_(t), output_dir_(output_dir){
    }

    virtual ~persistence_graph() {
        for (auto n : nodes_) {
            delete n;
        }

        for (auto og : order_generators_) {
            delete og;
        }

        for (auto sg : subgraphs_) {
            delete sg;
        }

    }

    const trace &get_trace(void) const { return trace_; }

    vertex get_vertex(const std::shared_ptr<trace_event> &te) const {
        return vmap.at(te);
    }


    // visualization, output to file
    void visualize(const std::string &filename, const graph_type &graph) const;

    // transitive reduction, given a boost graph, return the graph after transitive reduction and a new vertex to old vertex mapping
    std::pair<graph_type, std::unordered_map<vertex, vertex>>  transitive_reduction(graph_type& graph);

    // output stats of the graph to file
    void output_stats(void) const;

    // generate all partial orders given a list of vertex in the persistence graph
    std::set<std::set<vertex>> generate_all_orders(std::vector<vertex> vertex_list, std::atomic<bool>& cancel_flag);
    
    // get the boost graph representation
    const graph_type &whole_program_graph(void) const { return graph_; }

};


} // namespace pathfinder
