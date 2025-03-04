#include "trace.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <ios>
#include <vector>

#include <fcntl.h>

#include <boost/icl/right_open_interval.hpp>
#include <boost/algorithm/string/regex.hpp>

using namespace std;
namespace bp = boost::process;
namespace icl = boost::icl;
namespace fs = boost::filesystem;

namespace pathfinder
{

vector<stack_frame> trace::parse_pm_stack(
    vector<string>::iterator &start,
    vector<string>::iterator &end) {

    static regex frame_re(R"((\w+): (.+) \((.+):(\d+)\))");
    // static regex frame_imprecise_re(R"((\w+): (.+) \(in (.+)\))");
    vector<stack_frame> stack;

    for (auto it = start; it != end; ++it) {
        cmatch cm;
        stack_frame sf;

        if (std::regex_match(it->c_str(), cm, frame_re)) {
            sf.binary_address = stoi(cm[1], nullptr, 16);
            sf.function = cm[2];
            sf.file = cm[3];
            sf.line = stoi(cm[4]);
            stack.push_back(sf);
        } else {
            break;
        }
    }

    return stack;
}

vector<stack_frame> trace::parse_posix_stack(vector<string> &raw_pieces) {
    vector<stack_frame> stack;
    if (raw_pieces.size() <= 1) return stack;
    for (size_t i = 1; i < raw_pieces.size(); i++) {
        stack_frame sf;
        // split by ","
        vector<string> pieces;
        // boost::split(pieces, raw_pieces[i], boost::is_any_of(","));
        // split by "," but only split by last 3 commas
        boost::split(pieces, raw_pieces[i], boost::is_any_of(","));
        if (pieces.size() < 4) break;
        if (pieces.size() > 4) {
            vector<string> processed_pieces;
            // keep last 3 pieces, and combine the rest with ","
            string combined;
            for (size_t j = 0; j < pieces.size() - 3; j++) {
                combined += pieces[j];
                if (j != pieces.size() - 4) {
                    combined += ",";
                }
            }
            processed_pieces.push_back(combined);
            for (size_t j = pieces.size() - 3; j < pieces.size(); j++) {
                processed_pieces.push_back(pieces[j]);
            }
            pieces = processed_pieces;
        }
        sf.function = pieces[0];
        if (pieces[1] == "") {
            sf.file = "unknown";
            sf.line = -1;
            if (i == 1 && find(intrinsic_functions.begin(), intrinsic_functions.end(), sf.function) == intrinsic_functions.end())
            intrinsic_functions.push_back(sf.function);
            // continue;
        } 
        else {
            sf.file = pieces[1];
            sf.line = stoi(pieces[2]);
        }
        sf.binary_address = stoi(pieces[3], nullptr, 16);
        stack.push_back(sf);
    }
    return stack;

}

trace_event trace::parse_pm_op(const string &raw_event) {
    trace_event te;
    vector<string> pieces;

    BOOST_ASSERT(!raw_event.empty());

    boost::split(pieces, raw_event, boost::is_any_of(";"));

    auto it_start = pieces.begin();
    auto it_end = pieces.end();

    if (pieces[0] == "STORE") {
        te.type = STORE;
        te.address = stoull(pieces[1], nullptr, 16);
        te.value = stoull(pieces[2], nullptr, 16);
        te.size = stoull(pieces[3], nullptr, 16);
        assert(te.size <= sizeof(te.value));
        for (int i = 0; i < te.size; i++) {
            te.value_bytes.push_back( ((char*)&te.value)[i] );
        }
        assert(te.size == te.value_bytes.size());

        it_start = pieces.begin() + 4;
        te.stack = parse_pm_stack(it_start, it_end);
    } else if (pieces[0] == "FLUSH") {
        te.type = FLUSH;
        te.address = stoull(pieces[1], nullptr, 16);
        te.size = stoull(pieces[2], nullptr, 16);
        it_start = pieces.begin() + 3;
        // iangneal: don't need this for now
        te.stack = parse_pm_stack(it_start, it_end);
    } else if (pieces[0] == "FENCE") {
        te.type = FENCE;
        it_start = pieces.begin() + 1;
        // iangneal: don't need this for now.
        te.stack = parse_pm_stack(it_start, it_end);
    } else if (pieces[0] == "REGISTER_FILE") {
        te.type = REGISTER_FILE;
        te.file_path = pieces[1];
        te.address = stoull(pieces[2], nullptr, 16);
        te.size = stoull(pieces[3], nullptr, 16);
        te.file_offset = stol(pieces[4], nullptr, 16);
        // Also add to the set of known PM files
        pm_files_.insert(te.file_path);
    } else if (pieces[0] == "WRITE") {
        te.type = WRITE;
        te.file_path = pieces[1];

        // translate NEWLINE & SEMICOMMA tokens
        std::regex r_newline("NEWLINE");
        std::regex r_semicomma("SEMICOMMA");
        string processed = std::regex_replace(pieces[2], r_newline, "\n");
        processed = std::regex_replace(processed, r_semicomma, ";");

        te.buf = processed;
        it_start = pieces.begin() + 3;
        te.stack = parse_pm_stack(it_start, it_end);
        // for now I don't want to pollute pm_files_ with files for WRITE
        // if (pm_files_.find(te.file_path) != pm_files_.end()) {
        //     pm_files_.erase(te.file_path);
        // }
        // write_files_.insert(te.file_path);

        // WRITE event should be after REGISTER_WRITE_FILE event
        assert(write_files_.find(te.file_path) != write_files_.end());
    } else if (pieces[0] == "REGISTER_WRITE_FILE") {
        te.type = REGISTER_WRITE_FILE;
        te.file_path = pieces[1];
        write_files_.insert(te.file_path);
    } else if (pieces[0] == "PWRITEV") {
        te.type = PWRITEV;
        te.file_path = pieces[1];
        te.wfile_offset = stoull(pieces[2]);
        int buf_count = stoi(pieces[3]);
        for (int i = 0; i < buf_count; i++) {
            // translate NEWLINE & SEMICOMMA tokens
            std::regex r_newline("NEWLINE");
            std::regex r_semicomma("SEMICOMMA");
            string processed = std::regex_replace(pieces[4+i], r_newline, "\n");
            processed = std::regex_replace(processed, r_semicomma, ";");

            te.buf_vec.push_back(processed);
        }
        it_start = pieces.begin() + 4 + buf_count;
        te.stack = parse_pm_stack(it_start, it_end);
        // WRITE event should be after REGISTER_WRITE_FILE event
        assert(write_files_.find(te.file_path) != write_files_.end());
    } else if (pieces[0] == "FTRUNCATE") {
        te.type = FTRUNCATE;
        te.file_path = pieces[1];
        te.len = stol(pieces[2]);
    } else if (pieces[0] == "FALLOCATE") {
        te.type = FALLOCATE;
        te.file_path = pieces[1];
        te.mode = stoi(pieces[2]);
        te.file_offset = stol(pieces[3]);
        te.len = stol(pieces[4]);
    } else if (pieces[0] == PATHFINDER_BEGIN_TOKEN) {
        te.type = PATHFINDER_BEGIN;
    } else if (pieces[0] == PATHFINDER_END_TOKEN) {
        te.type = PATHFINDER_END;
    } else {
        cerr << "Unrecognized event: " << pieces[0] << endl;
        cerr << "\t" << raw_event << endl;
        exit(EXIT_FAILURE);
    }

    // This won't be equal because we discard "???" entries.
    // cout << "+++" << endl;
    // cout << raw_event << " (" << raw_event.size() << ")"<< endl;
    // cout << te.str() << " (" << te.str().size() << ")"<< endl;
    // cout << "+++" << endl;
    // assert(raw_event.find(te.str()) != string::npos);

    return te;
}

shared_ptr<char> trace::base64_decode(const char* input, uint32_t size)
{
	/* set up a destination buffer large enough to hold the encoded data */
	char* output = (char*)malloc(BUFFER_SIZE);
	/* keep track of our decoded position */
	char* c = output;
	/* store the number of bytes decoded by a single call */
	int cnt = 0;
	/* we need a decoder state */
	base64_decodestate s;
	
	/*---------- START DECODING ----------*/
	/* initialise the decoder state */
	base64_init_decodestate(&s);
	/* decode the input data */
	cnt = base64_decode_block(input, size, c, &s);
	c += cnt;
	/* note: there is no base64_decode_blockend! */
	/*---------- STOP DECODING  ----------*/
	
	/* we want to print the decoded data, so null-terminate it: */
	*c = 0;

    // now we know the actual size of the decoded data, allocate another char array and copy the data
    // char* final_output = (char*)malloc(c - output + 1);
    shared_ptr<char> final_output = shared_ptr<char>(new char[c - output + 1], std::default_delete<char[]>());
    memcpy(final_output.get(), output, c - output + 1);
    free(output);
	
	return final_output;
}

trace_event trace::parse_posix_op(const string &line) {
    trace_event te;
    vector<string> pieces;

    // split line by ";"
    vector<string> raw_pieces;
    boost::algorithm::split_regex(raw_pieces, line, boost::regex(";"));
    BOOST_ASSERT(raw_pieces.size());
    string posix_event = raw_pieces[0];

    // split posix_event by ","
    boost::split(pieces, posix_event, boost::is_any_of(","));
    auto it_start = pieces.begin();
    auto it_end = pieces.end();
    // cout << "posix_event: " << posix_event << endl;
    if (pieces[2] == "STORE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = STORE;
        te.store_num = stoull(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.address = stoull(pieces[5], nullptr, 16);
        te.size = stoull(pieces[6], nullptr, 10);
        string encoded_value = pieces[7];
        te.char_buf = base64_decode(encoded_value.c_str(), encoded_value.size());
        // I cannot use strlen here because it will stop at the first '\0'
        // assert(te.size <= strlen(te.char_buf));
        for (int i = 0; i < te.size; i++) {
            te.value_bytes.push_back(*(te.char_buf.get() + i));
        }
        assert(te.size == te.value_bytes.size());
        // parse stack

    }
    else if (pieces[2] == "REGISTER_FILE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = REGISTER_FILE;
        te.file_path = pieces[3];
        te.address = stoull(pieces[4], nullptr, 16);
        te.size = stoull(pieces[5], nullptr, 10);
        te.file_offset = stol(pieces[6], nullptr, 10);
        te.prot = stoi(pieces[7], nullptr, 10);
        te.flags = stoi(pieces[8], nullptr, 10);
        write_files_.insert(te.file_path);
        pm_files_.insert(te.file_path);
    }
    else if (pieces[2] == "UNREGISTER_FILE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = UNREGISTER_FILE;
        te.file_path = pieces[3];
        te.address = stoull(pieces[4], nullptr, 16);
        te.size = stoull(pieces[5], nullptr, 10);
    }
    else if (pieces[2] == "MSYNC") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = MSYNC;
        te.file_path = pieces[3];
        te.address = stoull(pieces[4], nullptr, 16);
        te.size = stoull(pieces[5], nullptr, 10);
        te.flags = stoi(pieces[6], nullptr, 10);
    }
    else if (pieces[2] == "FTRUNCATE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = FTRUNCATE;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.len = stol(pieces[5]);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "PWRITE64") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = PWRITE64;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.file_offset = stol(pieces[5]);
        te.size = stoull(pieces[6]);
        string encoded_value = pieces[7];
        te.char_buf = base64_decode(encoded_value.c_str(), encoded_value.size());
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "WRITE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = WRITE;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.size = stoull(pieces[5]);
        string encoded_value = pieces[6];
        te.char_buf = base64_decode(encoded_value.c_str(), encoded_value.size());
        // cout << "te.buf: " << te.buf << endl;
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "WRITEV") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = WRITEV;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.iovcnt = stoi(pieces[5]);
        for (int i = 0; i < te.iovcnt; i++) {
            int iov_len = stoi(pieces[6+i*2]);
            string encoded_value = pieces[6+i*2+1];
            shared_ptr<char> iov_base = base64_decode(encoded_value.c_str(), encoded_value.size());
            te.iov.push_back(make_tuple(iov_len, iov_base));
            // cout << "te.buf: " << te.buf << endl;
        }
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "LSEEK") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = LSEEK;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.file_offset = stol(pieces[5]);
        te.flags = stoi(pieces[6]);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "RENAME") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = RENAME;
        te.file_path = pieces[3];
        te.new_path = pieces[4];
        write_files_.insert(te.file_path);
        write_files_.insert(te.new_path);
    }
    else if (pieces[2] == "UNLINK") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = UNLINK;
        te.file_path = pieces[3];
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "FSYNC") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = FSYNC;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "FDATASYNC") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = FDATASYNC;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "FALLOCATE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = FALLOCATE;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.mode = stoi(pieces[5], nullptr, 10);
        te.file_offset = stol(pieces[6], nullptr, 10);
        te.len = stol(pieces[7], nullptr, 10);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "OPEN") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = OPEN;
        te.file_path = pieces[3];
        te.flags = stoi(pieces[4], nullptr, 10);
        te.mode = stoi(pieces[5], nullptr, 10);
        te.fd = stoi(pieces[6], nullptr, 10);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "CREAT") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = CREAT;
        te.file_path = pieces[3];
        te.mode = stoi(pieces[4], nullptr, 10);
        te.fd = stoi(pieces[5], nullptr, 10);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "CLOSE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = CLOSE;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
    }
    else if (pieces[2] == "MKDIR") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = MKDIR;
        te.file_path = pieces[3];
        te.mode = stoi(pieces[4], nullptr, 10);
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "RMDIR") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = RMDIR;
        te.file_path = pieces[3];
        write_files_.insert(te.file_path);
    }
    else if (pieces[2] == "SYNC") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = SYNC;
    }
    else if (pieces[2] == "SYNCFS") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = SYNCFS;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
    }
    else if (pieces[2] == "SYNC_FILE_RANGE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = SYNC_FILE_RANGE;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.file_offset = stol(pieces[5], nullptr, 10);
        te.len = stol(pieces[6], nullptr, 10);
        te.flags = stoi(pieces[7], nullptr, 10);
    }
    else if (pieces[2] == "FENCE") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = FENCE;
    }
    else if (pieces[2] == "READ") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = READ;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.size = stoull(pieces[5]);
    }
    else if (pieces[2] == "PREAD") {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10);
        te.type = PREAD;
        te.fd = stoi(pieces[3], nullptr, 10);
        te.file_path = pieces[4];
        te.file_offset = stol(pieces[5]);
        te.size = stoull(pieces[6]);
    }
    else if (pieces[2] == PATHFINDER_BEGIN_TOKEN) {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = PATHFINDER_BEGIN;
    }
    else if (pieces[2] == PATHFINDER_END_TOKEN) {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = PATHFINDER_END;
    }
    else if (pieces[2] == PATHFINDER_OP_BEGIN_TOKEN) {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = PATHFINDER_OP_BEGIN;
        te.workload_thread_id = stoi(pieces[3], nullptr, 10);
        te.thread_op_id = stoi(pieces[4], nullptr, 10);
        current_thread_op_ = make_pair(te.tid, *te.thread_op_id);
        current_tid_to_workload_tid_ = make_pair(te.tid, *te.workload_thread_id);
    }
    else if (pieces[2] == PATHFINDER_OP_END_TOKEN) {
        te.timestamp = stoull(pieces[0], nullptr, 10);
        te.tid = stoull(pieces[1], nullptr, 10); 
        te.type = PATHFINDER_OP_END;
        te.workload_thread_id = stoi(pieces[3], nullptr, 10);
        te.thread_op_id = stoi(pieces[4], nullptr, 10);
        current_thread_op_ = nullopt;
        current_tid_to_workload_tid_ = nullopt;
    }
    else {
        cerr << "Unrecognized event: " << pieces[2] << endl;
        cerr << "\t" << line << endl;
        exit(EXIT_FAILURE);
    }
    te.stack = parse_posix_stack(raw_pieces);
    // TODO: current Pin tool has some problem handling multi-processing with -follow-execv
    // this is a temporary fix to ensure timestamp is always increasing...
    te.timestamp = timestamp_;
    timestamp_++;

    if (current_thread_op_ && te.type != PATHFINDER_OP_BEGIN && current_thread_op_->first == te.tid) {
        thread_ops_[current_thread_op_->first][current_thread_op_->second].push_back(te.timestamp);
        te.workload_thread_id = current_tid_to_workload_tid_->second;
    }

    return te;
}

