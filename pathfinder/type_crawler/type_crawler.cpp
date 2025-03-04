#include "type_crawler.hpp"

#include "llvm/IR/Module.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Operator.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/Support/raw_ostream.h"

#include <boost/core/demangle.hpp>
#include <boost/icl/interval_map.hpp>

#include <algorithm>
#include <memory>
#include <set>
#include <unordered_set>

using namespace std;
using namespace llvm;
namespace icl = boost::icl;
namespace core = boost::core;

namespace pathfinder {

/* type_crawler */

size_t type_crawler::interpolate_missing_types(list<pair<shared_ptr<trace_event>, type_info>> &missing){
    list<pair<shared_ptr<trace_event>, type_info>> new_missing;

    // Now, set up the interpolation tree.
    // - We match to a set of events so we can also filter on timestamp, i.e. by
    // the most recent type overwrite.
    typedef unordered_set<shared_ptr<trace_event>> event_set;
    icl::interval_map<uint64_t, event_set> interpolation_tree;
    for (const auto &p : type_mapping_) {
        shared_ptr<trace_event> te = p.first;
        const type_info &ti = p.second;

        BOOST_ASSERT(te->is_store());

        event_set es({te});
        // cerr << (ti.range(te).upper() - ti.range(te).lower()) << "\n";
        auto mapping = make_pair(ti.range(*te), es);
        interpolation_tree.add(mapping);
    }
    cerr << "interpolation size= " << interpolation_tree.size() << "\n";

    // Now, fill in the missing types.
    cerr << "missing=" << missing.size() << "/" << trace_.stores().size() << "\n";
    for (const auto &p : missing) {
        shared_ptr<trace_event> m = p.first;

        const auto it = interpolation_tree.find(m->range());
        if (it == interpolation_tree.end()) {
            // cerr << "ERR\n";
            // cerr << "\t" << m->str() << "\n";
            // cerr << "\t" << try_get_type_info(*m).str() << "\n";
            // exit(1);
            new_missing.push_back(p);
            continue;
        }

        const event_set &es = it->second;
        shared_ptr<trace_event> prior = *es.begin();
        if (es.size() != 1) {
            // Now see if they refer all to the same types.
            unordered_set<type_info, type_info::hash> types;
            for (shared_ptr<trace_event> prior : es) {
                types.insert(type_mapping_.at(prior));
            }

            // Pick the largest type.
            // This will likely be the largest structure.
            if (types.size() != 1) {
                // cerr << "too many unique types! " << types.size() << "\n";
                const type_info *largest = nullptr;
                for (shared_ptr<trace_event> p : es) {
                    const type_info &ti = type_mapping_.at(p);
                    // cerr << "\t" << ti.str() << "\n";
                    if (!largest || largest->type_size() < ti.type_size()) {
                        largest = &ti;
                        prior = p;
                    }
                }
            }
        }

        if (prior->range() == m->range()) {
            // Easy. We just set equal
            type_mapping_[m] = type_mapping_[prior];
        } else {
            // Find the match.
            type_info mod_type = type_mapping_[prior];
            mod_type.offset_in_type += (m->range().lower() - prior->range().lower());
            BOOST_ASSERT(mod_type.valid());
            type_mapping_[m] = mod_type;
            // cerr << type_mapping_[prior].str() << "\n";
            // cerr << type_mapping_[m].str() << "\n";
            // exit(1);
        }
    }

    size_t fixed = missing.size() - new_missing.size();
    missing = new_missing;

    if (fixed) return fixed;

    /**
     * @brief
     * If we don't find any way to fix this, now we employ a heuristic.
     *
     * Assign relative offsets.
     *
     * Maybe something else later, too.
     */

    unordered_map<const Type *, uint64_t> rel_offset;
    for (const auto &p : missing) {
        std::shared_ptr<trace_event> m = p.first;
        const type_info &i = p.second;
        if (!rel_offset.count(i.type)) {
            rel_offset[i.type] = m->range().lower();
        } else {
            rel_offset[i.type] = rel_offset[i.type] > m->range().lower() ? rel_offset[i.type] : m->range().lower();
        }
    }

    for (const auto &p : missing) {
        std::shared_ptr<trace_event> m = p.first;
        type_info i = p.second;
        i.offset_in_type = (m->range().lower() - rel_offset[i.type])
            % i.type_size();
        cout << "\ti=" << i.str() << "\n";
        BOOST_ASSERT(!i.invalid_offset());
        BOOST_ASSERT(!i.invalid_size());
        BOOST_ASSERT(nullptr != i.type);
        BOOST_ASSERT(!i.needs_interpolation());
        BOOST_ASSERT(i.valid());
        type_mapping_[m] = i;
    }

    fixed = missing.size();
    missing.clear();
    return fixed;
}

type_crawler::type_crawler(const llvm::Module &m, const trace &t)
    : module_(m), trace_(t), dbg_info_mapping_() {

    // Set up debug symbol mappings
    for (const Function &f : m) {
        string f_name = string(core::demangle(f.getName().str().c_str()));
        // if f_name contains () at the end, which means it included argument, remove it, but keep other ()
        if (f_name.rfind(")") == f_name.size() - 1) {
            size_t pos = f_name.rfind("(");
            if (pos != string::npos) {
                f_name = f_name.substr(0, pos);
            }
        }
        for (const BasicBlock &bb : f) {
            for (const Instruction &i : bb) {
                if (!i.getMetadata("dbg")) continue;
                if (DILocation *di = dyn_cast<DILocation>(i.getMetadata("dbg"))) {
                    uint64_t line = di->getLine();

                    /* Don't need file name, function name has to be unique */
                    // DILocalScope *ls = di->getScope();
                    // DIFile *df = ls->getFile();
                    // li.file = df->getFilename();
                    if (!dbg_info_mapping_.count(f_name)) {
                        dbg_info_mapping_[f_name] =
                            unordered_map<uint64_t, list<const Instruction*>>();
                        // print if f_name start with "leveldb::(anonymous namespace)::PosixMmapFile::Append"
                        if (f_name.find("leveldb::(anonymous namespace)::PosixMmapFile::Append") != string::npos) {
                            llvm::errs() << "f_name: " << f_name << "\n";
                        }
                    }
                    if (!dbg_info_mapping_[f_name].count(line)) {
                        dbg_info_mapping_[f_name][line] = list<const Instruction*>();
                    }
                    dbg_info_mapping_[f_name][line].push_back(&i);
                }
            }
        }
    }

    /* Now, get all the initial types. Do in parallel. */
    // Do naively.
    list<pair<shared_ptr<trace_event>, type_info>> missing;
    for (shared_ptr<trace_event> store : trace_.stores()) {
        type_info ti = try_get_type_info(*store);
        if (ti.needs_interpolation()) {
            missing.push_back(make_pair(store, ti));
        } else {
            // Otherwise, add it to the mapping.
            type_mapping_[store] = ti;
        }
    }

    // DEBUG: display number of instances after initial pass
    // unordered_map<const Type *, unordered_set<uintptr_t>> ninstance;
    // for (const auto &p : type_mapping_) {
    //     const Type *ty = p.second.type;
    //     ninstance[ty].insert(p.second.range(*p.first).lower());
    // }

    // for (const auto &p : ninstance) {
    //     llvm::outs() << *p.first << "\n";
    //     llvm::outs() << "\t" << p.second.size() << " instances!\n";
    // }

    // cerr << "FINISH DEBUGGING ME\n";
    // exit(EXIT_FAILURE);

    // Finally, fill in the type mappings.
    while (!missing.empty()) {
        size_t fixed = interpolate_missing_types(missing);
        if (!fixed) {
            cerr << "didn't fix anything!\n";
            exit(1);
        }
        cerr << "fixed " << fixed << "\n";
    }

    // Now, we get the entire set of types.
    typedef std::set<shared_ptr<trace_event>> event_set;
    icl::interval_map<uint64_t, event_set> interpolation_tree;
    for (const auto &p : type_mapping_) {
        std::shared_ptr<trace_event> te = p.first;
        const type_info &ti = p.second;

        if (!te->is_store()) continue;

        event_set es({te});
        // cerr << (ti.range(te).upper() - ti.range(te).lower()) << "\n";
        auto mapping = make_pair(ti.range(*te), es);
        interpolation_tree.add(mapping);
        BOOST_ASSERT(interpolation_tree.find(te->range()) != interpolation_tree.end());
    }

    // Now, construct all type associations
    for (const auto &p : type_mapping_) {
        std::shared_ptr<trace_event> te = p.first;
        const type_info &ti = p.second;

        if (nullptr == ti.type) {
            llvm::errs() << "wut " << __FUNCTION__ << ":" << __LINE__ << "\n";
            exit(EXIT_FAILURE);
        }

        // Keep all struct types up to date.
        all_types_.insert(ti.type);
        if (nullptr != ti.type &&
                (isa<StructType>(ti.type) || isa<ArrayType>(ti.type) || isa<VectorType>(ti.type))
            ) {
            all_struct_types_.insert(ti.type);
        }

        if (!te->is_store()) continue;

        const auto it = interpolation_tree.find(te->range());
        if (it == interpolation_tree.end()) {
            cerr << "ERR: could not find anything in the interpolation tree\n";
            exit(1);
        }

        type_associations_[te] = type_info_set();
        // cerr << "overlap: " << it->first << " " << it->second.size() << "\n";

        for (shared_ptr<trace_event> other : it->second) {
            // Compute the offset_in_type
            type_info other_ti = type_mapping_[other];
            auto other_type_range = other_ti.range(*other);

            // If this store is not within the other type, skip.
            if (!(other_type_range.lower() <= te->range().lower() &&
                  other_type_range.upper() >= te->range().upper())) {
                // cerr << "ack! doesn't fit\n";
                // cerr << "\t" << other_type_range << "(" << other_ti.str() << ")\n";
                // cerr << "\t" << te->range() << "\n";
                // exit(1);
                continue;
            }

            // cerr << other_type_range << " {" << other_ti.type_size << "} " << other->range() << "\n";
            // cerr << "\t" << te->range() << "\n";

            // llvm::errs() << "before check 1 other_ti: type=" << *other_ti.type << ", offset="
            //     << other_ti.offset_in_type << "\n";
            // llvm::errs() << "\t" << other_ti.str() << "\n";

            // BOOST_ASSERT(other_ti.valid());
            // llvm::errs() << "before mod other_ti: type=" << *other_ti.type << ", offset="
            //     << other_ti.offset_in_type << "\n";

            uint64_t type_start = other_type_range.lower();
            other_ti.offset_in_type = te->range().lower() - type_start;
            if (other_ti.type_size() > 0) {
                other_ti.offset_in_type %= other_ti.type_size();
            }
            // llvm::errs() << "\ttype_start=" << type_start << ", range_lower="
            //     << te->range().lower() << "\n";

            // llvm::errs() << "before check 2 other_ti: type=" << *other_ti.type << ", offset="
            //     << other_ti.offset_in_type << "\n";

            BOOST_ASSERT(!other_ti.needs_interpolation());
            BOOST_ASSERT(other_ti.valid());

            type_associations_[te].insert(other_ti);
        }
    }

    // llvm::outs() << "N types= " << all_struct_types_.size() << "\n";
    // for (const Type *ty : all_struct_types_) {
    //     llvm::outs() << *ty << "\n";
    // }

    // exit(1);
}

const char *type_crawler::memory_intrinsics[] = {
    // memcpy implementations
    "__memcpy_avx_unaligned_erms",
    "__memset_avx2_unaligned_erms",
    "__memset_avx2_erms",
    // strcpy implementations
    "__strcpy_avx2",
    // strncpy implementations
    "__strncpy_avx2",
    "__strncpy_evex",
    // printf garbage,
    "_IO_default_xsputn",
    "__vfprintf_internal",
    "__vsnprintf_internal",
    "snprintf",
    // PMDK garbage,
    "memmove_movnt_sse2_clflush",
    "memmove_mov_sse2_clflush",
    "memmove_mov_sse2_noflush",
    "memmove_nodrain_sse2_clflush",
    "memset_mov_sse2_clflush",
    "memset_movnt_sse2_clflush",
    "memset_nodrain_sse2_clflush",
    "memmove_movnt_avx_clflush_wcbarrier",
    // pthread garbage
    "pthread_mutex_init",
    "pthread_mutex_destroy",
    "pthread_mutex_lock",
    "pthread_mutex_unlock",
    // Intel garbage
    "_mm_stream_si64",
    // LMDB garbage
    "__memset_evex_unaligned_erms",
    "__memmove_evex_unaligned_erms",
    "__pthread_mutex_init",
    "__pthread_mutex_lock_full",
    "__pthread_mutex_unlock_full",
    "__pthread_mutex_lock",
    "__pthread_mutex_unlock",
    "__memmove_avx512_unaligned_erms",
};
const unordered_set<string> type_crawler::intrinsics(
    std::begin(type_crawler::memory_intrinsics),
    std::end(type_crawler::memory_intrinsics));

bool type_crawler::is_memory_intrinsic(const std::string &f_name) {
    return !!intrinsics.count(f_name);
}

static bool is_llvm_memop(const Instruction *i) {
    if (const CallBase *cb = dyn_cast<CallBase>(i)) {
        const Value *v = cb->getCalledOperand();
        StringRef name = v->getName();
        return name.startswith("llvm.mem");
    }

    return false;
}

static unordered_set<string> vector_ops{
    "_mm_loadu_si128(long long __vector(2) const*)"
};

static bool is_vector_memop(const Instruction *i) {
    if (const CallBase *cb = dyn_cast<CallBase>(i)) {
        const Value *v = cb->getCalledOperand();
        string name = string(core::demangle(v->getName().str().c_str()));
        return vector_ops.count(name) != 0;
    }

    return false;
}

list<const Value*> type_crawler::get_arg_operand(
        const stack_frame &current_frame,
        const stack_frame &previous_frame,
        int arg_num,
        bool is_prev_mem_intrinsic) const {

    list<const Value*> args;

    const list<const Instruction*> &insts = dbg_info_mapping_
        .at(current_frame.function).at(current_frame.line);

    auto inst_cout = [&, this] (const char *fn, uint64_t line) {
        return dbg_info_mapping_.at(fn).at(line).size();
    };

    // cerr << "\t>>> " << insts.size() << "\n";
    unordered_set<const CallBase*> calls;
    // Find the call instruction.
    for (const Instruction *i : insts) {
        if (const CallBase *cb = dyn_cast<CallBase>(i)) {
            const Value *called_val = cb->getCalledOperand();
            if (const Function *fn = dyn_cast<Function>(called_val)) {
                if (fn->getName().str() == previous_frame.function ||
                    is_prev_mem_intrinsic) {
                    // llvm::errs() << *cb << "\n";
                    calls.insert(cb);
                }
            } else if (isa<LoadInst>(called_val)) {
                // Probably a function pointer
                // llvm::errs() << *cb << "\n";
                calls.insert(cb);
            }
        }
    }

#if 0
    const CallBase *cb = nullptr;

    if (calls.size() > 1) {
        for (const CallBase *candidate : calls) {
            if (is_llvm_memop(candidate) || is_vector_memop(candidate)) {
                cb = candidate;
            }
        }
    } else if (calls.size() == 1) {
        cb = *calls.begin();
    }

    if (__glibc_unlikely(!cb)) {
        cerr << "Could not find a suitable callback!\n";
        llvm::errs() << "Current Function:\n" << *get_function(current_frame) << "\n";
        llvm::errs() << "Previous function:\n" << *get_function(previous_frame) << "\n";
        cerr << "\tStarted with " << calls.size() << " call instructions!\n";
        for (const CallBase *cbi : calls) {
            llvm::errs() << "\t\t" << *cbi << "\n";
        }
        exit(EXIT_FAILURE);
    }

    const Value *cv = cb->getArgOperand(arg_num);
    const Instruction *ci = dyn_cast<Instruction>(cv);
    BOOST_ASSERT(ci && "not an instruction!");
    args.push_back(ci);
#endif

    for (const CallBase *cb : calls) {
        const Value *cv = cb->getArgOperand(arg_num);
        if (const Instruction *ci = dyn_cast<Instruction>(cv)) {
            args.push_back(ci);
        }
    }

    return args;
}

// Having these as static variables in a function causes a segfault on shutdown.
static unordered_set<unsigned> op_0 = {
    Instruction::Add, Instruction::ExtractElement,
    Instruction::InsertElement, Instruction::GetElementPtr,
    Instruction::AtomicCmpXchg, Instruction::AtomicRMW
};

static unordered_set<unsigned> op_1 = {
    Instruction::Store
};

/**
 * Returns the next pointer in the def-use chain, or none if we can't continue.
 */
static list<const Value*> get_pointer_operands(const Value *val) {
    list<const Value*> ret_ops;

    vector<const Value*> ops;
    const User *u = dyn_cast<User>(val);
    if (__glibc_unlikely(!u)) {
        cerr << "Must be a user!\n";
        llvm::errs() << *val << "\n";
        exit(EXIT_FAILURE);
    }

    for (const Value *op : u->operands()) {
        if (const Instruction *opi = dyn_cast<Instruction>(op)) {
            ops.push_back(opi);
        } else if (const GEPOperator *gep = dyn_cast<GEPOperator>(op)) {
            ops.push_back(gep);
        } else if (isa<GlobalValue>(op)) {
            ops.push_back(op);
        } else if (!isa<Constant>(op) && !isa<InlineAsm>(op)) {
            cerr << "don't know what to do!\n";
            llvm::errs() << *op << "\n";
            llvm::errs() << *op->getType() << "\n";
            llvm::errs() << isa<Function>(op) << "\n";
            llvm::errs() << *val << "\n";
            exit(1);
        }
    }

    const Instruction *i = dyn_cast<Instruction>(val);

    if (i && (op_0.count(i->getOpcode()) || is_llvm_memop(i)) ) {
        ret_ops.push_back(ops[0]);
    } else if (ops.size() == 1) {
        ret_ops.push_back(ops[0]);
    } else if (i && op_1.count(i->getOpcode())) {
        ret_ops.push_back(ops[1]);
    } else if (i && i->getOpcode() == Instruction::PHI) {
        ret_ops.insert(ret_ops.end(), ops.begin(), ops.end());
    } else if (isa<CallBase>(val)) {
        // Do backtracing later.
        ret_ops.push_back(val);
    } else if (isa<GEPOperator>(val)) {
        ret_ops.push_back(u->getOperand(0));
    } else {
        // llvm::errs() << __FUNCTION__ << " backup for " << *i << "\n";
        ret_ops.insert(ret_ops.end(), ops.begin(), ops.end());
        // cerr << "Don't know what to do!" << endl;
        // llvm::errs() << "\t" << *val << "\n";
        // for (const auto *op : ops) {
        //     llvm::errs() << "\tFOUND OP: " << *op << "\n";
        // }
        // for (const Value *op : u->operands()) {
        //     llvm::errs() << "\tU OPS: " << *op << "\n";
        //     llvm::errs() << "\t\t" << isa<Constant>(op) << "\n";
        //     llvm::errs() << "\t\t" << isa<GetElementPtrInst>(op) << "\n";
        //     llvm::errs() << "\t\t" << isa<GEPOperator>(op) << "\n";
        // }
        // exit(EXIT_FAILURE);
    }

    return ret_ops;
}

/**
 * Values are usually loaded from an stack allocation (alloca).
 * With no optimizations, this is extremely true.
 */
static const AllocaInst *get_alloca_from_value(const Value *val) {
    if (isa<Constant>(val)) {
        return nullptr;
    }

    if (const AllocaInst *ai = dyn_cast<AllocaInst>(val)) {
        return ai;
    }

    const Instruction *i = dyn_cast<Instruction>(val);
    if (!i) {
        cerr << "don't know how to continue!\n";
        exit(1);
    }

    list<const Value *> operands = get_pointer_operands(i);
    unordered_set<const Value *> visited;
    unordered_set<const AllocaInst *> allocas;

    while (!operands.empty()) {
        const Value *operand = operands.front();
        operands.pop_front();
        if (visited.count(operand)) {
            continue;
        }
        visited.insert(operand);

        if (const AllocaInst *ai = dyn_cast<AllocaInst>(operand)) {
            allocas.insert(ai);
        } else if (const CallBase *cb = dyn_cast<CallBase>(operand)) {
            // /*
            // Backtracing.
            // 1. Get the function
            // */
            // const Function *f = cb->getCalledFunction();
            // if (!f) {
            //     llvm::errs() << "cb doesn't call a function! cb = " << *cb << "\n";
            //     /* We can also get this from */
            //     llvm::errs() << "original value: " << *val << "\n";
            // }
            // BOOST_ASSERT(f);
            // // 2. Get all the return instructions in said function.
            // list <const ReturnInst *> ret_insts;
            // for (const BasicBlock &b : *f) {
            //     for (const Instruction &i : b) {
            //         if (const ReturnInst *ri = dyn_cast<ReturnInst>(&i)) {
            //             ret_insts.push_back(ri);
            //         }
            //     }
            // }
            // // 3. Add the return instructions to the operand list and let
            // // looping do the rest of the work!
            // operands.insert(operands.end(), ret_insts.begin(), ret_insts.end());

            // iangneal: we really don't have enough info and we're not guaranteed to get it.
            // Let's just continue here and see if we have anything better.
            // continue;

            // llvm::errs() << "get_alloca_from_value: cb = " << *cb << "\n";
            // llvm::errs() << *cb->getFunction() << "\n";

            operands.insert(operands.end(), cb->user_begin(), cb->user_end());
        } else if (!isa<GlobalValue>(operand)) {
            list<const Value *> o = get_pointer_operands(operand);
            operands.insert(operands.end(), o.begin(), o.end());
        }
    }

    if (allocas.empty()) {
        llvm::errs() << "get_alloca_from_value: no allocas from value = " << *val << "\n";
        return nullptr;
    }

    if (allocas.size() > 1) {
        cerr << "too many allocas!\n";
        exit(1);
    }

    return *allocas.begin();
}

const llvm::Function *type_crawler::get_function(const stack_frame &sf) const {
    return dbg_info_mapping_.at(sf.function).at(sf.line).front()->getFunction();
}

list<const Instruction*> type_crawler::get_store_instructions(const stack_frame &sf) const {
    list<const Instruction*> stores;
    // cerr << "fn=" << sf.function << " line=" << sf.line << endl;
    const list<const Instruction*> &insts = dbg_info_mapping_.at(sf.function).at(sf.line);

    for (const Instruction *i : insts) {
        unsigned opcode = i->getOpcode();
        if (opcode == Instruction::Store ||
            opcode == Instruction::AtomicCmpXchg ||
            opcode == Instruction::AtomicRMW ||
            is_llvm_memop(i) ||
            is_vector_memop(i)) {
            stores.push_back(i);
        }
    }

    return stores;
}


unordered_set<const Value*> type_crawler::get_modified_pointers(const Instruction* store) const {
    unordered_set<const Value*> ptrs;
    /*
    Iterate until we get to a non-instruction, I suppose?
    But what about allocation function calls? Well, those should
    still be stored into allocas and such.
    */
    list<const Value*> frontier{store};
    unordered_set<const Value*> traversed;
    while (!frontier.empty()) {
        const Value *curr = frontier.front();
        frontier.pop_front();

        if (const AllocaInst *ai = dyn_cast<AllocaInst>(curr)) {
            ptrs.insert(curr);
        } else if (const Argument *arg = dyn_cast<Argument>(curr)) {
            if (isa<PointerType>(arg->getType())) {
                ptrs.insert(curr);
            }
        } else if (const GlobalValue *val = dyn_cast<GlobalValue>(curr)) {
            if (isa<PointerType>(val->getType())) {
                ptrs.insert(curr);
            }
        } else if (const CallBase *cb = dyn_cast<CallBase>(curr)) {
            ptrs.insert(curr);
        } else {
            list<const Value*> ops = get_pointer_operands(curr);
            if (ops.empty()) {
                llvm::errs() << "No pointer operands for " << *curr << "\n";
                exit(EXIT_FAILURE);
            }

            for(const Value* v : ops) {
                if (!traversed.count(v)) {
                    traversed.insert(v);
                    frontier.push_back(v);
                }
            }
        }
    }

    return ptrs;
}


list<const Value*> type_crawler::find_offending_stores(const stack_frame &sf) const {
    vector<const Instruction*> stores;
    // cerr << "fn=" << sf.function << " line=" << sf.line << endl;
    const list<const Instruction*> &insts = dbg_info_mapping_.at(sf.function).at(sf.line);

    for (const Instruction *i : insts) {
        unsigned opcode = i->getOpcode();
        if (opcode == Instruction::Store ||
            opcode == Instruction::AtomicCmpXchg ||
            opcode == Instruction::AtomicRMW ||
            is_llvm_memop(i) ||
            is_vector_memop(i)) {
            stores.push_back(i);
        }
    }

    if (stores.size() == 1) {
        // cerr << "one store!\n";
        return get_pointer_operands(stores.front());
    }

    /*
    We have multiple stores, but in all likelihood, they're part of
    the same operation (i.e., it's doing something dumb like storing and
    reloading from the same stack variables)

    It turns out this is not always the case. So, what we should do is
    filter down as best we can to find the set of base stores.
    */

    // Dependent stores
    unordered_set<const Instruction*> dep_stores;
    for (auto it = stores.begin(); it != stores.end(); ++it) {
        unordered_set<const Instruction*> tmp_stores((it + 1), stores.end());
        // If it's used by a previous store, that's a bit weird.
        BOOST_ASSERT(!tmp_stores.count(nullptr));

        list<const User*> users;

        list<const Value *> ptr_ops = get_pointer_operands(*it);
        for (const Value *ptr_op : ptr_ops) {
            const Value *ptr_src = get_alloca_from_value(ptr_op);
            if (!ptr_src) continue;

            users.insert(users.end(),
                ptr_src->user_begin(), ptr_src->user_end());
        }

        unordered_set<const User*> visited;
        while (!users.empty()) {
            const User *u = users.front();
            users.pop_front();
            if (visited.count(u)) {
                continue;
            }
            visited.insert(u);

            const Instruction *ui = dyn_cast<Instruction>(u);
            BOOST_ASSERT(ui);

            if (tmp_stores.count(ui)) {
                dep_stores.insert(ui);
            } else {
                users.insert(users.end(), ui->user_begin(), ui->user_end());
            }
        }
    }

    list<const Value*> ptr_ops;
    for (const Instruction *store : stores) {
        if (dep_stores.count(store)) continue;

        list<const Value*> p = get_pointer_operands(store);
        ptr_ops.insert(ptr_ops.end(), p.begin(), p.end());
    }

    return ptr_ops;
}

/**
 * "Dereferencing" to get the base type of a pointer type.
 */
static const Type *get_base_type(const Type *ty) {
    if (const PointerType *pt = dyn_cast<PointerType>(ty)) {
        return pt->getElementType();
    }

    return ty;
}

static struct_info get_struct_info(const Value *v) {
    list<const Value *> ops{v};
    unordered_set<const Value *> visited;
    static uint64_t succ = 0;
    succ++;
    while (ops.size()) {
        const Value *op = ops.front();
        ops.pop_front();

        if (!isa<Instruction>(op) && !isa<GEPOperator>(op)) continue;

        if (isa<Instruction>(op) && isa<GetElementPtrInst>(op) &&
                (
                    isa<StructType>(dyn_cast<GetElementPtrInst>(op)->getSourceElementType()) ||
                    isa<VectorType>(dyn_cast<GetElementPtrInst>(op)->getSourceElementType()) ||
                    isa<ArrayType>(dyn_cast<GetElementPtrInst>(op)->getSourceElementType())
                )
            ) {
            // Parse getelementptr
            const GetElementPtrInst *gep = dyn_cast<GetElementPtrInst>(op);
            /*
            In theory, there can be a whole bunch of arguments on the end of
            this instruction, so we can just append them all.

            https://llvm.org/docs/GetElementPtr.html
            - The first argument indexes the first pointer.
            - Subsequent args index into the type we want
            - If there are no indices, then it's zero. We should return a constant
                0 to make our lives easier
            */
            struct_info si(gep->getSourceElementType(), op);
            // llvm::errs() << *gep << "\n" << *gep->getSourceElementType() << "\n";
            for (const Value *index : gep->indices()) {
                si.offset_values.push_back(index);
            }
            // cerr << "SUCC = " << succ << "\n";
            return si;

        } else if (isa<GEPOperator>(op)) {
            // Parse GEPOperator
            const GEPOperator *gep_op = dyn_cast<GEPOperator>(op);
            /*
            In theory, there can be a whole bunch of arguments on the end of
            this instruction, so we can just append them all.

            https://llvm.org/docs/GetElementPtr.html
            - The first argument indexes the first pointer.
            - Subsequent args index into the type we want
            - If there are no indices, then it's zero. We should return a constant
                0 to make our lives easier
            */
            // struct_info si(gep_op->getSourceElementType(), op);
            // llvm::errs() << "Unhandled GEP op! " << *op << "\n";
            // llvm::errs() << "\t" << *gep_op->getSourceElementType() << "\n";
            // for (auto it = gep_op->idx_begin(); it != gep_op->idx_end(); ++it) {
            //     llvm::errs() << **it << "\n";
            // }
            // cerr << "GEP op EXIT\n";
            // exit(EXIT_FAILURE);
            // llvm::errs() << "Crawling up GEP ops " << *gep_op << "\n";
            const Value *gep_ptr = gep_op->getPointerOperand();
            if (!visited.count(gep_ptr)) {
                visited.insert(gep_ptr);
                ops.push_back(gep_ptr);
            }
        } else if (isa<BitCastInst>(op) &&
                   isa<StructType>(get_base_type(dyn_cast<BitCastInst>(op)->getSrcTy()))) {

            const BitCastInst *bi = dyn_cast<BitCastInst>(op);

            struct_info si(
                get_base_type(bi->getSrcTy()),
                bi->getOperand(0));
            // Now we need offsets. Find in the visited operands
            // llvm::errs() << "bitcast: " << *bi << "\n";
            // for (const Value *v : visited) {
            //     llvm::errs() << "\tbitcast visited: " << *v << "\n";
            // }
            // exit(EXIT_FAILURE);

            return si;
        } else if (const User *u = dyn_cast<User>(op)){
            for (const Value *op : u->operands()) {
                if (!visited.count(op)) {
                    visited.insert(op);
                    ops.push_back(op);
                }
            }
        }
    }

    // cerr << "get_struct_info - exit with nothing\n";
    return struct_info();
}

/**
 * Given an alloca, find what function argument is stored in it, if any.
 * This will crawl alloca chains too.
 */
static const Argument *get_argument_from_alloca(const AllocaInst *a) {
    list<const Value *> alloca_users;
    unordered_set<const AllocaInst*> checked_allocas;
    alloca_users.insert(alloca_users.end(), a->user_begin(), a->user_end());

    cerr << "enter get_argument_from_alloca\n";

    while (!alloca_users.empty()) {
        const Value *v = alloca_users.front();
        alloca_users.pop_front();

        const StoreInst *si = dyn_cast<StoreInst>(v);
        if (!si) continue;

        // arg is usually stored into the alloca
        const Value *arg_val = si->getValueOperand();
        if (const Argument *a = dyn_cast<Argument>(arg_val)) {
            llvm::errs() << "exit get_argument_from_alloca with arg " << *a << "\n";
            return a;
        } else if (!isa<Instruction>(arg_val) || isa<CallBase>(arg_val)) {
            continue;
        }

        llvm::errs() << "\tget_argument_from_alloca: calling get_alloca_from_value with arg_val = " << *arg_val << "\n";
        const AllocaInst *ai = get_alloca_from_value(arg_val);
        if (ai && !checked_allocas.count(ai)) {
            checked_allocas.insert(ai);
            alloca_users.insert(alloca_users.end(), ai->user_begin(), ai->user_end());
        }
    }

    cerr << "exit get_argument_from_alloca with null\n";
    return nullptr;
}

/**
 * Given an alloca, find what values are possibly stored into this alloca.
 *
 * iangneal:
 * This may be overly broad, i.e. might include typecasts that aren't on the
 * right control-flow path, but we will take a "context-insensitive" approach
 * and consider it okay.
 */
static list<const Value *> get_alloca_stored_values(const AllocaInst *a) {
    list<const Value *> stored_values;

    // cerr << "enter " << __FUNCTION__ << "\n";

    for (const Value *u : a->users()) {
        const StoreInst *si = dyn_cast<StoreInst>(u);
        if (!si) continue;

        // llvm::errs() << "\t" << *si->getValueOperand() << " was stored into alloca " << *a << "\n";

        stored_values.push_back(si->getValueOperand());
    }

    // cerr << "exit " << __FUNCTION__ << "\n";
    return stored_values;
}

struct_info type_crawler::get_struct_info_in_frame(
    const stack_frame &sf, const list<const Value*> &ops, int &argnum, bool &is_mem_intrinsic) const {

    // cerr << "get_struct_info_in_frame: " << sf.str() << "\n";

    /* Crawl through pointer ops. */
    if (is_memory_intrinsic(sf.function)) {
        is_mem_intrinsic = true;
        argnum = 0;
        return struct_info();
    } else {
        if (ops.empty()) {
            cerr << "get_struct_info_in_frame: ops empty! " << sf.str() << "\n";
            llvm::errs() << *get_function(sf) << "\n";
        }
        BOOST_ASSERT(ops.size());
        argnum = -1;
        is_mem_intrinsic = false;
    }

    unordered_set<const Value*> visited;
    list<const Value*> frontier = ops;
    while (!frontier.empty()) {
        const Value *v = frontier.front();
        frontier.pop_front();
        if (visited.count(v)) {
            continue;
        }
        visited.insert(v);

        struct_info si = get_struct_info(v);

        if (si.is_valid()) {
            // if (isa<StructType>(si.type)) {
            //     // Why not handle empty?
            //     // BOOST_ASSERT(si.offset_values.size() && "Cannot handle empty!");
            //     // cerr << "Exit get_modified_type with si1\n";
            //     return si;
            // } else if (isa<VectorType>(si.type) || isa<ArrayType>(si.type)) {
            //     /*
            //     Here we try again, and try to find the struct this array is in.
            //     TODO: handle nested?
            //     */
            //     list<const Value *> ops = get_pointer_operands(si.value);
            //     BOOST_ASSERT(ops.size() == 1);
            //     llvm::errs() << "HMMMMMMMM\n";
            //     struct_info si2 = get_struct_info(ops.front());
            //     if (!si2.is_valid() || !isa<StructType>(si2.type)) {
            //         llvm::errs() << "si2 error\n";
            //         llvm::errs() << "si.value = " << *si.value << "\n";
            //         llvm::errs() << "ops.front() = " << *ops.front() << "\n";
            //         if (si2.type) llvm::errs() << "Type: " << si2.type << "\n";
            //         else llvm::errs() << "Type: null\n";
            //     }
            //     BOOST_ASSERT(si2.is_valid() && isa<StructType>(si2.type));
            //     BOOST_ASSERT(si2.offset_values.size() && "Cannot handle empty vector!");
            //     // cerr << "Exit get_modified_type with si2\n";
            //     return si2;
            if (isa<StructType>(si.type)
                || isa<VectorType>(si.type)
                || isa<ArrayType>(si.type)) {
                return si;
            } else {
                cerr << "Wut\n";
                llvm::errs() << *v << "\n";
                llvm::errs() << "si.type = " << *si.type << "\n";
                exit(EXIT_FAILURE);
            }
        } else {
            if (const auto *arg = dyn_cast<Argument>(v)) {
                int new_argnum = arg->getArgNo();
                if (argnum != -1 && argnum != new_argnum) {
                    cerr << "argnum already set\n";
                    exit(EXIT_FAILURE);
                }
                argnum = new_argnum;
            } else if (const AllocaInst *ai = dyn_cast<AllocaInst>(v)) {
                // llvm::errs() << "get_struct_info_in_frame, I have ai = " << *ai << "\n";
                // const Argument *arg = get_argument_from_alloca(ai);
                // // cerr << "yup it me, arg is " << arg << "\n";
                // // if (arg) {
                // //     llvm::errs() << "\t" << *arg << "\n\t\tfrom: " << *v << "\n";
                // // }
                // if (arg && arg->getParent()->getName().str() == sf.function) {
                //     frontier.push_back(arg);
                // }

                const list<const Value *> stored = get_alloca_stored_values(ai);
                frontier.insert(frontier.end(), stored.begin(), stored.end());
            } else if (isa<Instruction>(v)) {
                for (const Value *newop : get_pointer_operands(v)) {
                    frontier.push_back(newop);
                }
            }
        }
    }

    /* Means that it's not from an arg, so we'd just return the type. */
    if (argnum == -1) {
        unordered_set<struct_info, struct_info::hash> info_set;
        for (const Value *v : ops) {
            info_set.insert(get_struct_info(v));
        }
        if (info_set.size() != 1) {
            cerr << "info set is of size " << info_set.size()
                << ", ops of size " << ops.size() << "\n";
            for (const struct_info &s : info_set) {
                llvm::errs() << "\t" << *s.type << " " << *s.value << "\n";
            }
        }
        BOOST_ASSERT(info_set.size() == 1);
        return *info_set.begin();
    }

    return struct_info();
}

struct_info type_crawler::get_modified_type(const trace_event &event) const {
    /**
     * This is the workhorse of the function. We will crawl through the
     * stack trace and try to figure out the original structure that is
     * being modified. We stop when we get to the allocation site.
     */

    if (event.stack.empty()) {
        return struct_info();
    }

    int arg_num = -1;
    bool mem_intrinsic = false;

    static uint64_t nops = 0;

    // cerr << "get_modified_type: " << event.str() << "\n";

    /*
    First, let's see if we can find a type in the lowest level of the callstack.
    */

    list<const Value*> ops;

    const auto &bottom_sf = event.stack.at(0);

    // cerr << "get_modified_type: bottom_sf = " << bottom_sf.str() << "\n";

    if (!is_memory_intrinsic(bottom_sf.function)) {
        const auto stores = get_store_instructions(bottom_sf);
        // for (const Instruction *i : stores) {
        //     llvm::errs() << "\toffending store " << *i << "\n";
        // }

        for (const Instruction *i : stores) {
            // llvm::errs() << "\tDoes this store modify pointers? " << *i << "\n";
            unordered_set<const Value *> mod = get_modified_pointers(i);
            if (!mod.empty()) {
                // llvm::errs() << "\tyes!\n";
                // for (const Value *m : mod) {
                //     llvm::errs() << "\t\tmodifies " << *m << "\n";
                // }
                const auto ptrops = get_pointer_operands(i);
                ops.insert(ops.end(), ptrops.begin(), ptrops.end());
                // ops.insert(ops.end(), mod.begin(), mod.end());
            }
            // else { llvm::errs() << "\tno...\n"; }
        }

        if (ops.empty()) {
            cerr << "No ops in lowest level function!\n";
            llvm::errs() << *get_function(bottom_sf) << "\n";
            cerr << "\t" << bottom_sf.str() << "\n";
            cerr << "is_memory_intrinsic = " << is_memory_intrinsic(bottom_sf.function) << "\n";

            for (const Instruction *i : stores) {
                llvm::errs() << "\tstore: " << *i << "\n";

                for (const auto *ptr : get_modified_pointers(i)) {
                    llvm::errs() << "\t\tmod: " << *ptr << "\n";
                }
            }

            exit(EXIT_FAILURE);
        }
    }

    struct_info si = get_struct_info_in_frame(bottom_sf, ops, arg_num, mem_intrinsic);
    if (si.is_valid()) {
        return si;
    } else if (arg_num == -1) {
        BOOST_ASSERT(ops.size() == 1);
        const Value *val = ops.front();
        Type *ty = val->getType();
        return struct_info(ty, val);
    }

    // if (!mem_intrinsic) llvm::errs() << *get_function(bottom_sf) << "\n";

    /* Now, go up the stack. */

    for (int frame_idx = 1; frame_idx < event.stack.size(); ++frame_idx) {
        // cerr << "\nloop on frame_idx = " << frame_idx << "\n";

        const stack_frame &frame = event.stack.at(frame_idx);
        const stack_frame &prior_frame = event.stack.at(frame_idx-1);

        BOOST_ASSERT(frame.function.size());

        // cerr << "frame_idx : " << frame_idx << "\n";
        // cerr << "\tframe: " << frame.str() << "\n";
        // cerr << "\tprior-frame: " << prior_frame.str() << "\n";
        // cerr << "\targ_num: " << arg_num << "\n";
        // cerr << "\tmem_intrinsic: " << mem_intrinsic << "\n";

        // if (arg_num == -1) {
        //     struct_info si = get_struct_info_in_frame(prior_frame, ops, arg_num, mem_intrinsic);
        //     cerr << "si = " << si.str() << "\n";
        //     exit(EXIT_FAILURE);
        // }

        BOOST_ASSERT(arg_num >= 0); // 'Cannot continue! (not enough info)'

        if (is_memory_intrinsic(frame.function)) {
            // cerr << "\t\tis a memory intrinsic!\n";
            mem_intrinsic = true;
            arg_num = 0;
            continue;
        }

        list<const Value*> new_ops = get_arg_operand(frame, prior_frame, arg_num, mem_intrinsic);

        // for (const Value *v : ops) {
        //     llvm::errs() << "\t\top = " << *v << "\n";
        // }

        if (new_ops.empty()) {
            // If this is the case, we probably hit an allocation site inside
            // of the prior frame. So we can just use the prior frame info.

            struct_info si(ops.front()->getType(), ops.front());

            for (const Value *op : ops) {
                BOOST_ASSERT(op->getType() == si.type);
            }

            return si;
        }

        // Reset the info from the last frame
        arg_num = -1;
        mem_intrinsic = false;

        struct_info si = get_struct_info_in_frame(frame, new_ops, arg_num, mem_intrinsic);
        // cerr << "\tstruct_info: " << si.str() << "\n";
        if (si.is_valid()) {
            return si;
        } else if (arg_num == -1) {
            // cerr << "give up, go with front op\n";
            return struct_info(new_ops.front()->getType(), new_ops.front());
        }

        ops = new_ops;

#if 0
        // If we can't find a struct type, see if we can find a function arg.
        unordered_set<const AllocaInst *> allocas;
        for (const Value *v : ops) {
            const AllocaInst *alloca = get_alloca_from_value(v);
            if (alloca) {
                allocas.insert(alloca);
            }
        }

        unordered_set<const Argument *> arg_set;

        for (const AllocaInst *alloca : allocas) {
            const Argument *arg = get_argument_from_alloca(alloca);
            if (arg && arg->getParent()->getName().str() == frame.function) {
                // llvm::errs() << "ARG ADD " << *arg << "\n";
                arg_set.insert(arg);
            }
        }

        // Instead, just look at the type of the store.
        if (arg_set.empty()) {
            if (allocas.size() != 1) {
                unordered_set<const AllocaInst *> new_allocas;
                for (const auto *a : allocas) {
                    const auto *pty = dyn_cast<PointerType>(a->getAllocatedType());
                    if (isa<PointerType>(a->getAllocatedType())) {
                        new_allocas.insert(a);
                    }
                }

                if (new_allocas.size() >= 1) {
                    allocas = new_allocas;
                } else {
                    cerr << "weird alloca num: " << new_allocas.size() << "\n";
                    cerr << "\tfrom function: " << frame.function << "\n";
                    for (const auto *a : new_allocas)
                        llvm::errs() << "\t new: " << *a << "\n";
                    for (const auto *a : allocas)
                        llvm::errs() << "\t old: " << *a << "\n";
                    llvm::errs() << *get_function(frame) << "\n";

                    exit(EXIT_FAILURE);
                }
            }

            const Type *alloca_type = (*allocas.begin())->getType();
            const Type *base_type = get_base_type(alloca_type);

            // if (!(isa<StructType>(base_type) ||
            //        isa<VectorType>(base_type) ||
            //        isa<ArrayType>(base_type)) ) {
            //     llvm::errs() << *alloca_type << "\n";
            //     llvm::errs() << *base_type << "\n";
            // }

            // assert(isa<StructType>(base_type) ||
            //        isa<VectorType>(base_type) ||
            //        isa<ArrayType>(base_type));
            cerr << "Exit get_modified_type with data (allocas)\n";
            return struct_info(base_type, *allocas.begin());
        } else if (arg_set.size() > 1) {
            cerr << "too many args!\n";
            exit(EXIT_FAILURE);
        } else {
            arg_num = (*arg_set.begin())->getArgNo();
        }
#endif
    }

    // if (arg_num != -1) {

    // }

    // cerr << "Exit get_modified_type without data\n";
    if (!event.stack.empty()) {
        llvm::errs() << "We should have data if we have a stack!\n";
        exit(EXIT_FAILURE);
    }
    return struct_info();
}

uint64_t type_crawler::get_byte_offset(const struct_info &si) const {
    // static uint64_t ninvoke = 0;

    if (!si.type) {
        return UINT64_MAX;
    }

    BOOST_ASSERT(si.is_valid());

    if (si.offset_values.empty()) {
        // if (!si.type->isStructTy()) {
        //     // llvm::errs() << *si.inst << " (" << *si.type << ")\n";
        //     // exit(1);
        //     // ninvoke++;

        //     // cerr << "(scalar) Ninvoke= " << ninvoke << "\n";
        //     return 0;
        // } else {
        //     return UINT64_MAX;
        // }
        return 0;
    }

    // if (si.offset_values.size()) {
    //     ninvoke++;
    //     cerr << "noff " << si.offset_values.size() << "\n";
    //     llvm::errs() << *si.value << "\n";
    //     cerr << "Ninvoke= " << ninvoke << "\n";
    // }

    if (si.offset_values.size() > 2) {
        cerr << "don't know what to do with all the offsets!\n";
        exit(1);
    }

    // The first offset gets the element in the array, so skip it.
    if (si.offset_values.size() == 1) return 0;

    const Value *elem_offset_expr = si.offset_values[1];
    if (const Constant *c = dyn_cast<Constant>(elem_offset_expr)) {
        uint64_t elem_offset = c->getUniqueInteger().extractBitsAsZExtValue(64, 0);

        if (const StructType *st = dyn_cast<StructType>(si.type)) {
            const StructLayout *sl = module_.getDataLayout()
                .getStructLayout(const_cast<StructType*>(st));
            return sl->getElementOffset(elem_offset);
        } else if (const VectorType *vt = dyn_cast<VectorType>(si.type)) {
            const Type *scalar_type = vt->getScalarType();
            uint64_t scalar_size = module_.getDataLayout()
                .getTypeSizeInBits(const_cast<Type*>(scalar_type)).getFixedSize() / 8;
            return scalar_size * elem_offset;
        } else if (const ArrayType *at = dyn_cast<ArrayType>(si.type)) {
            const Type *element_type = at->getElementType();
            uint64_t element_size = module_.getDataLayout()
                .getTypeSizeInBits(const_cast<Type*>(element_type)).getFixedSize() / 8;
            return element_size * elem_offset;
        }
    } else {
        // If not a constant, we'll just return 0.
        return 0;
    }

    llvm::errs() << "get_byte_offset: err with " << *elem_offset_expr << "\n";
    exit(EXIT_FAILURE);
    return 0;
}

type_info type_crawler::try_get_type_info(const trace_event &event) const {
    struct_info si = get_modified_type(event);

    uint64_t byte_offset = get_byte_offset(si);

    type_info ti(&module_);
    ti.type = si.type;
    ti.offset_in_type = byte_offset;

    return ti;
}

}