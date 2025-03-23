#include <cassert>
#include <iostream>
#include <fstream>
#include "leveldb/db.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "common.h"
#include <random>
#include <filesystem>

using namespace std;
using namespace leveldb;

#define WRITE_PERCENT 70

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <workload_dir>\n", argv[0]);
		exit(1);
	}
	/* Variable declarations for leveldb */
	DB* db;
	Options options;
	Status ret;
	WriteOptions write_options;
	ReadOptions read_options;
	string key, value;

	/* Variable declarations for workload operations */
	int num_ops = 5000;
	int log_num = num_ops / 10;
	int sync_at = 100;
	bool fast_delete = true;
	bool test_ops_completed = true;
	bool is_fsync, fsync_hit = false;

	/* Global log file for op tracking 
	 * SYNTAX:
	 * (thread_id, op_id, sync_option, op, key, value) */
	std::ofstream global_log;
	string global_log_path = "/home/yilegu/squint/pm-cc-bug-finder/targets/leveldb-bug-2/global_log_file.txt";
	global_log.open(global_log_path);
	if (!global_log) {
		std::cerr << "Error: Could not open the file for writing!" << std::endl;
		return 1;
	}
	string entry;
	string thread_id = "0,"; // no threads to keep track of

	/* Test ops completed file
	 * SYNTAX: (thread_id, op_id, is_completed) */
	std::ofstream ops_completed;
	string ops_completed_path = "/home/yilegu/squint/pm-cc-bug-finder/targets/leveldb-bug-2/ops_completed.txt";
	if (test_ops_completed) {
		ops_completed.open(ops_completed_path);
		if (!ops_completed) {
			std::cerr << "Error: Test ops completed file failed to open." << std::endl;
			return 1;
		}
	}
	string test_entry;

	/* Initialize a random number generator */
	std::mt19937 mt{};
	std::uniform_int_distribution<> percent_gen{1, 100};

	/* Set leveldb options */
	options.create_if_missing = true;
	options.paranoid_checks = true;
	// options.write_buffer_size = WRITE_BUFFER_SIZE;
	options.write_buffer_size = 1024;
	write_options.sync = false; // change

	/* Use for fast delete, keep track of the keys we've inserted in the db */
	vector<int> keys_inserted;

	/* Open the database */
	ret = DB::Open(options, argv[1], &db);
	assert(ret.ok());

	/* Randomly Put/Delete rows into/from the database. Their keys and values are each strings
	 * corresponding the current iteration number. Puts/Deletes at (i % sync_at == 0) are
	 * done synchronously. Puts/Deletes are also done synchronously when compaction starts. */
	for (int i = 0; i < num_ops; i++) {
		string key = std::to_string(i);
		string value = std::to_string(i);

		/* Enforce regular sync intervals. */
		// if(i % sync_at == 0) {
		// 	write_options.sync = true;
		// }
		// else {
		// 	write_options.sync = false;
		// }

		if (test_ops_completed) {
			test_entry = thread_id + std::to_string(i) + ",1";
			ops_completed << test_entry << std::endl;
		}

		if (!is_fsync && !fsync_hit) {
			/* If we haven't come across an fsync point, continue delete ops. */
			int prob = percent_gen(mt);
			if (prob < WRITE_PERCENT) {
				/* If we roll a WRITE_PERCENT% chance, put key into the db */
				SQUINT_OP_BEGIN(0, i);
				ret = db->Put(write_options, key, value);
				SQUINT_OP_END(0, i);
				i % log_num == 0 ? printf("Put key < %d\n", i) : 0;
				if (fast_delete) { keys_inserted.push_back(i); }
				assert(ret.ok());
				entry = thread_id + std::to_string(i) + "," + (is_fsync ? "SYNC," : "ASYNC,")
						+ "PUT," + key  + "," + value;
				global_log << entry << std::endl;
			} else { 
				/* If we roll a (100 - WRITE_PERCENT)% chance, delete a random key from the db */
				if (fast_delete) {
					if (!keys_inserted.empty()) {
						int db_size = keys_inserted.size();
						std::uniform_int_distribution<> vec_distrib{0, db_size - 1};
						int index = vec_distrib(mt);
						string del_key = std::to_string(keys_inserted[index]);
						keys_inserted.erase(keys_inserted.begin() + index);
						SQUINT_OP_BEGIN(0, i);
						ret = db->Delete(write_options, del_key);
						SQUINT_OP_END(0, i);
						assert(ret.ok());
						entry = thread_id + std::to_string(i) + "," + (is_fsync ? "SYNC," : "ASYNC,")
								+ "DELETE," + del_key;
						global_log << entry << std::endl;
					}
				} else {
					/* Safe (slow) implementation */
					vector<int> all_keys;
					Iterator* it = db->NewIterator(read_options);
					assert(it->status().ok());
					for (it->SeekToFirst(); it->Valid(); it->Next()) {
						assert(it->status().ok());
						int curr_key = stoi(it->key().ToString());
						all_keys.push_back(curr_key);
					}
					if (!all_keys.empty()) {
						int db_size = all_keys.size();
						std::uniform_int_distribution<> vec_distrib{0, db_size - 1};
						int index = vec_distrib(mt);
						string del_key = std::to_string(all_keys[index]);
						SQUINT_OP_BEGIN(0, i);
						ret = db->Delete(write_options, del_key);
						SQUINT_OP_END(0, i);
						assert(ret.ok());
						entry = thread_id + std::to_string(i) + "," + (is_fsync ? "SYNC," : "ASYNC,")
								+ "DELETE," + del_key;
						global_log << entry << std::endl;
					}
				}
			}
		} else if (is_fsync && !fsync_hit) {
			fsync_hit = true;
			std::cout << "Hit a sync point at op_id: " << i - 1 << ", only doing puts from now on." << std::endl;
			SQUINT_OP_BEGIN(0, i);
			ret = db->Put(write_options, key, value);
			SQUINT_OP_END(0, i);
			i % log_num == 0 ? printf("Put key < %d\n", i) : 0;
			if (fast_delete) { keys_inserted.push_back(i); }
			assert(ret.ok());
			entry = thread_id + std::to_string(i) + "," + (is_fsync ? "SYNC," : "ASYNC,")
					+ "PUT," + key  + "," + value;
			global_log << entry << std::endl;
		} else { // fsync_hit is true
			/* Continue doing puts after the sync point. */
			SQUINT_OP_BEGIN(0, i);
			ret = db->Put(write_options, key, value);
			SQUINT_OP_END(0, i);
			i % log_num == 0 ? printf("Put key < %d\n", i) : 0;
			if (fast_delete) { keys_inserted.push_back(i); }
			assert(ret.ok());
			entry = thread_id + std::to_string(i) + "," + (is_fsync ? "SYNC," : "ASYNC,")
					+ "PUT," + key  + "," + value;
			global_log << entry << std::endl;
		}
	}

	/* Close the database */
	global_log.close();
	ops_completed.close();
	delete db;
}