size_t trace::get_next_events(string line) {
    if (line.empty()) {
        return 0;
    }
    size_t nadded = 0;

    if (mode_ == PM) {
        // See if this line has a trace in it.
        if (line.find("START||") != string::npos ||
            line.find("||STOP") != string::npos) {
            // Split the trace
            vector<string> raw_events;
            boost::algorithm::split_regex(raw_events, line, boost::regex("\\|\\|"));
            // cerr << "\n\nnew line!\n>>> " << line << endl << endl;
            for (const auto &event : raw_events) {
                if (event.empty() ||
                    event.find("START") != string::npos ||
                    event.find("STOP") != string::npos ||
                    event.find("==") != string::npos
                    ) continue;

                // cerr << "event: " << event << endl;

                std::shared_ptr<trace_event> te = make_shared<trace_event>(parse_pm_op(event));
                if (te->is_marker_event() && !selective_) {
                    cerr << "Warning: ignoring selective testing trace events (testing full trace anyways).\n";
                    continue;
                }

                te->timestamp = timestamp_;
                timestamp_++;
                if (te->is_store()) {
                    te->store_num = store_num_;
                    store_num_++;
                }

                if (te->is_write() || te->is_pwritev()) {
                    te->write_num = write_num_;
                    write_num_++;
                }

                if (te->is_pathfinder_begin()) {
                    testing_starts_.push_back(te->timestamp);
                }

                if (te->is_pathfinder_end()) {
                    testing_stops_.push_back(te->timestamp);
                }

                events_.push_back(te);
                assert(te->event_idx() == events_.size() - 1);
                if (events_.back()->is_store()) {
                    stores_.push_back(events_.back());
                    // cerr << stores_.back() << "\n" << stores_.back()->str() << "\n";
                }
                nadded++;
            }
        }
    }
    else {
        std::shared_ptr<trace_event> te = make_shared<trace_event>(parse_posix_op(line));
        events_.push_back(te);
        assert(te->event_idx() == events_.size() - 1);
        if (events_.back()->is_store()) {
            stores_.push_back(events_.back());
            // cerr << stores_.back() << "\n" << stores_.back()->str() << "\n";
        }
        nadded++;
    }

    // Otherwise, see if it's a summary
    // TODO: We really don't need this right now, we don't use it anyways.

    return nadded;
}

