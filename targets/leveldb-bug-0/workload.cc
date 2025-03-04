#include <cassert>
#include <iostream>
#include "leveldb/db.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "common.h"

using namespace std;
using namespace leveldb;

int main(int argc, char *argv[]) {
	if (argc != 2) {
		printf("Usage: %s <workload_dir>\n", argv[0]);
		exit(1);
	}
	/* Variable declarations and some setup */
	DB* db;
	Options options;
	Status ret;
	WriteOptions write_options;
	string key, value;
	int i;

	options.create_if_missing = true;
	options.paranoid_checks = true;
	options.write_buffer_size = WRITE_BUFFER_SIZE;
	write_options.sync = false;

	/* Open the database */
	ret = DB::Open(options, argv[1], &db);
	assert(ret.ok());

	/* Put six rows into the database. Their keys and values are each strings
	 * corresponding to 4, 5, 6, 7, 8, and 9. Only the last Put (corresponding to 9) is
	 * done synchronously. */
	for(i = 4; i < 10; i++) {
		// key = string(gen_string(i, KEY_SIZE));
		// value = string(gen_string(i, VALUE_SIZE));
		key = to_string(i);
		value = to_string(i);

		if(i % 2 == 0) {
			write_options.sync = true;
		}
		else {
			write_options.sync = false;
		}

		printf("before %d\n", i);
		ret = db->Put(write_options, key, value);
		printf("after %d\n", i);
		assert(ret.ok());
	}

	/* Close the database */
	delete db;
}