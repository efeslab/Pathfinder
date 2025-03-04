#include "stack_tree.hpp"

namespace fs = boost::filesystem;
using namespace std;

namespace pathfinder
{

    // we will create a shadow root node for the tree
    stack_tree::stack_tree(string root_name, const persistence_graph &pg, fs::path output_dir) : _tree(tree<shared_ptr<stack_tree_node>>()), _pg(pg), _output_dir(output_dir){
        // stack_tree_node* root_node = new stack_tree_node(root_name);
        shared_ptr<stack_tree_node> root_node = make_shared<stack_tree_node>(root_name);
        _tree.set_head(root_node);
    }

    void stack_tree::print(string file_name, bool print_um_group) {
        this->update_height();
        fs::path log_path = _output_dir / file_name;
        ofstream log_file(log_path.string());

        log_file << "Printing the tree" << endl;
        tree<shared_ptr<stack_tree_node>>::iterator it = _tree.begin();
        tree<shared_ptr<stack_tree_node>>::iterator end = _tree.end();
        if(!_tree.is_valid(it)) return;
        int rootdepth=_tree.depth(it);
        log_file << "-----" << endl;
        const_property_map pmap = boost::get(pnode_property_t(), _pg.whole_program_graph());
        while(it != end) {
            log_file << "D" << _tree.depth(it) - rootdepth << " ";
            for(int i = 0; i<_tree.depth(it) - rootdepth; ++i) 
                log_file << "--";
            log_file << (*it)->function << " ";
            log_file << "(H:" << (*it)->height << ")" << endl << flush;
            update_mechanism_group umg = (*it)->um_group;
            if (print_um_group) {
                for (int i = 0; i < umg.size(); ++i) {
                    for(int i = 0; i<_tree.depth(it) - rootdepth; ++i) 
                        log_file << "  ";
                    log_file << "#Update protocol " << i << endl;
                    for (int j = 0; j < umg[i].size(); ++j) {
                        // get source code line first
                        const posix_node *node = dynamic_cast<const posix_node*>(boost::get(pmap, umg[i][j]));
                        shared_ptr<trace_event> event = node->event();
                        int line = -1;
                        string file;
                        for (int k = 0; k < event->backtrace().size(); ++k) {
                            if (event->backtrace()[k].function == (*it)->function) {
                                line = event->backtrace()[k].line;
                                file = event->backtrace()[k].file;
                                break;
                            }
                        }
                        assert(line != -1);
                        for(int i = 0; i<_tree.depth(it) - rootdepth; ++i) 
                            log_file << "  ";
                        log_file << "vertex id:" << umg[i][j] << "|event id:" << event->event_idx() << " -- " << file << " " << line << endl;
                    }
                    log_file << endl;   
                }
            }
            ++it;
        }
        log_file << "-----" << endl;
    }

    void stack_tree::compact() {
        tree<shared_ptr<stack_tree_node>>::iterator it = _tree.begin();
        tree<shared_ptr<stack_tree_node>>::iterator end = _tree.end();
        vector<tree<shared_ptr<stack_tree_node>>::iterator> to_delete;
        while(++it != end) {
            if ((*it)->um_group.size() == 0) {
                to_delete.push_back(it);
            }

        }
        for (int i = 0; i < to_delete.size(); i++) {
                tree<shared_ptr<stack_tree_node>>::iterator victim = to_delete[i];
                _tree.reparent(_tree.parent(victim), victim);
                // delete *victim;
                _tree.erase(victim);
        }
    }