void trace::construct_testing_ranges(void) {
    // BOOST_ASSERT(!events_.empty());
    if (events_.empty()) {
        return;
    }

    uint64_t first = events_.front()->timestamp;
    uint64_t last = events_.back()->timestamp;
    if (testing_starts_.empty() && testing_stops_.empty()) {
        auto all_events_iv = icl::interval<uint64_t>::closed(first, last);
        testing_ranges_.insert(all_events_iv);
        return;
    }

    if (testing_starts_.size() == testing_stops_.size() - 1) {
        testing_starts_.push_front(first);
    } else if (testing_starts_.size() - 1 == testing_stops_.size()){
        testing_stops_.push_back(last);
    } else if (testing_starts_.size() != testing_stops_.size()) {
        cerr << "Not sure what to do with n_starts=" << testing_starts_.size()
            << " and n_stops=" << testing_stops_.size() << "\n";
        exit(EXIT_FAILURE);
    }

    auto bi = testing_starts_.begin();
    auto si = testing_stops_.begin();
    for (; bi != testing_starts_.end() && si != testing_stops_.end(); bi++, si++) {
        if (*bi >= *si) {
            cerr << "Error: start ts=" << *bi << " >= stop ts=" << *si << "\n";
            exit(EXIT_FAILURE);
        }
        auto iv = icl::interval<uint64_t>::closed(*bi, *si);
        testing_ranges_.insert(iv);
    }
}

