#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include "../utils/common.hpp"
#include "../trace/trace.hpp"
#include "type_info.hpp"

namespace pathfinder
{

class type_crawler {
public:

    typedef std::unordered_set<type_info, type_info::hash> type_info_set;

private:

    const llvm::Module &module_;
    const trace &trace_;

    std::unordered_map<
        std::string,
        std::unordered_map<uint64_t,
                           std::list<const llvm::Instruction*>
        >
    > dbg_info_mapping_;


    std::unordered_map<std::shared_ptr<trace_event>, type_info> type_mapping_;
    std::unordered_map<std::shared_ptr<trace_event>, type_info_set> type_associations_;
    std::unordered_set<const llvm::Type*> all_struct_types_;
    std::unordered_set<const llvm::Type*> all_types_;

    type_crawler() = delete;

    static const char *memory_intrinsics[];
    static const std::unordered_set<std::string> intrinsics;
    static bool is_memory_intrinsic(const std::string &f_name);

    std::list<const llvm::Value*> get_arg_operand(
        const stack_frame &current_frame,
        const stack_frame &previous_frame,
        int arg_num,
        bool is_prev_mem_intrinsic) const;

    std::list<const llvm::Value*> find_offending_stores(
        const stack_frame &sf) const;

    std::list<const llvm::Instruction*> get_store_instructions(
        const stack_frame &sf) const;

    std::unordered_set<const llvm::Value*> get_modified_pointers(
        const llvm::Instruction*) const;

    const llvm::Function* get_function(const stack_frame &sf) const;

    struct_info get_struct_info_in_frame(
        const stack_frame &sf,
        const std::list<const llvm::Value*> &ops,
        int &argnum,
        bool &is_mem_intrinsic) const;

    /**
     * Derive the type modified by the store operation.
     */
    struct_info get_modified_type(const trace_event &event) const;

    /**
     * Try the rest of the construction.
     * Assumes
     */
    uint64_t get_byte_offset(const struct_info &si) const;

    /**
     * Try to get the type info just based on the trace event alone.
     */
    type_info try_get_type_info(const trace_event &event) const;

    /**
     * Fill in missing types. Return number of events fixed.
     */
    size_t interpolate_missing_types(std::list<std::pair<std::shared_ptr<trace_event>, type_info>> &missing);

public:
    /**
     * Uses the trace to pre-build the type information
     */
    type_crawler(const llvm::Module &m, const trace &t);

    const type_info &at(std::shared_ptr<trace_event> event) const {
        return type_mapping_.at(event);
    }

    const type_info_set &all_types(std::shared_ptr<trace_event> event) const {
        return type_associations_.at(event);
    }

    const std::unordered_set<const llvm::Type*> &all_types(void) const {
        return all_types_;
    }

    const std::unordered_set<const llvm::Type*> &all_struct_types(void) const {
        return all_struct_types_;
    }
};

} // namespace pathfinder