    tree<shared_ptr<stack_tree_node>>::iterator stack_tree::process_backtrace(const std::vector<stack_frame> &backtrace) {
        tree<shared_ptr<stack_tree_node>>::iterator it = _tree.begin();
        tree<shared_ptr<stack_tree_node>>::iterator res;
        assert(backtrace.size() > 0);
        for (int i = backtrace.size()-1; i >= 0; i--) {
            const stack_frame sf = backtrace[i];
            if (sf.file == "unknown") continue;
            string function = sf.function;
            int child_num = _tree.number_of_children(it);
            if (child_num == 0) {
                // if the current node has no children, we will add a new child
                // stack_tree_node* new_node = new stack_tree_node(function);
                shared_ptr<stack_tree_node> new_node = make_shared<stack_tree_node>(function);
                it = _tree.append_child(it, new_node);
                res = it;
                continue;
            } else {
                // if the current node has children, we will check if the function is already in the children
                bool found = false;
                for (int i = 0; i < child_num; i++) {
                    tree<shared_ptr<stack_tree_node>>::iterator child = _tree.child(it, i);
                    if ((*child)->function == function) {
                        // if the function is found in the children, we will move to the child
                        it = child;
                        found = true;
                        res = it;
                        break;
                    }
                }
                if (!found) {
                    // if the function is not found in the children, we will add a new child
                    // stack_tree_node* new_node = new stack_tree_node(function);
                    shared_ptr<stack_tree_node> new_node = make_shared<stack_tree_node>(function);
                    it = _tree.append_child(it, new_node);
                    res = it;
                    continue;
                }
            }
        }
        return res;
    }

    std::vector<tree<shared_ptr<stack_tree_node>>::iterator> stack_tree::leaves() {
        vector<tree<shared_ptr<stack_tree_node>>::iterator> res;
        tree<shared_ptr<stack_tree_node>>::leaf_iterator it = _tree.begin_leaf();
        tree<shared_ptr<stack_tree_node>>::leaf_iterator end = _tree.end_leaf();
        while (it != end) {
            res.push_back(it);
            ++it;
        }
        return res;
    }

    bool stack_tree::is_leaf(tree<shared_ptr<stack_tree_node>>::iterator it) {
        vector<tree<shared_ptr<stack_tree_node>>::iterator> leaves = this->leaves();
        for (int i = 0; i < leaves.size(); i++) {
            if (it == leaves[i]) return true;
        }
        return false;
    }

    vector<tree<shared_ptr<stack_tree_node>>::iterator> stack_tree::children(tree<shared_ptr<stack_tree_node>>::iterator it) {
        vector<tree<shared_ptr<stack_tree_node>>::iterator> res;
        int child_num = _tree.number_of_children(it);
        for (int i = 0; i < child_num; i++) {
            res.push_back(_tree.child(it, i));
        }
        return res;
    }

    void stack_tree::update_height() {
        // start from leafs
        vector<tree<shared_ptr<stack_tree_node>>::iterator> leaves = this->leaves();
        for (int i = 0; i < leaves.size(); i++) {
            this->update_height_helper(leaves[i], 0);
        }
    }

    void stack_tree::update_height_helper(tree<shared_ptr<stack_tree_node>>::iterator it, int height) {
        (*it)->height = height;
        if (it == this->root()) return;
        // get parent
        tree<shared_ptr<stack_tree_node>>::iterator parent = this->parent(it);
        update_height_helper(parent, height+1);
    }

    vector<tree<shared_ptr<stack_tree_node>>::iterator> stack_tree::get_nodes_at_height(int height) {
        vector<tree<shared_ptr<stack_tree_node>>::iterator> res;
        tree<shared_ptr<stack_tree_node>>::iterator it = _tree.begin();
        tree<shared_ptr<stack_tree_node>>::iterator end = _tree.end();
        while (it != end) {
            if ((*it)->height == -1) {
                cerr << "Warning: Height is not updated, please call update_height() first" << endl;
                exit(1);
            }
            if ((*it)->height == height) {
                res.push_back(it);
            }
            ++it;
        }
        return res;
    }

    stack_tree::~stack_tree() {
        // iterate over the tree and delete every node
        // tree<shared_ptr<stack_tree_node>>::iterator it = _tree.begin();
        // tree<shared_ptr<stack_tree_node>>::iterator end = _tree.end();
        // while(it != end) {
        //     delete *it;
        //     ++it;
        // }
    }
}