void trace::read(bp::child &child, std::istream &stream) {
    stringstream ss;
    thread t;
    do {
        string line;
        std::getline(stream, line);
        if (!line.empty()) {
            // Join so we get serializable time stamps.
            if (t.joinable()) t.join();
            // Do this asynchronously to allow for reading and parsing.
            auto tfn = [&, this] (string s) { this->get_next_events(s); };
            t = thread(tfn, line);
        }
    } while (!stream.eof());
    child.wait();
    if (t.joinable()) t.join();

    for (const auto & func : intrinsic_functions) {
        cout << "intrinsic function: " << func << endl;
    }


    construct_testing_ranges();
}

void trace::read(bp::child &child, bp::child &test, std::istream &stream) {
    stringstream ss;
    thread t;

    do {
        string line;
        std::getline(stream, line);
        if (!line.empty()) {
            // Join so we get serializable time stamps.
            if (t.joinable()) t.join();
            // Do this asynchronously to allow for reading and parsing.
            auto tfn = [&, this] (string s) { this->get_next_events(s); };
            t = thread(tfn, line);
        }
    } while (!stream.eof() || test.running());
    child.wait();
    if (t.joinable()) t.join();

    for (const auto & func : intrinsic_functions) {
        cout << "intrinsic function: " << func << endl;
    }

    construct_testing_ranges();
}

