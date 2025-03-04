#include "model_checker_state.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <unordered_map>
#include <cstdint>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/uio.h>
#include <thread>
#include <vector>
#include <iomanip>
#include <errno.h>

#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/insert_linebreaks.hpp>
#include <boost/archive/iterators/ostream_iterator.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/process.hpp>

namespace aio = boost::asio;
namespace ait = boost::archive::iterators;
namespace bp = boost::process;
namespace fs = boost::filesystem;
namespace icl = boost::icl;
using namespace std::chrono;
using namespace std;

/**
 * We would normally accumulate all transient stores across the testing region.
 * If this is enabled, assume transient stores are persisted at the end of epochs
 * UNLESS they are explicitly listed in the epoch we are testing.
 */
#define TRANSIENT_ELISION 1
/**
 * Only run we tests during epochs if we've stored to another field we're interested
 * in testing. Otherwise, wait until the next vertex.
 *
 */
#define TEST_ELISION 1
/**
 * This is a failure isolation testing strategy, but it doesn't have to be done
 * inline; we can actually do this process using the prior test results.
 *
 */
#define INLINE_DELTA_DEBUGGING 0

#define DEBUG_PRINTS 0

// whether to output possible ordering generated for debugging
#define OUTPUT_ORDERINGS 1

/**
 * A temporary solution to prevent OOM.
*/
#define MAX_PERMS (1ul << 9ul)

namespace pathfinder {

/* model_checker_state */

typedef
    ait::base64_from_binary<
        ait::transform_width<
                const char *,
                6,
                8
            >
        > base64_text;

static void dump_file_images(basic_ostream<char> &os, const test_result &res) {
    if (res.file_images.empty()) return;

    for (const auto &p : res.file_images) {
        // Store as base64
        std::stringstream ss;
        std::copy(
            base64_text(p.second.data()),
            base64_text(p.second.data() + p.second.size()),
            ostream_iterator<char>(ss));

        ss.flush();

        if (ss.str().size()) {
            os << "," << ss.str();
        } else {
            os << ",NULL";
        }

    }
}

static void dump_test_result(
        basic_ostream<char> &os,
        const event_config &ec,
        const test_result &res,
        seconds timestamp) {

    assert(res.valid() && "Test result isn't properly initialized!\n");
    // assert(!ec.empty() && "Test result needs stores!\n");

    for (const auto &p : ec) {
        os << p.second << ",";
    }

    string escaped = res.output;
    boost::replace_all(escaped, "\"", "\"\"");

    os << res.ret_code << ",\"" << escaped << "\"";

    escaped = res.note;
    boost::replace_all(escaped, "\"", "\"\"");
    os << ",\"" << escaped << "\"";

    dump_file_images(os, res);

    os << "," << timestamp.count() << "\n";
}

void dump_test_result_locked(mutex &m, fs::path resfile,
    event_config ec, test_result res, seconds timestamp) {

    stringstream ss;
    dump_test_result(ss, ec, res, timestamp);
    ss.flush();
    {
        const lock_guard<mutex> l(m);
        // cerr << "dumping to '" << resfile.string() << "'\n";
        fs::ofstream os;
        os.open(resfile, ios_base::app);
        os.seekp(0, ios_base::end);
        os << ss.str();
        os.flush();
        os.close();
    }
}

fs::path model_checker_state::construct_outdir_path(
        int perm_id, string suffix) const {

    stringstream ss;
    ss << test_id << "_" << test_case_id_ << "_" << perm_id << suffix;
    ss.flush();
    fs::path outpath = outdir / ss.str();
    error_if_exists(outpath);
    return outpath;
}

fs::path model_checker_state::construct_outdir_path(string suffix, bool exists_ok) const {
    stringstream ss;
    ss << test_id << "_" << test_case_id_ << suffix;
    ss.flush();
    fs::path outpath = outdir / ss.str();
    if (!exists_ok) error_if_exists(outpath);
    return outpath;
}

void model_checker_state::add_test_result(
    fs::path resfile, const event_config &ec, const test_result &res, seconds timestamp) {

    fs::ofstream rstream;
    rstream.open(resfile, ios_base::app);
    rstream.seekp(0, ios_base::end);
    dump_test_result(rstream, ec, res, timestamp);
    rstream.flush();
    rstream.close();
}

future<void> model_checker_state::add_test_result_async(
    mutex &m, fs::path resfile, const event_config &ec, const test_result &res, seconds timestamp) {

    return async(launch::async, [=](mutex *mtx) {
        dump_test_result_locked(*mtx, resfile, ec, res, timestamp);
    }, &m);
}

bool model_checker_state::store_is_redundant(shared_ptr<trace_event> te) const {
    void *addr = (void*)te->address;
    void *translated = NULL;
    // Address translation
    for (const auto &p : offset_mapping_) {
        if (p.first.lower() <= (uintptr_t)addr && (uintptr_t)addr < p.first.upper()) {
            ptrdiff_t offset = (uintptr_t) addr - p.first.lower();
            translated = (void*)(p.second.lower() + offset);
            break;
        }
    }
    BOOST_ASSERT(translated);

    return 0 == memcmp(translated, (void*)&te->value, te->size);
}

bool model_checker_state::do_store(shared_ptr<trace_event> te) {
    void *addr = (void*)te->address;
    void *translated = NULL;

    // Address translation
    for (const auto &p : offset_mapping_) {
        if (p.first.lower() <= (uintptr_t)addr && (uintptr_t)addr < p.first.upper()) {
            ptrdiff_t offset = (uintptr_t) addr - p.first.lower();
            translated = (void*)(p.second.lower() + offset);
            break;
        }
    }

    #ifdef DEBUG_MODE
    auto range = icl::interval<uintptr_t>::right_open(
        (uintptr_t)translated, (uintptr_t)translated + te->size);
    // assert(mapped_.find(range) != mapped_.end());
    if (mapped_.find(range) == mapped_.end()) {
        cerr << "Store "<< te->store_id() << " address is not translated :( \n";
        exit(EXIT_FAILURE);
    }
    #endif

    if (translated) {
        memcpy(translated, (void*)te->value_bytes.data(), te->value_bytes.size());
    } else {
        cerr << "Store "<< te->store_id() << " address is not translated :( \n";
        exit(EXIT_FAILURE);
    }
    return (translated != NULL);
}

void model_checker_state::do_write(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    // vector<char> char_vec(te->buf.begin(), te->buf.end());
    // ssize_t sz = write(file_to_fd.at(te->file_path),
    // &char_vec[0], char_vec.size());
    // if (fs::path(te->file_path).filename().string() == "000004.log"){
    //     for (int i = 0; i < te->size; i++) {
    //         // print in hex 
    //         cout << hex << (int)te->char_buf.get()[i] << " ";
    //     }
    //     // end hex
    //     cout << dec;
    //     cout << endl;
    //     cerr << "write to 000004.log" << endl;
    // }
    ssize_t sz = write(fd_to_fd.at(te->fd), te->char_buf.get(), te->size);
    // fsync(file_to_fd[te->file_path]);
    if (sz != te->size) {
        lock_guard<mutex> l(*stdout_mutex_);
        cerr << test_id << ":" << "File write failed!" << " fd " << fd_to_fd.at(te->fd) << " " << fsfile_map[te->file_path].string() << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_pwritev(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));

    struct iovec* iov;
    iov = (struct iovec*)malloc(te->buf_vec.size() * sizeof(*iov));

    ssize_t w_sz = 0;

    vector<vector<char>*> char_vecs;

    for (size_t i=0; i<te->buf_vec.size(); i++) {
        vector<char>* char_vec = new vector<char>(te->buf_vec[i].begin(), te->buf_vec[i].end());
        char_vecs.push_back(char_vec);
        iov[i].iov_base = &((*char_vec)[0]);
        iov[i].iov_len = char_vec->size();
        w_sz += char_vec->size();
    }

    ssize_t sz = pwritev(fd_to_fd.at(te->fd), iov, te->buf_vec.size(), te->wfile_offset);

    if (w_sz != sz) {
        cerr << "File pwritev failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }

    free(iov);
    for (auto &ptr: char_vecs) {
        free(ptr);
    }
}

void model_checker_state::do_ftruncate(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));

