#include "type_info.hpp"

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

namespace pathfinder
{

/* utils */

std::string get_type_name(const llvm::Type *ty) {
    string type_name;
    llvm::raw_string_ostream ostr(type_name);
    BOOST_ASSERT(nullptr != ty);
    if (ty->isStructTy()) {
        ostr << ty->getStructName();
    } else {
        ostr << *ty;
    }

    ostr.flush(); // yes, you have to do this.
    return type_name;
}

/* struct_info */

std::string struct_info::str(void) const {
    string ss;
    llvm::raw_string_ostream ostr(ss);

    ostr << "<struct struct_info> type = ";
    if (type) { ostr << *type; }
    else { ostr << "(null)"; }

    ostr << ", value = ";
    if (value) { ostr << *value; }
    else { ostr << "(null)"; }

    for (const auto *offset : offset_values) {
        ostr << ", offset = ";
        if (value) { ostr << *offset; }
        else { ostr << "(null)"; }
    }

    ostr.flush();
    return ss;
}

/* type_info */

icl::discrete_interval<uint64_t> type_info::range(const trace_event &te) const {
    BOOST_ASSERT(!needs_interpolation());
    BOOST_ASSERT(type_size() >= offset_in_type);

    icl::discrete_interval<uint64_t> te_range = te.range();
    uint64_t start = te_range.lower() - offset_in_type;
    uint64_t end = start + type_size();
    // A bit of a hack.
    // end = end > (start + te.size) ? end : start + te.size;
    end = std::max(end, te_range.upper());
    auto r = icl::interval<uint64_t>::right_open(start, end);

    BOOST_ASSERT(r.lower() <= te_range.lower());
    // cerr << r << " : " << te.range() << "\n";
    // cerr << "\t" << str() << "\n";
    // Gotta check for 0-sized offset.
    BOOST_ASSERT(r.upper() >= te_range.upper());

    BOOST_ASSERT(type_size() <= (r.upper() - r.lower()));

    return r;
}

uint64_t type_info::type_size(void) const {
    return module_->getDataLayout().getTypeSizeInBits(const_cast<Type*>(type))
        .getFixedSize() / 8;
}

bool type_info::has_zero_sized_element(void) const {
    if (const StructType *st = dyn_cast<StructType>(type)) {
        for (Type *e : st->elements()) {
            if (0 == module_->getDataLayout().getTypeSizeInBits(e).getFixedSize()) {
                return true;
            }
        }
    } else if (const ArrayType *at = dyn_cast<ArrayType>(type)) {
        if (at->getNumElements() == 0) {
            return true;
        }
    }
    // VectorType should have one or more elements.

    return false;
}

std::string type_info::str(void) const {
    string s;
    raw_string_ostream ss(s);

    ss << "<struct type_info> type = ";
    if (type) { ss << *type; }
    else { ss << "(null)"; }

    ss << ", type_size = " << type_size() << " (";
    ss.write_hex(type_size());
    ss << "), ";
    ss << "offset_in_type = " << offset_in_type << " (";
    ss.write_hex(offset_in_type);
    ss << "), ";
    ss << "has_zero_sized_element = " << has_zero_sized_element() << ", ";
    ss << "is_valid = " << valid() << "()";

    return ss.str();
}

}  // namespace pathfinder