void trace::read_offline_trace(fs::path trace_path) {
    if (!fs::exists(trace_path)) {
        cerr << "read_offline_trace: offline log doesn't exist!" << endl;
        exit(EXIT_FAILURE);
    }
    stringstream ss;
    thread t;
    fs::ifstream stream(trace_path);
    do {
        string line;
        std::getline(stream, line);
        if (!line.empty()) {
            // Join so we get serializable time stamps.
            if (t.joinable()) t.join();
            // Do this asynchronously to allow for reading and parsing.
            auto tfn = [&, this] (string s) { this->get_next_events(s); };
            t = thread(tfn, line);
        }
    } while (!stream.eof());
    if (t.joinable()) t.join();

    for (const auto & func : intrinsic_functions) {
        cout << "intrinsic function: " << func << endl;
    }

    construct_testing_ranges();
}

unordered_map<string, fs::path> trace::map_pmfile(fs::path pmfile) const {
    unordered_map<string, fs::path> path_map;
    if (pm_files_.size() != 1) {
        cerr << __FUNCTION__ << ": Too many pm_files in trace! " << pm_files_.size() << "\n";
        for (const auto &pmf : pm_files_) {
            cerr << "\t" << pmf << "\n";
        }
        exit(EXIT_FAILURE);
    }

    path_map[*pm_files_.begin()] = pmfile;
    return path_map;
}

unordered_map<string, fs::path> trace::map_pmfiles(fs::path pmdir) const {
    unordered_map<string, fs::path> path_map;

    string root_name = root_dir.filename().string();
    // cout << "root_name: " << root_name << endl;
    for (const auto &orig_file : pm_files_) {
        // cout << "orig_file: " << orig_file << endl;
        vector<string> split_res;
        boost::split(split_res, orig_file, boost::is_any_of("/"));
        int pos = -1;
        for (int i=0; i<split_res.size(); i++) {
            if (split_res[i] == root_name) {
                pos = i;
                break;
            }
        }
        // cmon we need to find the position of root name
        BOOST_ASSERT(pos != -1);
        fs::path rel_path;
        for (int i=pos+1; i<split_res.size(); i++) {
            rel_path = rel_path / fs::path(split_res[i]);
        }
        path_map[orig_file] = pmdir / rel_path;

        create_directories_if_not_exist(path_map[orig_file].parent_path());
    }

    return path_map;
}

