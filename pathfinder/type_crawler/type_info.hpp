#pragma once

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/Instruction.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Module.h>

#include <boost/icl/interval_map.hpp>

#include "../utils/common.hpp"
#include "../trace/trace.hpp"

namespace pathfinder
{

std::string get_type_name(const llvm::Type *ty);

struct struct_info {
    const llvm::Type *type = nullptr;
    const llvm::Value *value = nullptr;
    std::vector<const llvm::Value *> offset_values;

    struct_info(const llvm::Type *t, const llvm::Value *v): type(t), value(v) {}
    struct_info(): type(nullptr), value(nullptr) {}

    bool is_valid(void) const { return !!type && !!value; }

    struct hash {
        uint64_t operator()(const struct_info &si) const {
            uint64_t offset_hash = 0;
            for (const llvm::Value *v : si.offset_values) {
                offset_hash ^= std::hash<const llvm::Value*>{}(v);
            }
            return std::hash<const llvm::Type*>{}(si.type) ^
                   std::hash<const llvm::Value*>{}(si.value) ^
                   offset_hash;
        }
    };

    bool operator==(const struct_info &other) const {
        return
            type == other.type &&
            value == other.value &&
            offset_values == other.offset_values;
    }

    std::string str(void) const;
};

struct type_info {
    const llvm::Module *module_;
    const llvm::Type *type;
    uint64_t offset_in_type;

    type_info()
        : module_(nullptr), type(nullptr), offset_in_type(UINT64_MAX) {}

    type_info(const llvm::Module *m)
        : module_(m), type(nullptr), offset_in_type(UINT64_MAX) {}

    type_info(const type_info &ti) = default;
    type_info(type_info &&ti) = default;
    type_info &operator=(const type_info &ti) = default;
    ~type_info() = default;

    bool invalid_size(void) const { return type_size() == UINT64_MAX; }
    bool invalid_offset(void) const { return offset_in_type == UINT64_MAX; }
    uint64_t type_size(void) const;

    bool needs_interpolation(void) const {
        return !type || invalid_size() || invalid_offset();
    }

    bool has_zero_sized_element(void) const;

    bool valid(void) const {
        return !needs_interpolation() &&
            (offset_in_type < type_size() ||
            has_zero_sized_element());
    }

    std::string str(void) const;

    /**
     * Return the address range that the type encompasses, given a trace_event.
     */
    boost::icl::discrete_interval<uint64_t> range(const trace_event &te) const;

    struct hash {
        uint64_t operator()(const type_info &ti) const {
            return std::hash<const llvm::Type*>{}(ti.type) ^
                   std::hash<uint64_t>{}(ti.type_size()) ^
                   std::hash<uint64_t>{}(ti.offset_in_type);
        }
    };

    bool operator==(const type_info &other) const {
        return type == other.type
            && type_size() == other.type_size()
            && offset_in_type == other.offset_in_type;
    }
};


}  // namespace pathfinder