    int rt = ftruncate(fd_to_fd.at(te->fd), te->len);
    if (rt) {
        cerr << "File ftruncate failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }

}

void model_checker_state::do_fallocate(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));

    int rt = fallocate(fd_to_fd.at(te->fd), te->mode, te->file_offset, te->len);
    if (rt) {
        cerr << "File fallocate failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_msync(shared_ptr<trace_event> te) {
    void *addr = (void*)te->address;
    void *translated = NULL;

    // Address translation
    for (const auto &p : offset_mapping_) {
        if (p.first.lower() <= (uintptr_t)addr && (uintptr_t)addr < p.first.upper()) {
            ptrdiff_t offset = (uintptr_t) addr - p.first.lower();
            translated = (void*)(p.second.lower() + offset);
            break;
        }
    }

    #ifdef DEBUG_MODE
    auto range = icl::interval<uintptr_t>::right_open(
        (uintptr_t)translated, (uintptr_t)translated + te->size);
    assert(mapped_.find(range) != mapped_.end());
    #endif

    if (translated) {
        int rt = msync(translated, te->size, te->flags);
        if (rt) {
            cerr << "File msync failed!" << endl;
            cerr << string(strerror(errno)) << endl;
            exit(EXIT_FAILURE);
        }
    } else {
        cerr << "Msync address is not translated :( \n";
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_pwrite64(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));

    // vector<char> char_vec(te->buf.begin(), te->buf.end());
    // BOOST_ASSERT(te->size == char_vec.size());
    ssize_t sz = pwrite64(fd_to_fd.at(te->fd),
    te->char_buf.get(), te->size, te->file_offset);
    // fsync(file_to_fd[te->file_path]);
    if (sz != te->size) {
        cerr << "File pwrite64 failed!" << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_writev(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    assert(te->iov.size() == te->iovcnt);

    struct iovec* iov;
    iov = (struct iovec*)malloc(te->iov.size() * sizeof(*iov));

    ssize_t w_sz = 0;


    for (size_t i=0; i<te->iov.size(); i++) {
        iov[i].iov_base = std::get<1>(te->iov[i]).get();
        iov[i].iov_len = std::get<0>(te->iov[i]);
        w_sz += std::get<0>(te->iov[i]);
    }

    ssize_t sz = writev(fd_to_fd.at(te->fd), iov, te->iov.size());

    if (w_sz != sz) {
        cerr << "File writev failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }

    free(iov);
}

void model_checker_state::do_lseek(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    off_t offset = lseek(fd_to_fd.at(te->fd), te->file_offset, te->flags);
    if (offset == -1) {
        cerr << "File lseek failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_rename(shared_ptr<trace_event> te) {
    fs.rename_file(fsfile_map[te->file_path].string(), fsfile_map[te->new_path].string());
}

void model_checker_state::do_unlink(shared_ptr<trace_event> te) {
    fs.unlink_file(fsfile_map[te->file_path].string());
}

void model_checker_state::do_fsync(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    fs.fsync_file_or_dir(fsfile_map[te->file_path].string(), fd_to_fd.at(te->fd));
}

void model_checker_state::do_fdatasync(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    int rt = fdatasync(fd_to_fd.at(te->fd));
    if (rt) {
        cerr << "File fdatasync failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_unregister_file(shared_ptr<trace_event> te) {
    BOOST_ASSERT(file_to_fd.at(te->file_path) != -1);
    // do unmap
    void *addr = (void*)te->address;
    void *translated = NULL;

    // Address translation
    // for (const auto &p : offset_mapping_) {
    //     if (p.first.lower() <= (uintptr_t)addr && (uintptr_t)addr < p.first.upper()) {
    //         ptrdiff_t offset = (uintptr_t) addr - p.first.lower();
    //         translated = (void*)(p.second.lower() + offset);
    //         break;
    //     }
    // }

    // if (translated) {
    //     auto range = icl::interval<uintptr_t>::right_open(
    //         (uintptr_t)translated, (uintptr_t)translated + te->size);
    //     mapped_.erase(range);
    //     munmap(translated, te->size);
    // } 
    // else {
    //     cerr << "Unregister address is not translated :( \n";
    //     exit(EXIT_FAILURE);
    // }

    // it is a bit tricky for ummap, since it could unmap a sub-range of a mmaped region
    // we traverse the offset_mapping and check if this is a partial removal
    bool found = false;
    for (auto it = offset_mapping_.begin(); it != offset_mapping_.end(); ) {
        if (it->first.lower() <= (uintptr_t)addr && it->first.upper() >= (uintptr_t)addr + te->size) {
            ptrdiff_t offset_lower = (uintptr_t) addr - it->first.lower();
            ptrdiff_t offset_upper = it->first.upper() - (uintptr_t) addr - te->size;
            // add back the remaining ranges
            if (offset_lower > 0) {
                auto range_lower = icl::interval<uintptr_t>::right_open(
                    it->first.lower(), (uintptr_t)addr);
                auto range_lower_translated = icl::interval<uintptr_t>::right_open(
                    it->second.lower(), (uintptr_t)it->second.lower() + offset_lower);
                mapped_.insert(range_lower_translated);
                offset_mapping_[range_lower] = range_lower_translated;
            }
            if (offset_upper > 0) {
                auto range_upper = icl::interval<uintptr_t>::right_open(
                    (uintptr_t)addr + te->size, it->first.upper());
                auto range_upper_translated = icl::interval<uintptr_t>::right_open(
                    (uintptr_t)it->second.upper() - offset_upper, (uintptr_t)it->second.upper());
                mapped_.insert(range_upper_translated);
                offset_mapping_[range_upper] = range_upper_translated;
            }
            // do munmap
            auto translated = (void*)(it->second.lower() + offset_lower);
            int ret = munmap(translated, te->size);
            if (ret) {
                cerr << "File munmap failed!" << endl;
                cerr << string(strerror(errno)) << endl;
                exit(EXIT_FAILURE);
            }
            // remove the original range
            mapped_.erase(it->second);
            offset_mapping_.erase(it++);
            found = true;
            break;
        }
            
    }
    if (!found) {
        cerr << "Unregister address is not translated :( \n";
        exit(EXIT_FAILURE);
    }

}

void model_checker_state::do_open(shared_ptr<trace_event> te) {
    string path = fsfile_map[te->file_path].string();
    int fd = fs.open_file(path, te->flags, te->mode);
    file_to_fd[te->file_path] = fd;
    fd_to_fd[te->fd] = fd;
    // lock_guard<mutex> l(*stdout_mutex_);
    // cout <<  test_id << ":" << "[do_open] open file: " << te->file_path << " fd: " << fd << endl;
    // // print file_to_fd
    // cout <<  test_id << ":" << "[do_open] file_to_fd: " << endl;
    // for (auto it = file_to_fd.begin(); it != file_to_fd.end(); it++) {
    //     cout << it->first << " " << it->second << endl;
    // }
}

void model_checker_state::do_creat(shared_ptr<trace_event> te) {
    int fd = fs.create_file(fsfile_map[te->file_path].string(), te->mode);
    file_to_fd[te->file_path] = fd;
    fd_to_fd[te->fd] = fd;
}

void model_checker_state::do_close(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    int rt = close(fd_to_fd.at(te->fd));
    if (rt) {
        cerr << "File close failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
    // lock_guard<mutex> l(*stdout_mutex_);
    // cout <<  test_id << ":" << "[do_close] close file: " << te->file_path << " fd: " << fd_to_fd.at(te->fd) << endl;

    // // print file_to_fd
    // cout <<  test_id << ":" << "[do_close] file_to_fd: " << endl;
    // for (auto it = file_to_fd.begin(); it != file_to_fd.end(); it++) {
    //     cout << it->first << " " << it->second << endl;
    // }

    file_to_fd[te->file_path] = -1;
    fd_to_fd[te->fd] = -1;
}

void model_checker_state::do_mkdir(shared_ptr<trace_event> te) {
    int rt = mkdir(fsfile_map[te->file_path].c_str(), te->mode);
    if (rt) {
        cerr << "File mkdir failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_rmdir(shared_ptr<trace_event> te) {
    int rt = rmdir(fsfile_map[te->file_path].c_str());
    if (rt) {
        cerr << "File rmdir failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_sync(shared_ptr<trace_event> te) {
    sync();
}

void model_checker_state::do_syncfs(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    int rt = syncfs(fd_to_fd.at(te->fd));
    if (rt) {
        cerr << "File syncfs failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

void model_checker_state::do_sync_file_range(shared_ptr<trace_event> te) {
    assert((fd_to_fd.find(te->fd) != fd_to_fd.end()) && (fd_to_fd.at(te->fd) != -1));
    int rt = sync_file_range(fd_to_fd.at(te->fd), te->file_offset, te->len, te->flags);
    if (rt) {
        cerr << "File sync_file_range failed!" << endl;
        cerr << string(strerror(errno)) << endl;
        exit(EXIT_FAILURE);
    }
}

icl::discrete_interval<uintptr_t> model_checker_state::do_register_file(
    shared_ptr<trace_event> te) {

    // Map the file into our address space
    const fs::path &pmfile = get_file_path(te);

    if (!fs::exists(pmfile)) {
        std::ofstream o;
        o.open(pmfile.c_str(), ios::out);
        o.close();
        assert(fs::exists(pmfile));
    }

    if (fs::file_size(pmfile) < te->size) {
        fs::resize_file(pmfile, te->size);
    }


    int fd = -1;
    if (file_to_fd.find(te->file_path) != file_to_fd.end() && file_to_fd.at(te->file_path) != -1) {
        // if already in file_to_fd
        fd = file_to_fd.at(te->file_path);
    } else {
        fd = open(pmfile.c_str(), O_RDWR);
    }

    if (fd == -1) {
        perror("open pmfile");
        exit(EXIT_FAILURE);
    }

    /**
     * @brief So as much as I would like to directly do this mapping, it seems
     * that I have to do some translation, because this seems to screw up the
     * heap or something and causes a bunch of memory errors.
     *
     */
    // void *addr = mmap((void*)te->address, te->size, PROT_READ | PROT_WRITE,
    //     MAP_SHARED | MAP_FIXED, fd, te->file_offset);

    void *addr = mmap((void*)te->address, te->size, PROT_READ | PROT_WRITE,
        MAP_SHARED, fd, te->file_offset);

    // cerr << (void*)te->address << " vs " << addr << "\n";

    // cerr << "get out!\n";
    // return icl::interval<uintptr_t>::right_open((uintptr_t)0, (uintptr_t)1);

    if (addr == MAP_FAILED) {
        perror("map pmfile");
        exit(EXIT_FAILURE);
    }

    auto orig = icl::interval<uintptr_t>::right_open(
        (uintptr_t)te->address, (uintptr_t)te->address + te->size);

    auto range = icl::interval<uintptr_t>::right_open(
        (uintptr_t)addr, (uintptr_t)addr + te->size);

    mapped_.insert(range);
    file_memory_regions_[pmfile.string()].push_back(range);

    offset_mapping_[orig] = range;

    // mmap makes another reference, so close the fd if it is not opened for writes
    if (file_to_fd.find(te->file_path) == file_to_fd.end()
        || file_to_fd.at(te->file_path) == -1) {
        (void)close(fd);
    }

    return range;
}

template <typename C>
static size_t num_possible_permutations(const C &c) {
    return (size_t)pow(2lu, c.size());
}

template <typename T>
static list<list<T>> permute(const list<T> &s) {
    list<list<T>> res;

    if (s.empty()) {
        res.push_back(list<T>{});
    } else if (s.size() == 1) {
        res.push_back(list<T>{});
        res.push_back(s);
    } else {
        list<T> partial(s.begin(), s.end());
        T elem = *partial.begin();
        partial.erase(partial.begin());
        for (list<T> p : permute(partial)) {
            res.push_back(p);
            p.push_back(elem);
            res.push_back(p);
        }
    }

    BOOST_ASSERT(res.size() == num_possible_permutations(s));

    return res;
}

template <typename C>
void model_checker_state::print_orderings(
        const C &events, fs::ofstream &odstream) {
    int order = 1;
    for (const auto &te : events) {
        if (te->is_store()) {
            odstream<<"#"<<order<<": Store "<<te->store_id()<< " (Event "
                << te->event_idx() << ")" << endl;
        } else if (te->is_write()) {
            odstream<<"#"<<order<<": Write "<<te->event_idx()<<endl;
        } else if (te->is_pwritev()) {
            odstream<<"#"<<order<<": Pwritev "<<te->event_idx()<<endl;
        } else if (te->is_fallocate()) {
            odstream<<"#"<<order<<": Fallocate "<<te->event_idx()<<endl;
        } else if (te->is_ftruncate()) {
            odstream<<"#"<<order<<": Ftruncate "<<te->event_idx()<<endl;
        } else {
            odstream<<"#"<<order<<": ??? "<<te->event_idx()<<endl;
        }

        if (!te->stack.empty()) {
            stack_frame last_frame = te->stack.back();
            odstream<<"Most recent trace: (file) "<<last_frame.file<<" (function) "<<last_frame.function<<" (line) "<<last_frame.line<<endl;
        } else {
            odstream << "Most recent trace: N/A" << endl;
        }
        order++;
    }
    odstream.flush();
}

set<permutation_t>
model_checker_state::generate_orderings(
        const set<shared_ptr<trace_event>> &stores,
        set<permutation_t> &already_tested) const
{
    return generate_orderings(
        stores, set<shared_ptr<trace_event>>(), already_tested);
}

set<permutation_t>
    model_checker_state::generate_orderings(
        const set<shared_ptr<trace_event>> &stores,
        const set<shared_ptr<trace_event>> &syscalls,
        set<permutation_t> &already_tested) const
{

    set<permutation_t> results;

    icl::interval_map<uint64_t, permutation_t> cacheline_state;

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] Generating orderings for " << stores.size() << " stores!\n";
    #endif

    // Handle empty case first
    if (!already_tested.count(permutation_t())) {
        results.insert(permutation_t());
    }

    size_t nskipped = 0;

    // Now start adding.
    for (auto te : stores) {
        /**
         * @brief We could make this a bit more sophisticated---we could track
         * how stores update the cachelines by doing per-cacheling data reconstruction,
         * but this is simpler.
         *
         * Basically, if this is some bulk memset to 0 or something, then skip it.
         */
        if (cacheline_state.find(te->cacheline_range()) == cacheline_state.end()
            && store_is_redundant(te)) {
            nskipped++;
            continue;
        }

        // First, add.
        cacheline_state.add(make_pair(te->cacheline_range(), permutation_t({te})));

        // Now, get the sets
        // -- We start with everything from this cacheline
        permutation_t current = cacheline_state.find(te->cacheline_range())->second;
        // -- Now, we need all permutations of other cachelines. Meaning, this
        // gives us the orderings of the other lists.
        list<icl::discrete_interval<uint64_t>> cachelines;
        for (const auto &p : cacheline_state) {
            if (p.first == te->cacheline_range()) continue;
            cachelines.push_back(p.first);
        }

        list<list<icl::discrete_interval<uint64_t>>> cl_perms = permute(cachelines);
        for (const auto &perm : cl_perms) {
            permutation_t stores = current;
            for (const auto &range : perm) {
                const auto it = cacheline_state.find(range);
                if (it != cacheline_state.end()) {
                    const permutation_t &prior = it->second;
                    stores.insert(stores.end(), prior.begin(), prior.end());
                }
            }
            if (!already_tested.count(stores)) {
                if (results.size() > MAX_PERMS) {
                    cerr << "MAX_PERMS (" << MAX_PERMS << ") reached!" << endl;
                    cerr << "\tTest: " << test_id << "_" << test_case_id_ << endl;
                    cerr << "\tCacheline permute size: " << cl_perms.size() << endl;
                    // exit(EXIT_FAILURE);
                    goto end;
                }
                results.insert(stores);
                // We'll do the "already tested" thing without syscall consideration,
                // since what we do for the syscalls will be unique if we filter here.
                already_tested.insert(stores);
            }
        }
    }

end:

    if (!syscalls.empty()) {
        #if DEBUG_PRINTS
        cerr << "\t\t[" << test_id << "] Generated " << results.size() << " orderings!\n";
        #endif
    }

    // making it agnostic to how we generate stores, apply syscalls afterwards
    // for now just push syscalls at the front and back, not complete orderings but still valid

    set<permutation_t> old_results(results);
    results.clear();

    for (const auto &old_perm : old_results) {
        permutation_t perm = old_perm;

        int max_ts = -1;
        for (auto &te: perm) {
            if (te->event_idx() > max_ts) {
                max_ts = te->event_idx();
            }
        }
        // apply all syscalls that happen before the latest store in perm
        list<shared_ptr<trace_event>> early_syscalls, late_syscalls;
        list<permutation_t> late_perms;
        for (auto &syscall: syscalls) {
            if (syscall->is_msync()) {
                cerr << "ERROR: msync syscall in generate_orderings!" << endl;
                exit(EXIT_FAILURE);
            }
            if (syscall->event_idx() < max_ts) {
                early_syscalls.push_front(syscall);
            } else {
                late_syscalls.push_back(syscall);
                late_perms.push_back(late_syscalls);
            }
        }

        for (shared_ptr<trace_event> early_syscall : early_syscalls) {
            perm.push_front(early_syscall);
        }

        results.insert(perm);
        // for all syscalls after the latest store, they may or may not happen
        // list<list<shared_ptr<trace_event>>> late_perms = permute(late_syscalls);
        for (const auto& late_perm: late_perms) {
            if (results.size() > MAX_PERMS) {
                cerr << "MAX_PERMS (" << MAX_PERMS << ") reached---syscall" << endl;
                cerr << "\tTest: " << test_id << "_" << test_case_id_ << endl;
                // exit(EXIT_FAILURE);
                goto end_syscall;
            }
            permutation_t final_perm(perm);
            final_perm.insert(final_perm.end(), late_perm.begin(), late_perm.end());
            results.insert(final_perm);
        }
    }

end_syscall:

    #if DEBUG_PRINTS
    cerr << "\t\t[" << test_id << "] Generated " << results.size() <<
        " orderings (with syscalls)!\n";
    #endif

    return results;
}



model_checker_code model_checker_state::test_exhaustive_orderings(
    const event_config &init_config,
    const event_set &stores,
    const event_set &syscalls,
    string note)
{
    typedef set<shared_ptr<trace_event>> event_set;

    if (stores.empty()) {
        return NO_BUGS;
    }

    const time_point<system_clock> ord_start = system_clock::now();

    bool any_bugs = false;
    bool all_bugs = true;

    event_config baseline = init_config;
    for (const auto &te : stores) {
        baseline[te->store_id()] = -1;
    }

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ << ": " << ss.str() << endl;
    #endif

    // output ordering generated for debugging
    #if OUTPUT_ORDERINGS
    fs::path orderingfile = construct_outdir_path("_ordering.txt");
    fs::ofstream odstream(orderingfile);
    #endif

    if (__glibc_unlikely(baseline.empty())) {
        cerr << "no stores!\n";
        exit(EXIT_FAILURE);
    }

    if (pmdir.empty()) {
        checkpoint_push();
    } else {
        backup_pmdir(true);
    }

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_start) / 1s
        << " to setup!\n";
    #endif

    const time_point<system_clock> ord_test = system_clock::now();

    icl::interval_map<uint64_t, event_set> cacheline_state;


    // Handle empty case first
    int perm_id = 0;


    // Now start adding.
    for (const auto &te : stores) {
        /**
         * @brief We could make this a bit more sophisticated---we could track
         * how stores update the cachelines by doing per-cacheling data reconstruction,
         * but this is simpler.
         *
         * Basically, if this is some bulk memset to 0 or something, then skip it.
         */


        // First, add.
        event_set es{te};
        cacheline_state.add(make_pair(te->cacheline_range(), es));

        // Now, get the sets
        // -- We start with everything from this cacheline
        event_set current = cacheline_state.find(te->cacheline_range())->second;
        // -- Now, we need all permutations of other cachelines. Meaning, this
        // gives us the orderings of the other lists.
        vector<icl::discrete_interval<uint64_t>> cachelines;
        for (const auto &p : cacheline_state) {
            if (p.first == te->cacheline_range()) continue;
            cachelines.push_back(p.first);
        }
        int binary_mask = 0;
        int upper_bound;
        if (cachelines.size() <= 32) upper_bound = pow(2, cachelines.size());
        else upper_bound = INT_MAX;
        #if OUTPUT_ORDERINGS
        odstream<<"====="<<endl;
        odstream<<"Size of other cachelines: "<<cachelines.size()<<endl;
        odstream<<"Size of stores on current cacheline: "<<current.size()<<endl;
        odstream<<"====="<<endl;
        odstream<<endl;
        odstream.flush();
        #endif
        while (binary_mask < upper_bound && duration_cast<chrono::seconds>(system_clock::now() - ord_test) <= baseline_timeout) {
            set<icl::discrete_interval<uint64_t>> cachelines_subset;
            for (int i=0; i<cachelines.size(); i++) {
                if ((binary_mask >> i) & 1) {
                    cachelines_subset.insert(cachelines[i]);
                }
            }
            binary_mask++;
            event_set stores = current;
            for (const auto &range : cachelines_subset) {
                const auto it = cacheline_state.find(range);
                if (it != cacheline_state.end()) {
                    const event_set &prior = it->second;
                    stores.insert(prior.begin(), prior.end());
                }
            }
            // we just directly test on these stores
            list<shared_ptr<trace_event>> events_list(
                stores.begin(), stores.end());

            if (syscalls.size() > 0) {
                // insert each syscall to the first store that has timestamp bigger
                for (const auto &syscall : syscalls) {
                    for (auto iter = events_list.begin();
                         iter != events_list.end();
                         ++iter) {
                        if ((*iter)->event_idx() > syscall->event_idx()) {
                            events_list.insert(iter, syscall);
                            break;
                        }
                    }
                }
            }
            #if OUTPUT_ORDERINGS
            odstream<<"Perm "<<perm_id<<": "<<endl;
            odstream<<endl;
            odstream<<"====="<<endl;
            odstream.flush();
            print_orderings(events_list, odstream);
            #endif
            perm_id++;
            // shrink output size by go with an empty baseline
            event_config curr;

            create_permutation(events_list, curr);
            test_result res = test_permutation(note);

            // prepare output file
            stringstream ss;
            ss << test_id << "_" << test_case_id_++ << ".csv";
            ss.flush();
            // Initialize the file
            fs::path resfile = outdir / ss.str();
            assert(!fs::exists(resfile));
            fs::ofstream rstream(resfile);
            for (const auto &p : curr) {
                rstream << p.first << ",";
            }
            // -- now, the fields in the wahoo
            rstream << "ret_code,message,note,timestamp" << endl;
            rstream.flush();

            dump_test_result(rstream, curr, res,
                duration_cast<seconds>(system_clock::now() - start_time));

            rstream.flush();
            rstream.close();

            all_bugs = res.contains_bug() && all_bugs;
            any_bugs = res.contains_bug() || any_bugs;
            if (pmdir.empty()) {
                restore();
            } else {
                restore_pmdir(true);
            }
            #if OUTPUT_ORDERINGS
            odstream<<"====="<<endl;
            odstream<<endl;
            odstream.flush();
            #endif

        }

        auto ord_duration = duration_cast<chrono::seconds>(
            system_clock::now() - ord_test);
        if (ord_duration > baseline_timeout) {
            cout << "Max timeout " << baseline_timeout.count() <<
                " minutes elapsed, terminating test "<< test_id << endl;
            break;
        }

    }

    #if OUTPUT_ORDERINGS
    odstream.close();
    #endif

    const time_point<system_clock> ord_cleanup = system_clock::now();

    if (pmdir.empty()) {
        checkpoint_pop();
    }

    // cleanup backup files
    for (const auto &p : backup_map) {
        if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }

    if (!backup_dir.empty()) {
        fs::remove_all(backup_dir);
        error_if_exists(backup_dir);
    }


    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_cleanup) / 1s
        << " seconds to cleanup!\n";
    #endif

    test_case_id_++;

    if (all_bugs) {
        return ALL_INCONSISTENT;
    }

    if (any_bugs) {
        return HAS_BUGS;
    }

    return NO_BUGS;
}

model_checker_code model_checker_state::test_linear_orderings(
    const event_config &init_config,
    const event_set &stores,
    const event_set &syscalls,
    string note)
{
    if (stores.empty()) {
        return NO_BUGS;
    }

    const time_point<system_clock> ord_start = system_clock::now();

    bool any_bugs = false;
    bool all_bugs = true;

    event_config baseline = init_config;
    for (const auto &te : stores) {
        baseline[te->store_id()] = -1;
    }

    // output ordering generated for debugging
    #if OUTPUT_ORDERINGS
    fs::path orderingfile = construct_outdir_path("_ordering.txt");
    fs::ofstream odstream(orderingfile);
    #endif

    size_t init_checkpoints = num_checkpoints();

    if (pmdir.empty()) {
        checkpoint_push();
    } else {
        backup_pmdir(true);
    }

    const time_point<system_clock> ord_test = system_clock::now();

    // Handle empty case first
    int perm_id = 0;
    // dont do empty set for now
    // #if OUTPUT_ORDERINGS
    // odstream<<"Perm (emptyset) "<<perm_id<<": "<<endl;
    // odstream<<endl;
    // odstream<<"====="<<endl;
    // odstream.flush();
    // print_orderings(list<shared_ptr<trace_event>>({}), odstream);
    // #endif
    // perm_id++;
    // event_config curr = baseline;
    // test_result res = test_permutation(list<shared_ptr<trace_event>>({}), curr, note);
    // dump_test_result(rstream, curr, res, duration_cast<seconds>(system_clock::now() - start_time));
    // rstream.flush();
    // all_bugs = res.contains_bug() && all_bugs;
    // any_bugs = res.contains_bug() || any_bugs;
    // if (pmdir == fs::path("")) restore();
    // // restore_write_files();
    // else restore_pmdir(true);
    // #if OUTPUT_ORDERINGS
    // odstream<<"====="<<endl;
    // odstream<<endl;
    // odstream.flush();
    // #endif

    list<shared_ptr<trace_event>> events_list;
    for (auto te: stores) {
        events_list.push_back(te);
        list<shared_ptr<trace_event>> curr_events(events_list);
        if (syscalls.size() > 0) {
                for (const auto &syscall : syscalls) {
                    for (auto iter=curr_events.begin(); iter!=curr_events.end(); iter++) {
                        if ((*iter)->event_idx() > syscall->event_idx()) {
                            curr_events.insert(iter, syscall);
                            break;
                        }
                    }
                }
        }
        // shrink output size by go with an empty baseline
        event_config curr;
        #if OUTPUT_ORDERINGS
        odstream<<"Perm "<<perm_id<<": "<<endl;
        odstream<<endl;
        odstream<<"====="<<endl;
        odstream.flush();
        print_orderings(curr_events, odstream);
        #endif

        create_permutation(curr_events, curr);

        test_result res = test_permutation(note);

        perm_id++;

        // prepare output file
        stringstream ss;
        ss << test_id << "_" << test_case_id_++ << ".csv";
        ss.flush();
        // Initialize the file
        fs::path resfile = outdir / ss.str();
        assert(!fs::exists(resfile));
        fs::ofstream rstream(resfile);
        for (const auto &p : curr) {
            rstream << p.first << ",";
        }
        // -- now, the fields in the wahoo
        rstream << "ret_code,message,note,timestamp";
        rstream << "\n";
        rstream.flush();

        dump_test_result(rstream, curr, res, duration_cast<seconds>(system_clock::now() - start_time));
        rstream.flush();
        rstream.close();
        all_bugs = res.contains_bug() && all_bugs;
        any_bugs = res.contains_bug() || any_bugs;
        if (pmdir.empty()) {
            restore();
        } else {
            restore_pmdir(true);
        }
        #if OUTPUT_ORDERINGS
        odstream<<"====="<<endl;
        odstream<<endl;
        odstream.flush();
        #endif

        if (duration_cast<chrono::seconds>(system_clock::now() - ord_test) > baseline_timeout) {
            cout << "Max timeout " << baseline_timeout.count() << " minutes elapsed, terminating test "<< test_id << endl;
            break;
        }

    }

    #if OUTPUT_ORDERINGS
    odstream.close();
    #endif

    const time_point<system_clock> ord_cleanup = system_clock::now();

    if (pmdir.empty()) {
        checkpoint_pop();
    }

    // cleanup backup files
    for (const auto &p : backup_map) {
        if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }

    if (!backup_dir.empty()) {
        fs::remove_all(backup_dir);
        error_if_exists(backup_dir);
    }

    BOOST_ASSERT(init_checkpoints == num_checkpoints());


    if (all_bugs) return ALL_INCONSISTENT;
    if (any_bugs) return HAS_BUGS;
    return NO_BUGS;
}

template <typename C>
void model_checker_state::create_permutation(
    const C &events,
    event_config &curr)
{
    // open all the files for syscall
    open_write_files();
    // apply the stores
    int order = 1;
    for (shared_ptr<trace_event> te : events) {
        if (te->is_store()) {
            do_store(te);
        }
        else if (mode_ == POSIX && te->is_register_file()) {
            do_register_file(te);
        }
        else if (mode_ == POSIX && te->is_unregister_file()) {
            do_unregister_file(te);
        }
        else {
            apply_trace_event(te);
        }
        curr[te->event_idx()] = order;
        order++;
    }

    close_write_files();
}

test_result model_checker_state::test_permutation(string note) {
    // fsync on the pmdir
    if (!pmdir.empty()) {
        int fd = open(pmdir.c_str(), O_RDONLY);
        if (fd == -1) {
            cerr << "File open failed!" << endl;
            cerr << string(strerror(errno)) << endl;
            exit(EXIT_FAILURE);
        }
        int rt = fsync(fd);
        if (rt) {
            cerr << "Test dir fsync failed!" << endl;
            cerr << string(strerror(errno)) << endl;
            exit(EXIT_FAILURE);
        }
        close(fd);
    }
    close_write_files();
    // output completed ops recorded in tid_to_op_id to a file pmdir/ops_completed
    if (!pmdir.empty() && op_tracing_) {
        // we now figure out what operations are completed
        vector<uint64_t> all_applied_event_ids;
        for (uint64_t i = 0; i < prefix_event_id; i++) {
            all_applied_event_ids.push_back(i);
        }
        // concat all_applied_event_ids and applied_event_ids
        all_applied_event_ids.insert(all_applied_event_ids.end(), applied_event_ids.begin(), applied_event_ids.end());
        
        output_ops_completed(all_applied_event_ids);
    }
    
    test_result res = run_checker();
    if (__glibc_unlikely(!res.valid())) {
        cerr << "invalid test result!\n";
        exit(EXIT_FAILURE);
    }
    res.note = note;
    open_write_files();

    if (mode_ == POSIX) {
        // close all the files
        // lock_guard<mutex> l(*stdout_mutex_);
        // cout << test_id << ":" << "[test_permutation] Closing all files" << endl;
        for (const auto &p : file_to_fd) {
            if (p.second != -1) {
                close(p.second);
                // cout << test_id << ":" << "Closed file " << fsfile_map[p.first] << " with fd " << p.second << endl;
            }
        }
    }
    return res;
}

test_result model_checker_state::test_permutation(
        const event_set &stores,
        int perm_id, string note, vector<int> &accessed_ids) {

    accessed_ids.clear();

    if (save_file_images) {
        fs::path image_output = construct_outdir_path(perm_id, "_pm_image");
        recursive_copy(pmdir, image_output);
    }

    fs::path stores_output = output_stores(stores);
    fs::path pintool_output = construct_outdir_path(perm_id, "_pinout");

    test_result res = run_recovery_observer_mmio(stores_output, pintool_output);
    if (__glibc_unlikely(!res.valid())) {
        cerr << "invalid test result!\n";
        exit(EXIT_FAILURE);
    }

    accessed_ids = get_accessed_stores(pintool_output);

    res.note = note;
    return res;
}


void model_checker_state::record_file_images(test_result &res) {
    for (const auto &p : pmfile_map) {
        // Try to find a full range
        const string &original_name = p.first;
        const fs::path &pmfile = p.second;

        bool found = false;
        for (const auto &range : file_memory_regions_[pmfile.string()]) {
            if (range.upper() - range.lower() == fs::file_size(pmfile)) {
                if (res.ret_code != 0) {
                    assert(res.contains_bug());
                    const raw_data &contents = checkpoints_[range].back();
                    res.file_images[original_name] = gzip::compress(contents);
                } else {
                    // empty
                    res.file_images[original_name] = raw_data();
                }

                found = true;
                break;
            }
        }

        if (!found) {
            cerr << "don't know what to do!\n";
            exit(EXIT_FAILURE);
        }
    }
}

test_result model_checker_state::run_checker(void) {
    test_result res;

// debugging info
#if 1
    if (!daemon_args.empty()) {
        bp::ipstream out_is, err_is;
        string daemon_out;
        bp::child d = start_command(daemon_args, out_is, err_is);
        // Wait for daemon to start up
        // d.wait_for(chrono::seconds(1));
        std::this_thread::sleep_for(chrono::seconds(5));

        // print checker args
        // cout << "Checker args: ";
        // for (const auto &arg : checker_args) {
        //     cout << arg << " ";
        // }
        // cout << endl;
        res.ret_code = run_command(checker_args, res.output, timeout);

        (void)finish_command(d, out_is, err_is, daemon_out, timeout);
        res.output += daemon_out;
    } else {
        // print checker args
        // cout << "Checker args: ";
        // for (const auto &arg : checker_args) {
        //     cout << arg << " ";
        // }
        // cout << endl;
        res.ret_code = run_command(checker_args, res.output, timeout);
    }
#else
    if (!daemon_args.empty()) {
        bp::child d = start_command(daemon_args);
        // Wait for daemon to start up
        // iangneal: boost bug
        // d.wait_for(chrono::seconds(1));
        std::this_thread::sleep_for(chrono::seconds(1));

        res.ret_code = run_command(checker_args, timeout);

        (void)finish_command(d, timeout);
    } else {
        res.ret_code = run_command(checker_args, timeout);
    }
#endif

    if (save_file_images) {
        record_file_images(res);
    }

    assert(res.valid());
    return res;
}


model_checker_code model_checker_state::test_possible_orderings(
    const event_config &init_config,
    const std::set<std::shared_ptr<trace_event>> &stores,
    string note = "") {

    return test_possible_orderings(
        init_config, stores, std::set<std::shared_ptr<trace_event>>(), note);
}


model_checker_code model_checker_state::test_possible_orderings(
    const event_config &init_config,
    const std::set<std::shared_ptr<trace_event>> &stores,
    const std::set<std::shared_ptr<trace_event>> &syscalls,
    std::string note="")
{
    set<permutation_t> tested_permutations;

    if (stores.empty()) {
        return NO_BUGS;
    }

    const time_point<system_clock> ord_start = system_clock::now();

    bool any_bugs = false;
    bool all_bugs = true;

    event_config baseline = init_config;
    for (const auto &te : stores) {
        baseline[te->store_id()] = -1;
    }

    // Initialize the file
    fs::path resfile = construct_outdir_path(".csv");
    fs::ofstream rstream(resfile);

    for (const auto &p : baseline) {
        rstream << p.first << ",";
    }
    // -- now, the fields in the wahoo
    rstream << "ret_code,message,note,timestamp";
    if (save_file_images) {
        for (const auto &p : file_memory_regions_) {
            rstream << "," << p.first;
        }
    }
    rstream << "\n";
    rstream.flush();

    // output ordering generated for debugging
    #if OUTPUT_ORDERINGS
    fs::path orderingfile = construct_outdir_path("_ordering.txt");
    fs::ofstream odstream(orderingfile);
    #endif

    if (pmdir.empty()) {
        checkpoint_push();
    } else {
        backup_pmdir(true);
    }

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_start) / 1s
        << " to setup!\n";
    #endif

    const time_point<system_clock> ord_test = system_clock::now();

    int perm_id = 0;
    vector<int> accessed_ids;

    // Step 1: do an initial Pin tool pass
    event_config curr = baseline;

    tested_permutations.insert(permutation_t());

    create_permutation(*tested_permutations.begin(), curr);

    // disable DPOR for now
    // test_result res = test_permutation(stores, perm_id, note, accessed_ids);
    for (const auto & store : stores) {
        accessed_ids.push_back(store->store_id());
    }
    test_result res = test_permutation(note);

    auto elapsed = duration_cast<seconds>(system_clock::now() - start_time);
    dump_test_result(rstream, curr, res, elapsed);
    rstream.flush();

    all_bugs = res.contains_bug() && all_bugs;
    any_bugs = res.contains_bug() || any_bugs;

    #if OUTPUT_ORDERINGS
    odstream<<"Perm "<<perm_id<<": "<<endl<<endl;
    odstream<<"====="<<endl;
    print_orderings(permutation_t(), odstream);
    odstream.flush();
    #endif

    if (pmdir.empty()) {
        restore();
    } else {
        restore_pmdir(true);
    }

    perm_id++;

    // Step 2: get all read stores and formulate a new stores_list,
    // these are ones that we must test
    set<shared_ptr<trace_event>> accessed_stores_set;
    // -- ordered map for sanity
    map<int, int> access_map;

    // -- init access_map to all 0's
    for (const auto &store : stores) {
        access_map[store->store_id()] = 0;
    }

    // -- As we access more stores, update the map to add more 1's
    auto update_accessed = [&] (void) {
        for (const auto &store : stores) {
            auto fiter = find(accessed_ids.begin(), accessed_ids.end(),
                              store->store_id());
            if (fiter != accessed_ids.end()) {
                access_map[store->store_id()] = 1;
                accessed_stores_set.insert(store);
            }
        }
    };
    // -- go ahead and do an update
    update_accessed();

    // -- We also don't want to re-test any permutations, so let's make a little
    // function for this too
    auto generate_new_orderings = [&] (void) -> set<permutation_t> {
        return generate_orderings(
            accessed_stores_set, syscalls, tested_permutations);
    };

    // Step 3: generate new perm and start testing
    set<permutation_t> perms = generate_new_orderings();

     while (!perms.empty()) {

        // reduces nested loops.
        permutation_t perm = *perms.begin();
        perms.erase(perms.begin());

        #if OUTPUT_ORDERINGS
        odstream<<"Perm "<<perm_id<<": "<<endl<<endl;
        odstream<<"====="<<endl;
        print_orderings(perm, odstream);
        odstream.flush();
        #endif

        event_config curr = baseline;

            create_permutation(perm, curr);

        // disable DPOR for now
        // test_result res = test_permutation(stores, perm_id, note, accessed_ids); 

        for (const auto & te : perm) {
            if (te->is_store()) {
                accessed_ids.push_back(te->store_id());
            }
        }
        test_result res = test_permutation(note);

        auto elapsed = duration_cast<seconds>(system_clock::now() - start_time);
        dump_test_result(rstream, curr, res, elapsed);
        rstream.flush();

        all_bugs = res.contains_bug() && all_bugs;
        any_bugs = res.contains_bug() || any_bugs;

        if (pmdir.empty()) {
            restore();
        } else {
            restore_pmdir(true);
        }

        #if OUTPUT_ORDERINGS
        odstream<<"====="<<endl;
        odstream<<endl;
        odstream.flush();
        #endif

        // Now, do our DPOR metadata update.
        // No need to check if we have exhausted the orderings
        if (accessed_stores_set != stores) {
            update_accessed();
            set<permutation_t> new_perms = generate_new_orderings();
            perms.insert(new_perms.begin(), new_perms.end());
        }


        perm_id++;
    }
    // Let's output the accessed map too
    fs::path accessfile = construct_outdir_path("_accesses.csv");
    fs::ofstream oastream(accessfile);
    oastream << "store_id,is_accessed" << endl;
    for (const auto &p : access_map) {
        oastream << p.first << "," << p.second << endl;
    }
    oastream.flush();
    oastream.close();

    #if OUTPUT_ORDERINGS
    odstream.close();
    #endif

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_test) / 1s
        << " seconds to test normal sequence!\n";
    #endif

    #ifndef INLINE_DELTA_DEBUGGING
    #error "must define INLINE_DELTA_DEBUGGING flag to 1/0!"
    #endif

////////

    #if INLINE_DELTA_DEBUGGING
    const time_point<system_clock> ord_delta = system_clock::now();
    // If all of these are bugs, do the delta debugging
    // iangneal: Just add to a "note" field instead of a new file?
    int first_store_id = (*stores.begin())->store_id();
    // Can't delta debug if we're already at store 0.
    if (all_bugs && first_store_id > 0) {
        test_result res, t;
        int res_mid = -1;

        // Now, start searching
        int left = 0, right = first_store_id;
        while (left < right) {
            int mid = (left + right) / 2;
            // First, reset the files
            wipe_files();
            for (int i = 0; i <= mid; ++i) {
                const auto &te = event_trace.stores()[i];
                do_store(te);
            }

            // sync_files();
            t = run_checker();
            if (__glibc_unlikely(!t.valid())) {
                cerr << "invalid delta debugging intermediate result!\n";
                exit(EXIT_FAILURE);
            }

            if (!t.contains_bug()) {
                res = t;
                res_mid = mid;
                // Go up
                left = mid + 1;
            } else {
                // Go down
                right = mid - 1;
            }
        }

        if (!res.valid()) {
            res = t;
            if (__glibc_unlikely(!res.valid())) {
                cerr << "invalid delta debugging final result!\n";
                cerr << "\tstart_left = 0, start_right = " << first_store_id << endl;
                exit(EXIT_FAILURE);
            }
        }

        all_bugs = res.contains_bug() && all_bugs;
        any_bugs = res.contains_bug() || any_bugs;

        time_t timestamp = std::chrono::system_clock::to_time_t(system_clock::now())

        auto time_elapsed = duration_cast<seconds>(system_clock::now() - start_time);

        if (!res.contains_bug()) {
            res.note = note + string("\ndelta debugging store: ") + std::to_string(res_mid);
            dump_test_result(rstream, baseline, res, time_elapsed);
        } else {
            res.note = note + string("\ndelta debugging failed: ") + std::to_string(res_mid);
            dump_test_result(rstream, baseline, res, time_elapsed);
        }
    }

    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_delta) / 1s
        << " seconds to delta debug!\n";

    #endif

    const time_point<system_clock> ord_cleanup = system_clock::now();

    if (pmdir.empty()) {
        checkpoint_pop();
    }

    // cleanup backup files
    for (const auto &p : backup_map) {
        if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }

    if (!backup_dir.empty()) {
        fs::remove_all(backup_dir);
        error_if_exists(backup_dir);
    }

    rstream.flush();
    rstream.close();

    #if DEBUG_PRINTS
    cerr << "\t[" << test_id << "] " << __FUNCTION__ <<
        ": Took " << (system_clock::now() - ord_cleanup) / 1s
        << " seconds to cleanup!\n";
    #endif

    test_case_id_++;

    if (all_bugs) return ALL_INCONSISTENT;
    if (any_bugs) return HAS_BUGS;
    return NO_BUGS;
}

template <typename C>
fs::path model_checker_state::output_stores(
    const C &event_list)
{
    fs::path store_output = construct_outdir_path("_stores", true);
    if (!fs::exists(store_output)) {
        fs::ofstream f(store_output);
        for (const auto &te : event_list) {
            // only need size and addr
            if (te->is_store()) {
                f << te->store_id() << ",";
                f << te->address << ",";
                f << te->size << "\n";
            }
        }
        f.close();
    }

    return store_output;
}

std::list<std::string> model_checker_state::get_recovery_observer_mmio_args(
        fs::path input_file_path, fs::path output_file_path) {

    std::list<std::string> pintool_args{PIN_PATH, "-t", PINTOOL_DPOR_MMIO_PATH, "-i",
        input_file_path.c_str(), "-o", output_file_path.c_str(), "--"};

    // Now, set up the pintool args (based on the checker arguments)
    pintool_args.insert(pintool_args.end(),
        checker_args.begin(), checker_args.end());

    return pintool_args;
}

std::list<std::string> model_checker_state::get_recovery_observer_posix_args(
        fs::path output_file_path) {

    std::list<std::string> pintool_args{PIN_PATH, "-t", PINTOOL_DPOR_POSIX_PATH, "-o", output_file_path.c_str(),"-tf", pmdir.string(), "-record-value", "0", "--"};

    // Now, set up the pintool args (based on the checker arguments)
    pintool_args.insert(pintool_args.end(),
        checker_args.begin(), checker_args.end());

    return pintool_args;
}


test_result model_checker_state::run_recovery_observer_mmio(
        fs::path stores_output, fs::path pintool_output) {

    auto pintool_args = get_recovery_observer_mmio_args(stores_output, pintool_output);

    test_result res;
    res.ret_code = run_command(pintool_args, res.output, timeout);

    return res;
}

test_result model_checker_state::run_recovery_observer_posix(
        fs::path pintool_output) {

    auto pintool_args = get_recovery_observer_posix_args(pintool_output);

    test_result res;
    res.ret_code = run_command(pintool_args, res.output, timeout);

    return res;
}


vector<int> model_checker_state::get_accessed_stores(fs::path pintool_output) {

    fs::ifstream ifp(pintool_output);
    string line;
    vector<int> accessed_ids;

    // format should be "store_id, is_accessed"
    while (getline(ifp, line)) {
        vector<string> pieces;
        boost::split(pieces, line, boost::is_any_of(","));
        if (pieces.size() != 2) {
            cerr << __PRETTY_FUNCTION__ << ": unexpected format \"" << line << "\"" << endl;
            exit(EXIT_FAILURE);
        }

        size_t store_id = boost::lexical_cast<size_t>(pieces[0]);
        bool is_accessed = boost::lexical_cast<bool>(pieces[1]);

        if (is_accessed) accessed_ids.push_back(store_id);
    }

    return accessed_ids;
}

void model_checker_state::record_lseek_offset(void) {
    // print current file to fd map
    // lock_guard<mutex> l(*stdout_mutex_);
    // cout << test_id << ":" << "[record_lseek_offset] Current file to fd map: " << endl;
    // for (const auto pair : file_to_fd) {
    //     cout  << test_id << ":" << "file: " << pair.first << " fd: " << pair.second << endl;
    // }
    for (const auto pair : fd_to_fd) {
        int trace_fd = pair.first;
        int pathfinder_fd = pair.second;
        if (pathfinder_fd == -1) continue;
        int offset = lseek(pathfinder_fd, 0, SEEK_CUR);
        if (offset == -1) {
            // search which file is this fd
            for (const auto p : file_to_fd) {
                if (p.second == pathfinder_fd) {
                    cerr << test_id << ":" << "lseek failed!" << "failed fd: " << pathfinder_fd << " file: " << fsfile_map[p.first] << endl;
                    cerr << string(strerror(errno)) << endl;
                    exit(EXIT_FAILURE);
                }
            }
            // cerr << "lseek failed!" << "failed fd: " << pathfinder_fd << endl;
            // cerr << string(strerror(errno)) << endl;
            // exit(EXIT_FAILURE);
        }
        lseek_map[trace_fd] = offset;
    
    }
}

void model_checker_state::apply_lseek_offset(void) {
    for (const auto pair : lseek_map) {
        int trace_fd = pair.first;
        int offset = pair.second;
        int pathfinder_fd = fd_to_fd[trace_fd];
        if (pathfinder_fd == -1) continue;
        int rt = lseek(pathfinder_fd, offset, SEEK_SET);
        if (rt == -1) {
            cerr << "lseek failed!" << endl;
            cerr << string(strerror(errno)) << endl;
            exit(EXIT_FAILURE);
        }
    }
}

void model_checker_state::setup_init_state(int until) {
    if (!setup_args.empty()) {
        string output;
        int ret = run_command(setup_args, output, timeout);
        if (ret != 0) {
            cerr << "Setup failed! " << ret << "\n";
            cerr << output << "\n";
            exit(EXIT_FAILURE);
        }
    }

    /**
     * @brief Set up the initial state of the PM file(s).
     *
     * Go ahead and track transients to see if anything previous messes with
     * our operations. Clear after each fence, though
     */
    // Want this ordered

    // for PM and MMIO we still open all files at the beginning
    open_write_files();

    size_t init_checkpoints = num_checkpoints();
    
    assert(until <= event_trace.events().size());
    for (int i = 0; i < until; ++i) {
        const shared_ptr<trace_event> &te = event_trace.events()[i];
        if (te->is_register_file()) {
            // Record it for backup recovery later
            mmio_events.push_back(te);
            (void)do_register_file(te);
        } 
        else if (te->is_store()) {
            // Do the store!
            bool translated = do_store(te);
        }
        else if (mode_ == POSIX && te->is_unregister_file()) {
            mmio_events.push_back(te);
            do_unregister_file(te);
        }
        else if (mode_ == POSIX && te->is_open()) {
            mmio_events.push_back(te);
            do_open(te);
        }
        else if (mode_ == POSIX && te->is_creat()) {
            mmio_events.push_back(te);
            do_creat(te);
        }
        else if (mode_ == POSIX && te->is_close()) {
            mmio_events.push_back(te);
            do_close(te);
        }
        else {
            apply_trace_event(te);
        }
    }

    prefix_event_id = until;

    record_lseek_offset();

    close_write_files();
}


void model_checker_state::run_pm(promise<model_checker_code> &&output_res) {
    model_checker_code res = NO_BUGS;
    event_config econfig;

    const time_point<system_clock> run_start = system_clock::now();

    setup_init_state(event_idxs.front());

    // if (!nfence) {
    //     dirty.clear();
    // }

    #if DEBUG_PRINTS
    using namespace std::literals;
    cerr << "[" << test_id << "] Took " << (system_clock::now() - run_start) / 1s
        << " seconds to set up!\n";
    #endif

    const time_point<system_clock> run_test = system_clock::now();

    #ifndef TRANSIENT_ELISION
    #error "must define TRANSIENT_ELISION flag to 1/0!"
    #endif

    #ifndef TEST_ELISION
    #error "must define TEST_ELISION flag to 1/0!"
    #endif


    /**
     * @brief Now, run the tests.
     */
    #if TRANSIENT_ELISION || TEST_ELISION
    /**
     * Track which indexes are being added to the testing set. Only run tests
     * if a new event is added.
     *
     */
    unordered_set<int> all_idxs(event_idxs.begin(), event_idxs.end());
    unordered_set<int> new_idxs;
    #endif

    set<shared_ptr<trace_event>> all_syscalls;

    typedef std::set<shared_ptr<trace_event>> event_set;
    icl::interval_map<uint64_t, event_set> dirty, flushed;
    size_t nfence = 0;

    /**
     * We're given the stores we're interested in. We actually want to start testing
     * at the last fence, so we get the initial transients.
     *
     * Do the same for last event.
     */
    int f_range = (event_idxs.back() - event_idxs.front()) / 2;
    int earliest_event = std::max(0, event_idxs.front() - f_range);
    int latest_event = std::min((int)event_trace.events().size(), event_idxs.back() + f_range);
    int first_event = event_idxs.front();
    int last_event = event_idxs.back();

    for (int i = first_event; i <= last_event; ++i) {
        shared_ptr<trace_event> te = event_trace.events()[i];
        #if TRANSIENT_ELISION || TEST_ELISION
        if (all_idxs.count(i)) {
            new_idxs.insert(i);
        }
        #endif

        switch(te->type) {
            case STORE: {
                // Add to the dirty map.
                dirty.add(make_pair(te->cacheline_range(), event_set({te})));
                break;
            }
            case FLUSH: {
                // Move from dirty to flushed.
                auto it = dirty.find(te->cacheline_range());
                if (it != dirty.end()) {
                    flushed.add(*it);
                    dirty.subtract(*it);
                }

                break;
            }
            case FENCE: {
                // Now, we do the testing.

                // -- Combine everything in flushed and dirty (transient)
                event_set all_stores;
                for (const auto &p : dirty) {
                    all_stores.insert(p.second.begin(), p.second.end());
                }
                for (const auto &p : flushed) {
                    all_stores.insert(p.second.begin(), p.second.end());
                }

                #if TEST_ELISION
                // test IF there are some stores we want to test
                if (!new_idxs.empty()) {
                    // model_checker_code code = test_possible_orderings(econfig, all_stores);
                    model_checker_code code;

                    if (ttype == EXHAUSTIVE) {
                        code = test_exhaustive_orderings(econfig, all_stores,
                            all_syscalls, "end exhaustive test (w/syscalls)");
                    } else if (ttype == LINEAR) {
                        code = test_linear_orderings(econfig, all_stores,
                            all_syscalls, "end linear test (w/syscalls)");
                    } else {
                        code = test_possible_orderings(econfig, all_stores,
                            all_syscalls, "end test (w/syscalls)");
                    }
                    update_code(res, code);
                    new_idxs.clear();
                }
                #else
                // model_checker_code code = test_possible_orderings(econfig, all_stores);
                model_checker_code code = test_possible_orderings(econfig, all_stores, all_syscalls);
                update_code(res, code);
                #endif

                /**
                 * @brief Clear the flush tree. This means, for all flushed stores,
                 * apply them and then set the config appropriately
                 *
                 */

                // TODO: there is still some sneaky-peaky inconsistency here to fix for exhaustive testing
                // -- ensure we update the config too
                for (const auto &p : flushed) {
                    for (const auto &te : p.second) {
                        do_store(te);
                        // econfig[te->event_idx()] = 0;
                        econfig[te->store_id()] = 0;
                    }
                }
                flushed.clear();
                // apply all syscalls until this point
                open_write_files();
                for (auto te : all_syscalls) {
                    apply_trace_event(te);
                }
                all_syscalls.clear();
                close_write_files();

                #if TRANSIENT_ELISION && TEST_ELISION
                /**
                 * @brief We will also clear all the transient stores that aren't
                 * in the set we are interested in; in other words, don't worry
                 * about long-life transient stores that aren't a part of the
                 * update mechanism we want to test. The prevents us from testing
                 * everything across the program, and allows us to test reorderings
                 * of a specific update mechanism across a program.
                 *
                 */
                icl::interval_map<uint64_t, event_set> remaining_stores;
                for (const auto &p : dirty) {
                    for (const auto &te : p.second) {
                        if (all_idxs.count(te->event_idx())) {
                            remaining_stores.add(p);
                        } else {
                            // Treat it as an applied store.
                            do_store(te);
                            // econfig[te->event_idx()] = 0;
                            econfig[te->store_id()] = 0;
                        }
                    }
                }
                // cerr << "Removing " << dirty.size() - remaining_stores.size()
                //     << " transient stores from testing!\n";
                dirty = remaining_stores;
                #elif TRANSIENT_ELISION
                dirty.clear();
                #endif

                // Don't worry about checkpointing here.

                break;
            }
            case REGISTER_FILE: {
                // Record it for backup recovery later
                mmio_events.push_back(te);

                auto range = do_register_file(te);
                break;
            }
            case WRITE:
            case PWRITEV:
            case FALLOCATE:
            case FTRUNCATE:
            case PWRITE64:
            case WRITEV:
            case LSEEK:
            case RENAME:
            case UNLINK:
            case FSYNC:
            case FDATASYNC:
            case MSYNC:
                all_syscalls.insert(te);
                break;
            case REGISTER_WRITE_FILE:
                break;
            case UNREGISTER_FILE:
            // TODO: fix this
                break;
            default:
                cerr << "Unhandled event!\n\t" << te->type << "\n";
                exit(EXIT_FAILURE);
        }
    }

    // apply all syscalls that are not cleared
    open_write_files();
    for (auto te : all_syscalls) {
        apply_trace_event(te);
    }
    all_syscalls.clear();
    close_write_files();

    // Now, if there is anything left in dirty/flushed, go ahead and test as well.
    // -- Combine everything in flushed and dirty (transient)
    event_set all_stores;
    for (const auto &p : dirty) {
        all_stores.insert(p.second.begin(), p.second.end());
    }
    for (const auto &p : flushed) {
        all_stores.insert(p.second.begin(), p.second.end());
    }

    #if TEST_ELISION
    // test IF there are some stores we want to test
    if (!new_idxs.empty()) {
        // cerr << "End testing!\n";
        model_checker_code code;
        if (ttype == EXHAUSTIVE) {
            code = test_exhaustive_orderings(econfig, all_stores, event_set(),
            string("end exhaustive test"));
            update_code(res, code);
            goto end;
        }
        else if (ttype == LINEAR) {
            code = test_linear_orderings(econfig, all_stores, event_set(),
            string("end linear test"));
            update_code(res, code);
            goto end;
        }
        else {
            code = test_possible_orderings(econfig, all_stores, "end test");
            update_code(res, code);
        }
        new_idxs.clear();
    }
    #else
    model_checker_code code = test_possible_orderings(econfig, all_stores, "end test");
    update_code(res, code);
    #endif

    /**
     * If we still have transient stores, let's see if they interfere with anything
     * else.
     */
    #if 1
    if (!dirty.empty() || !flushed.empty()) {
        int max_remaining = std::min((int)event_trace.events().size(), last_event + f_range);
        // cerr << "EXTENSION FOR VERTS IN RANGE " << event_idxs.front() << ", "
        //     << event_idxs.back() << endl;
            // << "\n\textending by going as far as event " << max_remaining << endl;

        bool finish_testing = false;

        open_write_files();

        for (int i = last_event + 1; i < (int)event_trace.events().size(); ++i) {
            if (dirty.empty()) {
                // cerr << "\tdone at event " << i << " (empty)" << endl;
                break;
            }
            if (finish_testing) {
                // cerr << "\tdone at event " << i << " (found a bug!)" << endl;
                break;
            }

            shared_ptr<trace_event> te = event_trace.events()[i];

            switch(te->type) {
                case STORE: {

                    #if 1
                    // Remove the transient range
                    auto it = dirty.find(te->cacheline_range());
                    if (it != dirty.end()) {
                        for (const auto &s : it->second) {
                            do_store(s);
                        }
                        dirty.subtract(*it);
                    }
                    // And flushed
                    it = flushed.find(te->cacheline_range());
                    if (it != flushed.end()) {
                        for (const auto &s : it->second) {
                            do_store(s);
                        }
                        flushed.subtract(*it);
                    }
                    // Do the store directly.
                    do_store(te);
                    #else
                    dirty.add(make_pair(te->cacheline_range(), event_set({te})));
                    #endif

                    break;
                }
                case FLUSH: {
                    // Move from dirty to flushed.
                    auto it = dirty.find(te->cacheline_range());
                    if (it != dirty.end()) {
                        flushed.add(*it);
                        dirty.subtract(*it);
                    }

                    break;
                }
                case FENCE: {
                    // Now, we do the testing.

                    // -- Combine everything in flushed and dirty (transient)
                    event_set all_stores;
                    for (const auto &p : dirty) {
                        all_stores.insert(p.second.begin(), p.second.end());
                    }
                    for (const auto &p : flushed) {
                        all_stores.insert(p.second.begin(), p.second.end());
                    }
                    // TODO: generate exhaustive test here?
                    model_checker_code code = test_possible_orderings(econfig, all_stores,
                        string("Transient reach testing @ event ") + to_string(te->timestamp));
                    update_code(res, code);
                    #if 0
                    if (has_bugs(code)) {
                        finish_testing = true;
                    }

                    /**
                     * @brief Clear the flush tree. This means, for all flushed stores,
                     * apply them and then set the config appropriately
                     *
                     */
                    // -- ensure we update the config too
                    for (const auto &p : flushed) {
                        for (const auto &te : p.second) {
                            do_store(te);
                            // econfig[te->event_idx()] = 0;
                            econfig[te->store_id()] = 0;
                        }
                    }
                    flushed.clear();
                    #else
                    finish_testing = true;
                    #endif


                    break;
                }
                case REGISTER_FILE: {
                    // Record it for backup recovery later
                    mmio_events.push_back(te);

                    auto range = do_register_file(te);
                    break;
                }
                case WRITE:
                    do_write(te);
                    break;
                case PWRITEV:
                    do_pwritev(te);
                    break;
                case FALLOCATE:
                    do_fallocate(te);
                    break;
                case FTRUNCATE:
                    do_ftruncate(te);
                    break;
                case PWRITE64:
                    do_pwrite64(te);
                    break;
                case WRITEV:
                    do_writev(te);
                    break;
                case LSEEK:
                    do_lseek(te);
                    break;
                case RENAME:
                    do_rename(te);
                    break;
                case UNLINK:
                    do_unlink(te);
                    break;
                case FSYNC:
                    do_fsync(te);
                    break;
                case FDATASYNC:
                    do_fdatasync(te);
                    break;
                case MSYNC:
                    do_msync(te);
                    break;
                case UNREGISTER_FILE:
                // TODO: fix this
                    break;
                case REGISTER_WRITE_FILE:
                    break;
                default:
                    cerr << "Unhandled event!\n\t" << te->type << "\n";
                    exit(EXIT_FAILURE);
            }
        }
        close_write_files();
    }
    #endif

    #if DEBUG_PRINTS
    cerr << "[" << test_id << "] Took " << (system_clock::now() - run_test) / 1s
        << " seconds to test!\n";
    #endif

    /**
     * Testing is officially over. We now need to cleanup.
    */

end:

    // Sanity check for memory consumption---make sure we don't have extra checkpoints.
    // BOOST_ASSERT(init_checkpoints == num_checkpoints());

    if (num_checkpoints() != 0) {
        cerr << __FILE__ << " @ line " << __LINE__ << ": # checkpoints is " <<
            num_checkpoints() << " at the end!\n";
        exit(EXIT_FAILURE);
    }

    for (const auto &range : mapped_) {
        (void)munmap((void*)range.lower(), range.upper() - range.lower());
    }

    if (!cleanup_args.empty()) {
        string output;
        int ret = run_command(cleanup_args, output);
        if (ret != 0) {
            cerr << "Cleanup failed! " << ret << "\n";
            cerr << output << "\n";
            exit(EXIT_FAILURE);
        }
    }

    for (const auto &p : pmfile_map) {
        if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }

    for (const auto &p : fsfile_map) {
        // iangneal: allow applications to open /dev/null and such, but don't
        // subsequently attempt to delete these files.
        if (fs::exists(p.second) && p.second.native().rfind("/dev/", 0) != 0) {
            // we don't support dir creation for now, so if it is a dir it must be the pmdir, skip it now
            if (fs::is_directory(p.second)) {
                continue;
            }
            fs::remove(p.second);
        }
    }

    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        error_if_exists(pmdir);
    }

    output_res.set_value(res);
}

test_result model_checker_state::process_permutation(ostream& rstream, const vector<int>& perm, const string& test_type) {
    backup_pmdir(true);
    // Other parts of the function remain unchanged...
    event_config curr;
    vector<shared_ptr<trace_event>> events_list;
    for (const auto idx : perm) {
        events_list.push_back(event_trace.events()[idx]);
        applied_event_ids.push_back(idx);
    }
    create_permutation(events_list, curr);
    test_result res = test_permutation(test_type);
    // Fill up curr with event_idx -> 0/1
    // for (const auto idx : event_idxs) {
    //     if (find(perm.begin(), perm.end(), idx) != perm.end()) {
    //         curr[idx] = 1;
    //     } else {
    //         curr[idx] = 0;
    //     }
    // }
    for (const auto &p : curr) {
        rstream << p.first << ",";
    }
    dump_test_result(rstream, curr, res, duration_cast<seconds>(system_clock::now() - start_time));
    rstream.flush();

    // TODO: do not restore dir status because currently one test corresponds to one ordering
    // if (pmdir.empty()) {
    //     restore();
    // } else {
    //     restore_pmdir(true);
    // }
    // fs.reset_state();
    // applied_event_ids.clear();

    
    return res;
}

void model_checker_state::run_posix(promise<model_checker_code> &&output_res) {
    if (setup_until) {
        setup_init_state(*setup_until);
    }
    else {
        setup_init_state(event_idxs.front());
    }

    // for Persevere
    if (persevere_) {
        // we run the recovery observer here
        // we only support crash state calculation now so we will return after the pin tool finishes, the pathfinder engine will handle the calculation
        int perm_id = 0;
        fs::path pintool_output = construct_outdir_path(perm_id, "_pinout");

        test_result pin_res = run_recovery_observer_posix(pintool_output);
        if (__glibc_unlikely(!pin_res.valid())) {
            cerr << "invalid test result!\n";
            exit(EXIT_FAILURE);
        }

        model_checker_code res = NO_BUGS;
        output_res.set_value(res);
        return;
    }

    model_checker_code res = NO_BUGS;
    event_config econfig;

    bool all_bugs = true;
    bool any_bugs = false;

    // prepare output file
    stringstream ss;
    ss << test_id << "_" << test_case_id_++ << ".csv";
    ss.flush();
    // Initialize the file
    fs::path resfile = outdir / ss.str();
    assert(!fs::exists(resfile));
    fs::ofstream rstream(resfile);
    // -- now, the fields in the wahoo
    rstream << "ret_code,message,note,timestamp(posix mode)" << endl;
    rstream.flush();

    const time_point<system_clock> run_start = system_clock::now();

    test_result test_res = process_permutation(rstream, event_idxs, "posix");
    all_bugs = test_res.contains_bug() && all_bugs;
    any_bugs = test_res.contains_bug() || any_bugs;


    // Yile: do not perumute orderings when event_idxs is provided
    // TODO: redesign fsync_exhaustive, fsync_linear baseline?

    // if (event_idxs.size() >= 2000) goto end;

    // // permute all events within events_idxs, return a set of permutations represented in vector<int>
    // if (event_idxs.size() <= 10) {
    //     unsigned int powerset_size = (1 << event_idxs.size());
    //     for (unsigned int i = 0; i < powerset_size; i++) {
    //         vector<int> perm;
    //         for (unsigned int j = 0; j < event_idxs.size(); j++) {
    //             if (i & (1 << j)) {
    //                 perm.push_back(event_idxs[j]);
    //             }
    //         }
    //         test_result res = process_permutation(rstream, perm, "fsync_exhaustive");
    //         all_bugs = res.contains_bug() && all_bugs;
    //         any_bugs = res.contains_bug() || any_bugs;
            
    //     }
    // }
    // else {
    //     vector<int> perm;
    //     vector<vector<int>> perms;
    //     perms.push_back(perm);
    //     for (const auto idx : event_idxs) {
    //         perm.push_back(idx);
    //         perms.push_back(perm);
    //     }
    //     for (const auto perm : perms) {
    //         test_result res = process_permutation(rstream, perm, "fsync_linear");
    //         all_bugs = res.contains_bug() && all_bugs;
    //         any_bugs = res.contains_bug() || any_bugs;
    //     }

    // }

end:

    if (all_bugs) {
        res = ALL_INCONSISTENT;
    } else if (any_bugs) {
        res = HAS_BUGS;
    } else {
        res = NO_BUGS;
    }

    rstream.close();

    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        error_if_exists(pmdir);
    }
    if (!backup_dir.empty()) {
        fs::remove_all(backup_dir);
        error_if_exists(backup_dir);
    }

    output_res.set_value(res);
}

void model_checker_state::run_posix_with_orders(promise<model_checker_code> &&output_res) {
    // get the min index from set<set<vertex>> all_event_orders
    int min_idx = INT_MAX;
    for (const auto &order : all_event_orders) {
        for (const auto &idx : order) {
            min_idx = std::min(min_idx, idx);
        }
    }
    setup_init_state(min_idx);

    model_checker_code res = NO_BUGS;
    event_config econfig;

    bool all_bugs = true;
    bool any_bugs = false;

    // prepare output file
    stringstream ss;
    ss << test_id << "_" << test_case_id_++ << ".csv";
    ss.flush();
    // Initialize the file
    fs::path resfile = outdir / ss.str();
    assert(!fs::exists(resfile));
    fs::ofstream rstream(resfile);
    // -- now, the fields in the wahoo
    rstream << "ret_code,message,note,timestamp(posix mode)" << endl;
    rstream.flush();

    const time_point<system_clock> run_start = system_clock::now();


    // get the order from all orders and construct events
    for (const auto order : all_event_orders) {
        test_result res = process_permutation(rstream, order, "posix");
        all_bugs = res.contains_bug() && all_bugs;
        any_bugs = res.contains_bug() || any_bugs;
    }


end:

    if (all_bugs) {
        res = ALL_INCONSISTENT;
    } else if (any_bugs) {
        res = HAS_BUGS;
    } else {
        res = NO_BUGS;
    }

    rstream.close();

    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        error_if_exists(pmdir);
    }
    if (!backup_dir.empty()) {
        fs::remove_all(backup_dir);
        error_if_exists(backup_dir);
    }
    
    output_res.set_value(res);
}

void model_checker_state::run_sanity_check(promise<model_checker_code> &&output_res) {
    model_checker_code res = NO_BUGS;
    event_config econfig;

    const time_point<system_clock> run_start = system_clock::now();

    if (!setup_args.empty()) {
        string output;
        int ret = run_command(setup_args, output, timeout);
        // print setup args
        cout << "Setup args: ";
        for (const auto &arg : setup_args) {
            cout << arg << " ";
        }
        cout << endl;
        if (ret != 0) {
            cerr << "Setup failed! " << ret << "\n";
            cerr << output << "\n";
            exit(EXIT_FAILURE);
        }
    }

    // size_t init_checkpoints = num_checkpoints();

    open_write_files();

    for (int i = 0; i < event_trace.events().size(); ++i) {
        const shared_ptr<trace_event> &te = event_trace.events()[i];
        if (te->is_register_file()) {
            // If it is the file for fs write, skip it
            // mmap also calls open, as a temp work-around just skip zero size map
            // if (fsfile_map.find(te->file_path) != fsfile_map.end() || !te->size) continue;

            auto range = do_register_file(te);
        }

        if (te->is_store()) {
            // Do the store!
            bool translated = do_store(te);
        } else if (te->is_flush()) {
            // Nothing for now
        } else if (te->is_fence()) {
            // Nothing for now
        } else {
            apply_trace_event(te);
        }
    }

    for (const auto &range : mapped_) {
        (void)munmap((void*)range.lower(), range.upper() - range.lower());
    }

    close_write_files();

    if (!pmdir.empty() && op_tracing_) {
        vector<uint64_t> all_applied_event_ids;
        for (const auto &te : event_trace.events()) {
            all_applied_event_ids.push_back(te->event_idx());
        }
        output_ops_completed(all_applied_event_ids);
    }

    test_result checker_res = run_checker();
    cout << "Checker output: " << endl;
    cout << checker_res.output << endl;

    if (checker_res.contains_bug()) res = HAS_BUGS;

end:

    // BOOST_ASSERT(init_checkpoints == num_checkpoints());

    if (!cleanup_args.empty()) {
        string output;
        int ret = run_command(cleanup_args, output);
        if (ret != 0) {
            cerr << "Cleanup failed! " << ret << "\n";
            cerr << output << "\n";
            exit(EXIT_FAILURE);
        }
    }

    for (const auto &p : pmfile_map) {
        if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }

    for (const auto &p : fsfile_map) {
        if (fs::is_directory(p.second)) {
            fs::remove_all(p.second);
        }
        else if (fs::exists(p.second)) {
            fs::remove(p.second);
        }
    }


    if (!pmdir.empty()) {
        fs::remove_all(pmdir);
        error_if_exists(pmdir);
    }

    output_res.set_value(res);
}

void model_checker_state::checkpoint_push(void) {
    for (const auto &p : file_memory_regions_) {
        const auto &pmfile = p.first;
        for (const auto &r : p.second) {

            vector<char> contents((char*)r.lower(), (char*)r.upper());

            // // Trade-off between checkpoint speed and space.
            // // checkpoints_[r].push_back(gzip::compress(contents));
            checkpoints_[r].push_back(contents);

            // checkpoint_files_[r].push_back(fs::unique_path());
            // const auto &new_path = checkpoint_files_[r].back();
            // BOOST_ASSERT(!fs::exists(new_path));

            // fs::ofstream cs(new_path);
            // cs.write((char*)r.lower(), r.upper() - r.lower() + 1);
        }
    }
}

void model_checker_state::checkpoint_pop(void) {
    for (auto &p : checkpoints_) {
        auto &stack = p.second;
        BOOST_ASSERT(stack.size() >= 1);
        stack.pop_back();
    }

    // for (auto &p : checkpoint_files_) {
    //     auto &stack = p.second;
    //     BOOST_ASSERT(stack.size() >= 1);
    //     fs::remove(stack.back());
    //     stack.pop_back();
    // }
}

size_t model_checker_state::num_checkpoints(void) const {
    if (checkpoints_.empty()) {
        return 0;
    }

    auto ci = checkpoints_.begin();
    size_t num = ci->second.size();
    ++ci;
    for (; ci != checkpoints_.end(); ++ci) {
        BOOST_ASSERT(num == ci->second.size());
    }
    return num;
}

void model_checker_state::restore(void) {
    for (auto &p : checkpoints_) {
        const auto &r = p.first;
        auto &stack = p.second;
        // auto backup = gzip::decompress(stack.back());
        const auto &backup = stack.back();

        BOOST_ASSERT(backup.size() == r.upper() - r.lower());

        memcpy((void*)r.lower(), backup.data(), backup.size());
    }

    // for (auto &p : checkpoint_files_) {
    //     const auto &r = p.first;
    //     auto &stack = p.second;
    //     // auto backup = gzip::decompress(stack.back());
    //     const auto &backup = stack.back();

    //     BOOST_ASSERT(backup.size() == r.upper() - r.lower());

    //     memcpy((void*)r.lower(), backup.data(), backup.size());
    // }
}

void model_checker_state::sync_files(void) {
    for (auto &p : checkpoints_) {
        const auto &r = p.first;
        msync((void*)r.lower(), r.upper() - r.lower(), MS_SYNC);
    }
}

void model_checker_state::wipe_files(void) {
    // Reset to the first checkpoint (a practical wipe) to still allow files
    // with trace setup to start over.
    for (auto &p : checkpoints_) {
        const auto &r = p.first;
        auto &stack = p.second;
        // auto backup = gzip::decompress(stack.back());
        const auto &backup = stack.front();

        assert(backup.size() == r.upper() - r.lower());

        memcpy((void*)r.lower(), backup.data(), backup.size());
    }
}

void model_checker_state::backup_write_files(void) {
    unordered_map<string, fs::path>::iterator iter = fsfile_map.begin();
    while (iter != fsfile_map.end()) {
        if (!fs::exists(iter->second)) continue;
        fs::path backup_path;
        do {
            backup_path = iter->second.parent_path() / fs::unique_path();
        } while (fs::exists(backup_path));
        fs::copy_file(iter->second, backup_path);
        // printf("Backup file %s \n", backup_path.c_str());
        backup_map[iter->second.string()] = backup_path;
        iter++;
    }
}

void model_checker_state::restore_write_files(void) {
    unordered_map<string, fs::path>::iterator iter = backup_map.begin();
    while (iter != backup_map.end()) {
        fs::path orig_path(iter->first);
        if (fs::exists(orig_path)) {
            fs::remove(orig_path);
        }
        fs::copy_file(iter->second, orig_path);
        iter++;
    }
}

void model_checker_state::backup_pmdir(bool contains_sparse_file) {
    do {
        backup_dir = pmdir.parent_path() / fs::unique_path("%%%%-%%%%-%%%%-%%%%-BAK");
    } while (fs::exists(backup_dir));

    copy_directory(pmdir, backup_dir, contains_sparse_file);

}

void model_checker_state::restore_pmdir(bool contains_sparse_file) {
    // We have to fix-up mmap regions, as they are all invalidated now
    for (const auto &range : mapped_) {
        (void)munmap((void*)range.lower(), range.upper() - range.lower());
    }
    mapped_.clear();
    file_memory_regions_.clear();
    offset_mapping_.clear();
    unordered_map<int, int>::iterator iter = fd_to_fd.begin();
    while (iter != fd_to_fd.end()) {
        if (iter->second != -1) {
            close(iter->second);
            fd_to_fd[iter->first] = -1;
        }
        iter++;
    }
    
    file_to_fd.clear();
    fd_to_fd.clear();

    for (const auto &path : fs::directory_iterator(pmdir)) {
        fs::remove_all(path);
    }

    copy_directory(backup_dir, pmdir, contains_sparse_file);

    fs::remove_all(backup_dir);
    error_if_exists(backup_dir);

    // iter = fd_map_backup.begin();
    // while (iter != fd_map_backup.end()) {
    //     if (iter->second == -1) {
    //         iter++;
    //         continue;
    //     }
    //     // if a file is opened, skip it, otherwise we may create zombie fd
    //     if (file_to_fd.find(iter->first) != file_to_fd.end() && file_to_fd.at(iter->first) != -1) {
    //         iter++;
    //         continue;
    //     }
    //     check_and_create_path(fsfile_map[iter->first].parent_path());
    //     // if iter->second is a directory, skip for now
    //     // TODO: handle directory
    //     int fd = -1;
    //     if (fs::is_directory(fsfile_map[iter->first])) {
    //         check_and_create_path(fsfile_map[iter->first]);
    //         fd = open(fsfile_map[iter->first].c_str(), O_RDONLY | O_DIRECTORY);
    //     }
    //     else {
    //         fd = open(fsfile_map[iter->first].c_str(), O_RDWR | O_CREAT, 0666);
    //     }
    //     if (fd == -1) {
    //         cerr << "Failed to open file or directory: " << fsfile_map[iter->first] << endl
    //             << "Reason: " << strerror(errno) << endl;;
    //         exit(EXIT_FAILURE);
    //     }
    //     file_to_fd[iter->first] = fd;
    //     // printf("File %s open as fd %d \n", iter->second.c_str(), fd);
    //     iter++;
    // }
    
    // TODO: this causes issues as it may create intermediate files and wipe out content of current files
    // we create a separate model_checker_state for each ordering for now
    for (const auto te : mmio_events) {
        if (te->is_register_file()) {
            do_register_file(te);
        }
        else if (te->is_unregister_file()) {
            do_unregister_file(te);
        }
        else if (te->is_open()) {
            do_open(te);
        }
        else if (te->is_creat()) {
            // TODO: this may be unsafe, since file may already exist
            do_creat(te);
        }
        else if (te->is_close()) {
            do_close(te);
        }
        else {
            cerr << "Unhandled event! " << te->str() << endl;
            exit(EXIT_FAILURE);
        }
    }
    apply_lseek_offset();
}

void model_checker_state::open_write_files(void) {
    // this call will be skipped for POSIX mode
    if (mode_ == POSIX) {
        return;
    }
    unordered_map<string, fs::path>::iterator iter = fsfile_map.begin();
    while (iter != fsfile_map.end()) {
        // if a file is opened, skip it, otherwise we may create zombie fd
        if (file_to_fd.find(iter->first) != file_to_fd.end() && file_to_fd.at(iter->first) != -1) {
            iter++;
            continue;
        }
        check_and_create_path(iter->second.parent_path());
        // if iter->second is a directory, skip for now
        // TODO: handle directory
        int fd = -1;
        if (fs::is_directory(iter->second)) {
            check_and_create_path(iter->second);
            fd = open(iter->second.c_str(), O_RDONLY | O_DIRECTORY);
        }
        else {
            fd = open(iter->second.c_str(), O_RDWR | O_CREAT, 0666);
        }
        if (fd == -1) {
            cerr << "Failed to open file or directory: " << iter->second << endl
                << "Reason: " << strerror(errno) << endl;;
            exit(EXIT_FAILURE);
        }
        file_to_fd[iter->first] = fd;
        // printf("File %s open as fd %d \n", iter->second.c_str(), fd);
        iter++;
    }
}

void model_checker_state::close_write_files() {
    // this call will be skipped for POSIX mode
    if (mode_ == POSIX) {
        return;
    }
    unordered_map<string, int>::iterator iter = file_to_fd.begin();
    while (iter != file_to_fd.end()) {
        close(iter->second);
        file_to_fd[iter->first] = -1;
        iter++;
    }
}

void model_checker_state::apply_trace_event(shared_ptr<trace_event> te) {
        if (te->is_write()) {
            do_write(te);
        } 
        else if (te->is_pwritev()) {
            do_pwritev(te);
        } 
        else if (te->is_fallocate()) {
            do_fallocate(te);
        } 
        else if (te->is_ftruncate()) {
            do_ftruncate(te);
        } 
        else if (te->is_pwrite64()) {
            do_pwrite64(te);
        } 
        else if (te->is_writev()) {
            do_writev(te);
        } 
        else if (te->is_lseek()) {
            do_lseek(te);
        }
        else if (te->is_rename()) {
            do_rename(te);
        }
        else if (te->is_unlink()) {
            do_unlink(te);
        }
        else if (te->is_fsync()) {
            do_fsync(te);
        }
        else if (te->is_fdatasync()) {
            do_fdatasync(te);
        }
        else if (te->is_msync()) {
            do_msync(te);
        } 
        else if (te->is_mkdir()) {
            do_mkdir(te);
        }
        else if (te->is_rmdir()) {
            do_rmdir(te);
        }
        else if (te->is_sync()) {
            do_sync(te);
        }
        else if (te->is_syncfs()) {
            do_syncfs(te);
        }
        else if (te->is_sync_file_range()) {
            do_sync_file_range(te);
        }
        else if (mode_ == POSIX && te->is_unregister_file()) {
            do_unregister_file(te);
        }
        else if (mode_ == POSIX && te->is_open()) {
            do_open(te);
        }
        else if (mode_ == POSIX && te->is_creat()) {
            do_creat(te);
        }
        else if (mode_ == POSIX && te->is_close()) {
            do_close(te);
        }
        else if (!te->is_flush() &&
                   !te->is_fence() &&
                   !te->is_register_write_file() &&
                   !te->is_marker_event() && !te->is_unregister_file() && !te->is_open() && !te->is_creat() && !te->is_close()) {
            cerr << __FUNCTION__ << ":" << __LINE__ << " --- Unhandled event! "
                << te->str() << endl;
            exit(EXIT_FAILURE);
        }
}

void model_checker_state::output_ops_completed(vector<uint64_t> all_applied_event_ids) {
        fs::path ops_completed = pmdir / "ops_completed";
        fs::ofstream ocstream(ops_completed);
        // for (const auto &p : tid_to_op_id) {
        //     ocstream << p.first << "," << p.second << endl;
        // }
    
        unordered_map<uint64_t, unordered_map<int, vector<uint64_t>>> thread_ops = event_trace.get_thread_ops();
        for (auto &p : thread_ops) {
            uint64_t tid = p.first;
            // sort p.second by the key
            vector<pair<int, vector<uint64_t>>> ops_vec(p.second.begin(), p.second.end());
            sort(ops_vec.begin(), ops_vec.end(), [](const pair<int, vector<uint64_t>> &a, const pair<int, vector<uint64_t>> &b) {
                return a.first < b.first;
            });
            
            for (auto &q : ops_vec) {
                int op_id = q.first;
                unordered_map<uint64_t, bool> event_id_to_applied;
                for (uint64_t event_id : q.second) {
                    shared_ptr<trace_event> te = event_trace.events()[event_id];
                    if (find(all_applied_event_ids.begin(), all_applied_event_ids.end(), event_id) != all_applied_event_ids.end() || te->is_sync_family() || (te->micro_events && te->micro_events->size() == 0)) {
                        // sync family events are vacuously applied
                        event_id_to_applied[event_id] = true;
                    } else {
                        event_id_to_applied[event_id] = false;
                    }
                }
                bool all_applied = true;
                bool none_applied = true;
                for (auto &r : event_id_to_applied) {
                    if (!r.second) all_applied = false;
                    if (r.second) none_applied = false;
                }
                assert(!(all_applied && none_applied));
                assert(q.second.size() > 0);
                int workload_tid = event_trace.events()[q.second[0]]->workload_tid();
                if (all_applied) {
                    ocstream << workload_tid << "," << op_id << ",1" << endl;
                } else if (!none_applied && !all_applied) {
                    ocstream << workload_tid << "," << op_id << ",2" << endl;
                }
            }
        }
        ocstream.close();
}

model_checker_state::model_checker_state(
    uint64_t id, const trace &t, fs::path o, shared_ptr<mutex> m) :
        file_mutex_(new mutex()),
        stdout_mutex_(m),
        checkpoints_(),
        test_id(id), event_trace(t), outdir(o) {
    // BOOST_ASSERT(event_idxs.front() >= 0);
    // BOOST_ASSERT(event_idxs.back() < t.events().size());
}

}  // namespace pathfinder