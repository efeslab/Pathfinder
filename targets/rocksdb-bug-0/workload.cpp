#include <dirent.h>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/utilities/transaction_db.h>

using namespace rocksdb;

void ReOpen(TransactionDB** txn_db, const std::string& db_path, const DBOptions& db_options, const TransactionDBOptions& txn_db_options, std::vector<ColumnFamilyDescriptor>& column_families, std::vector<ColumnFamilyHandle*>& handles) {
    if (*txn_db != nullptr) {
        for (auto* handle : handles) {
            delete handle;
        }
        handles.clear();
        delete *txn_db;
    }

    // List existing column families
    std::vector<std::string> cf_names;
    Status s = DB::ListColumnFamilies(db_options, db_path, &cf_names);
    if (!s.ok()) {
        // If there's an error listing column families, assume there's only the default one
        cf_names.push_back(kDefaultColumnFamilyName);
    }

    // Prepare column family descriptors
    column_families.clear();
    for (const auto& cf_name : cf_names) {
        column_families.push_back(ColumnFamilyDescriptor(cf_name, ColumnFamilyOptions()));
    }

    s = TransactionDB::Open(db_options, txn_db_options, db_path, column_families, &handles, txn_db);
    assert(s.ok());
}

// Function to list all log files in a directory and find the largest one
std::string findLargestLogFile(const std::string& directory) {
    DIR* dir;
    struct dirent* ent;
    std::string largest_file;
    int max_number = -1;

    if ((dir = opendir(directory.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            std::string filename(ent->d_name);
            if (filename.find(".log") != std::string::npos) {
                int number = std::stoi(filename.substr(0, filename.find(".log")));
                if (number > max_number) {
                    max_number = number;
                    largest_file = filename;
                }
            }
        }
        closedir(dir);
    } else {
        // Could not open directory
        perror("Could not open directory");
        return "";
    }

    return directory + "/" + largest_file;
}

// Helper function to check if an operation was successful
void AssertOK(bool condition, const std::string& message = "") {
    if (!condition) {
        std::cerr << "Assertion failed: " << message << std::endl;
        exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <database_path>" << std::endl;
        return 1;
    }

    std::string db_path = argv[1];
    TransactionDBOptions txn_db_options;
    DBOptions db_options;
    db_options.create_if_missing = true;
    db_options.create_missing_column_families = true;
    db_options.wal_recovery_mode = WALRecoveryMode::kPointInTimeRecovery;

    TransactionDB* txn_db = nullptr;
    std::vector<ColumnFamilyHandle*> handles;
    std::vector<ColumnFamilyDescriptor> column_families = {
        ColumnFamilyDescriptor(kDefaultColumnFamilyName, ColumnFamilyOptions())
    };
    ReOpen(&txn_db, db_path, db_options, txn_db_options, column_families, handles);
    ColumnFamilyOptions cf_options;
    ColumnFamilyHandle* cf_handle = nullptr;
    bool cf_exists = false;
    for (auto* handle : handles) {
        if (handle->GetName() == "two") {
            cf_handle = handle;
            cf_exists = true;
            break;
        }
    }
    if (!cf_exists) {
        Status s = txn_db->CreateColumnFamily(cf_options, "two", &cf_handle);
        assert(s.ok());
        handles.push_back(cf_handle);
    }
    WriteOptions write_options;
    TransactionOptions txn_options;
    Transaction* txn = txn_db->BeginTransaction(write_options, txn_options);
    std::string large_value(400, ' ');
    assert(txn != nullptr);
    Status s = txn->SetName("xid");
    assert(s.ok());
    s = txn->Put(cf_handle, "foo1", "bar1");
    assert(s.ok());
    s = txn->Put(cf_handle, "foo2", large_value);
    assert(s.ok());
    s = txn->Commit();
    assert(s.ok());
    delete txn;
    
    SequenceNumber latest_seq = txn_db->GetLatestSequenceNumber();
    std::cout << "Latest sequence in DB: " << latest_seq << std::endl;

    // Corrupt the latest log file
    // std::string fname = findLargestLogFile(db_path);
    // if (fname.empty()) {
    //     std::cerr << "No log file found or directory could not be opened." << std::endl;
    //     return 1;
    // }
    // // Open the largest log file for reading and writing
    // std::fstream file(fname, std::ios::in | std::ios::out | std::ios::binary);
    // AssertOK(file.is_open(), "Failed to open file");
    // // Read the contents of the file into a string
    // std::string file_content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    
    // // Modify the contents at specific positions
    // if (file_content.size() > 401) {  // Ensure there's enough data to modify
    //     file_content[400] = 'h';
    //     file_content[401] = 'a';
    // } else {
    //     std::cerr << "File content is not long enough to be corrupted as specified." << std::endl;
    //     return 1;
    // }
    // // Move the file pointer back to the beginning
    // file.seekp(0, std::ios::beg);
    // // Write the modified content back to the file
    // file.write(file_content.data(), file_content.size());
    // AssertOK(file.good(), "Failed to write corrupted data back to file");
    // // Close the file
    // file.close();
    // Simulate "crash" by properly closing the DB
    
    for (auto* handle : handles) {
        delete handle;
    }
    handles.clear();
    delete txn_db;
    txn_db = nullptr;
    // select the *.log file with the largest number
    
    // Simulate recovery
    DBOptions new_db_options;
    new_db_options.create_if_missing = false;
    new_db_options.create_missing_column_families = false;
    new_db_options.add_empty_batch = true;
    ReOpen(&txn_db, db_path, new_db_options, txn_db_options, column_families, handles);
    // if (write_after_recovery) {
    //     s = txn_db->Put(write_options, cf_handle, "foo5", std::string(400, ' '));
    //     assert(s.ok());
    // }
    assert(s.ok());

    latest_seq = txn_db->GetLatestSequenceNumber();
    std::cout << "Latest sequence in DB: " << latest_seq << std::endl;

    txn_db->FlushWAL(true);
    // Clean up
    for (auto* handle : handles) {
        delete handle;
    }
    delete txn_db;
    return 0;
}