unordered_map<string, fs::path> trace::map_fsfiles(fs::path pmdir) const {
    unordered_map<string, fs::path> path_map;

    string root_name = root_dir.filename().string();

    for (const auto &orig_file : write_files_) {
        vector<string> split_res;
        boost::split(split_res, orig_file, boost::is_any_of("/"));
        int pos = -1;
        for (int i=0; i < split_res.size(); ++i) {
            if (split_res[i] == root_name) {
                pos = i;
                break;
            }
        }
        // cmon we need to find the position of root name
        // assert(pos != -1);

        BOOST_ASSERT(!pmdir.empty());

        // if not using pmdir, or not in root dir, just copy the file path
        if (pos == -1) {
            // hack: replace output.txt with /dev/null
            fs::path orig_path(orig_file);
            if (orig_path.filename() == "output.txt") {
                fs::path new_path = "/dev/null";
                path_map[orig_file] = new_path.string();
            } else {
                path_map[orig_file] = orig_file;
            }
        } else {
            fs::path rel_path;
            for (int i=pos+1; i<split_res.size(); i++) {
                rel_path = rel_path / fs::path(split_res[i]);
            }
            path_map[orig_file] = pmdir / rel_path;

            // create_directories_if_not_exist(path_map[orig_file].parent_path());
        }
    }

    return path_map;
}

std::unordered_map<string, boost::filesystem::path> trace::map_pmfile_hint(
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals) const
{
    unordered_map<string, fs::path> path_map;
    if (pm_files_.size() != 1) {
        cerr << __FUNCTION__ << ": Too many pm_files in trace! " << pm_files_.size() << "\n";
        for (const auto &pmf : pm_files_) {
            cerr << "\t" << pmf << "\n";
        }
        exit(EXIT_FAILURE);
    }

    // pm_file used in pmemcheck
    string pm_file = *pm_files_.begin();
    fs::path pm_file_path = pm_file;
    jinja2::ValuesMap::iterator iter = pmcheck_vals.begin();
    string template_value = "";
    while (iter != pmcheck_vals.end()) {
        // printf("Pmemcheck vals %s : %s \n", iter->first, iter->second.asString());
        // cout << "Pmemcheck vals "<<iter->first<< " : "<< iter->second.asString() << endl;
        fs::path val_path = iter->second.asString();
        if (pm_file_path.filename() == val_path.filename()) {
            template_value = iter->first;
            break;
        }
        iter++;
    }

    if (template_value == "") {
        cerr << "Don't know where this pmfile comes from! " << endl;
        exit(EXIT_FAILURE);
    }
    fs::path new_path = checker_vals[template_value].asString();
    path_map[pm_file] = new_path;

    return path_map;
}

std::unordered_map<string, boost::filesystem::path> trace::get_fsfiles_hint(
    jinja2::ValuesMap checker_vals,
    jinja2::ValuesMap pmcheck_vals) const
{
    unordered_map<string, fs::path> path_map;
    for (const auto &orig_file : write_files_) {
        fs::path orig_file_path = orig_file;
        jinja2::ValuesMap::iterator iter = pmcheck_vals.begin();
        string template_value = "";
        while (iter != pmcheck_vals.end()) {
            // printf("Pmemcheck vals %s : %s \n", iter->first, iter->second.asString());
            // cout << "Pmemcheck vals "<<iter->first<< " : "<< iter->second.asString() << endl;
            fs::path val_path = iter->second.asString();
            if (orig_file_path.filename() == val_path.filename()) {
                template_value = iter->first;
                break;
            }
            iter++;
        }
        if (template_value == "") {
            cerr << "Don't know where this fsfile comes from! " << endl;
            exit(EXIT_FAILURE);
        }
        fs::path new_path = checker_vals[template_value].asString();
        path_map[orig_file] = new_path;
    }

    return path_map;
}

bool trace::within_testing_range(std::shared_ptr<trace_event> ptr) const {
    return icl::contains(testing_ranges_, ptr->timestamp);
}

