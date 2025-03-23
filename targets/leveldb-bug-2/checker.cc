#include <cassert>
#include <iostream>
#include <vector>
#include <algorithm>
#include "leveldb/db.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unordered_map>
#include <fstream>
#include <sstream>

using namespace std;
using namespace leveldb;

#define PATH_BUFFER 10000

/* Utility function that reads an entire file into the given buffer. Assumes
 * buffer has enough space to read entire file. */
void readall(char *filepath, char *buffer) {
	int fd, ret, pos;
	fd = open(filepath, O_RDONLY);
	assert(fd > 0);
	
	pos = 0;
	do {
		ret = read(fd, buffer + pos, 4096);
		assert(ret >= 0);
		pos = pos + ret;
		*(buffer + pos) = '\0';
	} while (ret > 0);
}

bool contains_key(const std::unordered_map<string, string>& map, string key) {
    return map.find(key) != map.end();
}

int main(int argc, char *argv[]) {
	// if (argc != 2 && argc != 3) {
	// 	printf("Usage: %s <workload_dir>\n", argv[0]);
	// 	exit(1);
	// }

	/* Variable declarations and some setup */
	DB* db;
	Options options;
	Status ret;
	ReadOptions read_options;
	string key, value;

	Iterator* it;
	char printed_messages[1000];
	int fd, pos;
	int retreived_rows = 0;
	int row_present[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char db_path[PATH_BUFFER];

	string global_log_path;
	string op_completed_path;
	unordered_map<string, string> expected_state; // map of key -> value
	unordered_map<string, string> expected_state_last_sync; // snapshot of expected_state at sync points
	unordered_map<string, string> completed_ops;  // map of op_id -> thread_id

	options.write_buffer_size = WRITE_BUFFER_SIZE;
	options.create_if_missing = true;
	options.paranoid_checks = true;
	read_options.verify_checksums = true;

	/* Getting all the messages printed to the terminal at the time of the simulated
	 * crash. This will be useful when we are checking for durability. The second
	 * command line argument to this checker is the path to a file containing the
	 * terminal output at the time of the simulated crash.  */
	// readall(argv[2], printed_messages);

	/* Opening the database. The first command line argument to this checker is the
	 * path to a folder that contains the state of the workload directory after the
	 * file system recovers from the simulated crash (i.e., if the argument is
	 * "/tmp/foo", then if the exact simulated crash had actually happened, we will
	 * find all files within "workload_dir" to be in the same state as they are now
	 * in "/tmp/foo"). Therefore, the database that we are supposed to check, is
	 * "<first command line argument>/testdb" (corresponding to
	 * "workload_dir/testdb" used in init.cc and workload.cc). */

	/* Get the crash state file path, argument 1 */
	strcpy(db_path, argv[1]);
	ret = DB::Open(options, db_path, &db);
	if (!ret.ok()) {
		printf("Open failed\n");
		printf("%s\n", ret.ToString().c_str());
		exit(1);
	}

	/* Create our expected state by reading the global log and ops completed file */

	/* Get the op_completed file, argument 3
	 * ENTRY: thread_id, op_id, is_completed */
	op_completed_path = argv[3];
	std::cout << op_completed_path << std::endl;
	std::ifstream op_completed(op_completed_path);
	if (!op_completed) {
        std::cerr << "Error opening ops completed file." << std::endl;
        exit(1);
    }
	string oc_entry;
	char delimeter = ',';
	std::cout << "Reading ops completed file..." << std::endl;
	while (std::getline(op_completed, oc_entry)) {
		string token;
		vector<string> get_entry;
		std::stringstream ss(oc_entry);
		while (std::getline(ss, token, delimeter)) {
			get_entry.push_back(token);
		}
		if (get_entry[2] == "1") {
			completed_ops.insert({get_entry[1], get_entry[0]});
		} else if (get_entry[2] == "0" || get_entry[2] == "2") { // "2" is a half-complete op
			continue;
		} else {
			std::cerr << "Error parsing ops completed file." << std::endl;
        	exit(1);
		}
	}
	std::cout << "Ops completed size: " << completed_ops.size() << std::endl;

	/* Get the global log file, argument 2.
	 * PUT: thread_id, op_id, sync_option, op, key, value
	 * DELETE: thread_id, op_id, sync_option, op, key */
	global_log_path = argv[2];
	std::ifstream global_log(global_log_path);
	if (!global_log) {
        std::cerr << "Error opening global log." << std::endl;
        exit(1);
    }
	string gl_entry;
	std::cout << "Reading global log..." << std::endl;
	int num_puts = 0;
	int num_dels = 0;
	while (std::getline(global_log, gl_entry)) {
		string token;
		vector<string> get_entry;
		std::stringstream ss(gl_entry);
		while (std::getline(ss, token, delimeter)) {
			get_entry.push_back(token);
		}
		/* If the operation is executed and is within the global log, update the expected state. */
		if (contains_key(completed_ops, get_entry[1])) {
			if (get_entry[3] == "PUT") {
				expected_state.insert({get_entry[4], get_entry[5]});
				num_puts += 1;
			} else if (get_entry[3] == "DELETE") {
				expected_state.erase(get_entry[4]);
				num_dels += 1;
			} else {
				std::cerr << "Invalid op when parsing global log." << std::endl;
				exit(1);
			}
			/* If we are at a sync point, save a snapshot of the expected state. */
			if (get_entry[2] == "SYNC") { 
				std::cout << "Updating sync snapshot at op_id: " << get_entry[1] << std::endl;
				expected_state_last_sync = expected_state; 
			}
		}
	}
	std::cout << "Number of puts: " << num_puts << std::endl;
	std::cout << "Number of deletes: " << num_dels << std::endl;
	std::cout << "Initial expected state size: " << expected_state.size() << std::endl;


	/* Read the database, and verify *atomicity*, i.e., whether the retreived
	 * key-value pairs are the same as those inserted during the workload. */
	std::cout << "Looking through the database..." << std::endl;
	it = db->NewIterator(read_options);
	assert(it->status().ok());

	bool contains_error = false;
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		assert(it->status().ok());

		/* If K-V is in the map, remove it. Otherwise, raise a flag. */
		string key_str = it->key().ToString();
		string value_str = it->value().ToString();
		if (contains_key(expected_state, key_str)) {
			expected_state.erase(key_str);
		} else {
			contains_error = true;
			// TODO: do something?
			continue;
		}
	}

	delete it;

	/* Check if K-V pairs from the sync point exist in the db */
	/* Make sure log files are synchronized with the db. */
	bool missing_keys_before_sync = false;
	if (!expected_state_last_sync.empty()) {
		Status res;
		string k, v;
		for (const auto& pair : expected_state_last_sync) {
			k = pair.first;
			res = db->Get(read_options, k, &v);
			if (!res.ok()) { // key doesn't exist in the db, return inconsistency.
				if (!missing_keys_before_sync) { missing_keys_before_sync = true; }
				std::cout << "Missing key " << k << " before last sync point!" << std::endl;
			} 
		}
	}

	if (missing_keys_before_sync) {
		std::cout << "Missing keys. Returning inconsistency." << std::endl;
		return 1;
	} else {
		std::cout << "Expected state is consistent!" << std::endl;
	}

	if (!expected_state.empty()) {
		std::cout << "Final expected state size: " << expected_state.size() << std::endl;
	}
	std::cout << "Did we try to get a key that doesn't exist?: " << std::boolalpha << contains_error << std::endl;

	std::cout << "Checker instance finished." << std::endl;
	return 0;
}