#include <cassert>
#include <iostream>
#include "leveldb/db.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "common.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

using namespace std;
using namespace leveldb;

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
	int i;

	Iterator* it;
	char printed_messages[1000];
	int fd, pos;
	int retreived_rows = 0;
	int row_present[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	char db_path[10000];

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

	strcpy(db_path, argv[1]);
	// strcat(db_path, "/testdb");

	ret = DB::Open(options, db_path, &db);
	// assert(ret.ok());
	if (!ret.ok()) {
		printf("Open failed\n");
		printf("%s\n", ret.ToString().c_str());
		exit(1);
	}

	/* Read the database, and verify *atomicity*, i.e., whether the retreived
	 * key-value pairs are the same as those inserted during the workload.
	 * (workload.cc and init.cc inserts unique strings corresponding to the numbers
	 * 0 to 9, as key-value pairs.) 
	 *
	 * Also record the total number of key-value pairs retreived, in the
	 * "retreived_rows" variable. 
	 *
	 * Also record which exact key-value pairs (corresponding to which numbers
	 * between 0 and 9) are retreived, in the row_present array.*/

	it = db->NewIterator(read_options);
	assert(it->status().ok());

	bool has_last_key = false;
	for (it->SeekToFirst(); it->Valid(); it->Next()) {
		assert(it->status().ok());

		// int row_number = it->key().ToString().c_str()[0] - 'a';
		// assert(row_number >= 0 && row_number < 10);

		// key = string(gen_string(row_number, KEY_SIZE));
		// value = string(gen_string(row_number, VALUE_SIZE));
		// assert(key == it->key().ToString());
		// assert(value == it->value().ToString());
		// printf("Got row %d\n", row_number);

		// printf("Got key %s value %s\n", it->key().ToString().c_str(), it->value().ToString().c_str());
		string key_str = it->key().ToString();
		string value_str = it->value().ToString();
		if (key_str.size() > 20) key_str = key_str.substr(0, 20);
		if (value_str.size() > 20) value_str = value_str.substr(0, 20);
		cout << "Got key " << key_str << " value " << value_str << endl;

		// if (key[0] == 'j') {
		// 	printf("after 9\n");
		// 	has_last_key = true;
		// }
		// row_present[row_number] = 1;
		// retreived_rows++;
	}

	delete it;

	// /* Verify ordering: If 7 rows are present, they must be rows 0 to 6. */

	// for(i = 0; i < retreived_rows; i++) {
	// 	assert(row_present[i] == 1);
	// }

	// /* Verify durability */

	// /* init.cc inserts four rows. These four rows should be present, no matter
	//  * when a crash occurs. */
	// assert(retreived_rows >= 4);

	// /* If the message "after 9" has been printed to the terminal by the time the
	//  * crash occurs, all 10 rows (i.e., 0 to 9) should be present in the database
	//  * (workload.cc use synchronous Put for the last row.) */
	// if (has_last_key) {
	// 		assert(retreived_rows == 10);
	// }

}