void trace::validate_store_events(void) const {
    cout << "Checking if all stores have valid addresses" << endl;
    vector<boost::icl::discrete_interval<uintptr_t>> addr_mappings;
    for (int i = 0; i < events().size(); i++) {
        const shared_ptr<trace_event> &te = events()[i];
        if (te->is_store()) {
            bool mapped = false;
            for (const auto &p : addr_mappings) {
                if (p.lower() <= (uintptr_t)te->address && (uintptr_t)te->address < p.upper()) {
                    mapped = true;
                    break;
                }
            }
            if (!mapped) {
                cerr << "No mapping found for store event: "<<
                    te->event_idx() << " at address: " << te->address << endl;
                exit(EXIT_FAILURE);
            }
        }
        else if (te->is_register_file()) {
            auto interval = icl::interval<uintptr_t>::right_open(
                (uintptr_t)te->address, (uintptr_t)te->address + te->size);
            addr_mappings.push_back(interval);
        }
    }
}

void trace::dump_csv(boost::filesystem::path &path) const {
    fs::ofstream f(path);
    f << "timestamp,event_type,store_id";
    // Figure out the max stack frame
    size_t max_stack = 0;
    for (const auto &te : events()) {
        max_stack = std::max(max_stack, te->stack.size());
    }
    f << ",address,size,value";
    for (size_t i = 0; i < max_stack; ++i) {
        f << ",frame_" << i;
        f << ",function_" << i;
        f << ",file_" << i;
        f << ",line_" << i;
        f << ",binary_address_" << i;
    }
    f << "\n";
    // Now dump all the events
    for (const auto &te : events()) {
        f << te->timestamp;
        f << "," << event_type_to_str(te->type);
        if (te->is_store()) f << "," << te->store_id();
        else f << "," << -1;
        // f << "," << te->event_idx();
        f << "," << te->address;
        f << "," << te->size;
        f << "," << te->value;
        size_t i = 0;
        // fill in what we have
        for (; i < te->stack.size(); ++i) {
            f << ",\"" << te->stack[i].str() << "\"";
            f << ",\"" << te->stack[i].function << "\"";
            f << ",\"" << te->stack[i].file << "\"";
            f << "," << te->stack[i].line;
            f << ",\"" << te->stack[i].binary_address << "\"";
        }
        // then, fill with empty
        for (; i < max_stack; ++i) {
            f << ",,,,,";
        }
        f << "\n";
    }
    f.flush();
    f.close();
}

