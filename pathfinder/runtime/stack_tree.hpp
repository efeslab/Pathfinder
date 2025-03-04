#pragma once

#include <memory>

#include "../include/tree.hh"
#include "../graph/persistence_graph.hpp"
#include "../graph/pm_graph.hpp"
#include "../graph/posix_graph.hpp"
#include "../trace/stack_frame.hpp"

namespace pathfinder
{
    class stack_tree_node {
    public:
        std::string function;
        update_mechanism_group um_group;
        int height;
        
        stack_tree_node(std::string func) : function(func), um_group(update_mechanism_group()), height(-1) {}
    };

    // a wrapper for the tree data structure, specialized for processing call stacks
    class stack_tree {

        tree<std::shared_ptr<stack_tree_node>> _tree;
        const persistence_graph &_pg;
        boost::filesystem::path _output_dir;

        void update_height_helper(tree<std::shared_ptr<stack_tree_node>>::iterator it, int height);

    public:

        // constructor, will create a shadow root and set it as head
        // this is for better support thread or fork that does not has main function
        stack_tree(std::string root_name, const persistence_graph &pg, boost::filesystem::path output_dir);

        // print the tree, utility function
        void print(std::string file_name, bool print_um_group = false);

        // get root as iterator
        tree<std::shared_ptr<stack_tree_node>>::iterator root() { return _tree.begin(); }

        // get parent as iterator
        tree<std::shared_ptr<stack_tree_node>>::iterator parent(tree<std::shared_ptr<stack_tree_node>>::iterator it) { return _tree.parent(it); }

        // compact the tree, remove the nodes that has no update mechanism in um_group
        void compact();

        // consume the current backtrace, update the call stack tree, and return the leaf node (one with the most recent call)
        tree<std::shared_ptr<stack_tree_node>>::iterator process_backtrace(const std::vector<stack_frame> &backtrace);

        // get all leaf nodes
        std::vector<tree<std::shared_ptr<stack_tree_node>>::iterator> leaves();

        bool is_leaf(tree<std::shared_ptr<stack_tree_node>>::iterator it);

        // get all children of a node
        std::vector<tree<std::shared_ptr<stack_tree_node>>::iterator> children(tree<std::shared_ptr<stack_tree_node>>::iterator it);

        // update depth of all nodes
        void update_height();

        std::vector<tree<std::shared_ptr<stack_tree_node>>::iterator> get_nodes_at_height(int height);

        // destructor, will delete the tree and free every node
        ~stack_tree();

    };

}