void trace::decompose_trace_events(void) {
    assert(events_.size() > 0 && mode_ == POSIX);
    // we go over all the trace events and derive decomposed events
    // the hard part is tracking file size and offsets as they are important for determining whether a write may extend the file and includes metadata update
    unordered_map<int, int> fd_to_file_offset;
    unordered_map<string, vector<int>> filename_to_fd;
    unordered_map<string, int> filename_to_size;
    for (const auto &te : events_) {
        te->micro_events = vector<micro_event>();
        // open/creat/close
        // there are only metadata and data updates if a file is created
        // dir is also tracked but should have no effect as they are never written to by write family syscalls
        if (te->is_open()) {
            if (te->flags & O_CREAT) {
                filename_to_size[te->file_path] = 0;
                te->micro_events->push_back(m_event::creat_add_file_inode(te->file_path));
                string dirname = get_dir_name(te->file_path);
                te->micro_events->push_back(m_event::creat_inode_data_update(dirname));
            }
            if (te->flags & O_APPEND) {
                fd_to_file_offset[te->fd] = te->file_size;
            } else {
                fd_to_file_offset[te->fd] = 0;
            }
            filename_to_fd[te->file_path].push_back(te->fd);
        }
        if (te->is_creat()) {
            filename_to_size[te->file_path] = 0;
            fd_to_file_offset[te->fd] = 0;
            filename_to_fd[te->file_path].push_back(te->fd);
            te->micro_events->push_back(m_event::creat_add_file_inode(te->file_path));
            string dirname = get_dir_name(te->file_path);
            te->micro_events->push_back(m_event::creat_inode_data_update(dirname));
        }
        if (te->is_close()) {
            fd_to_file_offset.erase(te->fd);
            filename_to_fd[te->file_path].erase(std::remove(filename_to_fd[te->file_path].begin(), filename_to_fd[te->file_path].end(), te->fd), filename_to_fd[te->file_path].end());
        }
        // write family events
        if (te->is_write_family()) {
            off_t file_offset;
            uint64_t write_size;
            if (te->is_write()) {
                file_offset = fd_to_file_offset[te->fd];
                write_size = te->size;
            }
            if (te->is_pwrite64()) {
                file_offset = te->file_offset;
                write_size = te->size;
            }
            if (te->is_writev()) {
                uint64_t total_size = 0;
                for (const auto &iov : te->iov) {
                    total_size += std::get<0>(iov);
                }
                file_offset = fd_to_file_offset[te->fd];
                write_size = total_size;
            }
            if (te->is_pwritev()) {
                // TODO: pwritev not implemented in POSIX!!
                uint64_t total_size = 0;
                for (const auto &iov : te->iov) {
                    total_size += std::get<0>(iov);
                }
                file_offset = te->file_offset;
                write_size = total_size;
            }
            // derive block ids affected
            te->block_ids = get_block_ids(file_offset, write_size, BLOCK_SIZE);

            te->micro_events->push_back(m_event::creat_data_update(te->file_path, file_offset, write_size));
            // check if it is an extend by checking if the offset + size is greater than the file size
            if (file_offset + write_size > filename_to_size[te->file_path]) {
                filename_to_size[te->file_path] = file_offset + write_size;
                te->micro_events->push_back(m_event::creat_metadata_update(te->file_path));
            }
            // update the file offset if write or writev
            if (te->is_write() || te->is_writev()) {
                fd_to_file_offset[te->fd] += write_size;
            }
        }
        if (te->is_fallocate()) {
            // we assume fallocate always extend file and do not deal with deallocation for now
            // TODO: fallocate may do data update as well
            if (te->file_offset + te->len > filename_to_size[te->file_path]) {
                filename_to_size[te->file_path] = te->file_offset + te->len;
            }
            te->micro_events->push_back(m_event::creat_metadata_update(te->file_path));
            te->micro_events->push_back(m_event::creat_data_update(te->file_path, te->file_offset, te->len));
            te->block_ids = get_block_ids(te->file_offset, te->len, BLOCK_SIZE);
        }
        if (te->is_ftruncate()) {
           // ftruncate is interesting, extend does not need data update, but shrink does
            if (te->len < filename_to_size[te->file_path]) {
                te->block_ids = get_block_ids(te->len, filename_to_size[te->file_path] - te->len, BLOCK_SIZE);
                te->micro_events->push_back(m_event::creat_data_update(te->file_path, te->len, filename_to_size[te->file_path] - te->len));
            }
            filename_to_size[te->file_path] = te->len;
            te->micro_events->push_back(m_event::creat_metadata_update(te->file_path));
        }
        if (te->is_unlink()) {
            filename_to_size.erase(te->file_path);
            for (const auto &fd : filename_to_fd[te->file_path]) {
                fd_to_file_offset.erase(fd);
            }
            filename_to_fd.erase(te->file_path);
            string dirname = get_dir_name(te->file_path);
            te->micro_events->push_back(m_event::creat_inode_data_update(dirname));
            te->micro_events->push_back(m_event::creat_metadata_update(te->file_path));
        }
        if (te->is_rename()) {
            filename_to_size[te->new_path] = filename_to_size[te->file_path];
            filename_to_size.erase(te->file_path);
            for (const auto &fd : filename_to_fd[te->file_path]) {
                filename_to_fd[te->new_path].push_back(fd);
            }
            filename_to_fd.erase(te->file_path);
            string old_dirname = get_dir_name(te->file_path);
            string new_dirname = get_dir_name(te->new_path);
            te->micro_events->push_back(m_event::creat_inode_data_update(old_dirname));
            te->micro_events->push_back(m_event::creat_inode_data_update(new_dirname));
        }
        if (te->is_mkdir()) {
            string parent_dirname = get_dir_name(te->file_path);
            te->micro_events->push_back(m_event::creat_add_dir_inode(te->file_path));
            te->micro_events->push_back(m_event::creat_inode_data_update(parent_dirname));
            te->micro_events->push_back(m_event::creat_metadata_update(parent_dirname));
        }
        if (te->is_rmdir()) {
            string parent_dirname = get_dir_name(te->file_path);
            te->micro_events->push_back(m_event::creat_metadata_update(te->file_path));
            te->micro_events->push_back(m_event::creat_inode_data_update(parent_dirname));
            te->micro_events->push_back(m_event::creat_metadata_update(parent_dirname));
        }
        if (te->is_lseek()) {
            if (te->flags & SEEK_SET) {
                fd_to_file_offset[te->fd] = te->file_offset;
            } else if (te->flags & SEEK_CUR) {
                fd_to_file_offset[te->fd] += te->file_offset;
            } else if (te->flags & SEEK_END) {
                fd_to_file_offset[te->fd] = filename_to_size[te->file_path] + te->file_offset;
            }
            
        }
        // derive block_ids for sync_file_range
        if (te->is_sync_file_range()) {
            te->block_ids = get_block_ids(te->file_offset, te->len, BLOCK_SIZE);
        }
    }
}

trace::~trace() {
    // for (const auto & te : events_) {
    //     if (te->is_store() || te->is_pwrite64() || te->is_write()) {
    //         free(te->char_buf);
    //     }
    //     if (te->is_pwritev()) {
    //         for (auto &iov : te->iov) {
    //             free(std::get<1>(iov));
    //         }
    //     }
    // }
}

}