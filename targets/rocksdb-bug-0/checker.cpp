#include <fstream>
#include <iostream>
#include <filesystem>
#include <memory>
#include <string>

#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/transaction_log.h>
#include <rocksdb/write_batch.h>

using namespace rocksdb;

class CountingHandler : public WriteBatch::Handler {
 public:
  size_t num_puts = 0;
  size_t num_deletes = 0;

  Status PutCF(uint32_t /*cf_id*/, const Slice& /*key*/, const Slice& /*value*/) override {
    ++num_puts;
    return Status::OK();
  }
  
  Status DeleteCF(uint32_t /*cf_id*/, const Slice& /*key*/) override {
    ++num_deletes;
    return Status::OK();
  }
};


int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " /path/to/db\n";
    return 1;
  }

  std::string db_path = argv[1];
  Options options;
  options.create_if_missing = false;

  DB* db = nullptr;

  std::vector<std::string> cf_names;
  Status s = DB::ListColumnFamilies(DBOptions(), db_path, &cf_names);
  if (!s.ok()) {
  std::cerr << "ListColumnFamilies failed: " << s.ToString() << "\n";
  return 0;
  }  
  std::cout << "Column families in this DB:\n";
  for (auto& cf_name : cf_names) {
  std::cout << "  " << cf_name << "\n";
  }  
  std::vector<ColumnFamilyDescriptor> cf_descriptors;
  for (auto& name : cf_names) {
  cf_descriptors.push_back(
      ColumnFamilyDescriptor(name, ColumnFamilyOptions())
  );
  }

  std::vector<ColumnFamilyHandle*> handles;
  s = DB::Open(options, db_path, cf_descriptors, &handles, &db);

  if (!s.ok()) {
    std::cerr << "Failed to open DB: " << s.ToString() << std::endl;
    return 0;
  }

  SequenceNumber latest_seq = db->GetLatestSequenceNumber();
  std::cout << "Latest sequence in DB: " << latest_seq << std::endl;

  // list all files under db_path with a prefix of LOG
  std::vector<std::string> log_files;
  // use C++ filesystem API to list files
  for (const auto& entry : std::filesystem::directory_iterator(db_path)) {
    if (entry.path().filename().string().find("LOG") == 0) {
      log_files.push_back(entry.path().string());
    }
  }

  // for all LOG files, check if there is an entry containing "Dummy write to log"
  // use C++ fstream API to read the file
  bool found_dummy_write = false;
  for (const auto& log_file : log_files) {
    std::ifstream file(log_file);
    if (!file.is_open()) {
      std::cerr << "Failed to open file: " << log_file << std::endl;
      return 0;
    }
    std::string line;
    while (std::getline(file, line)) {
      if (line.find("Dummy write to log") != std::string::npos) {
        std::cout << "Found dummy write in log file: " << log_file << std::endl;
        found_dummy_write = true;
        break;
      }
    }
    file.close();
  }

  // print key and value
  ReadOptions ro;
  int count = 0;
  bool found_dummy_key = false;
  for (auto& cfh : handles) {
    std::unique_ptr<Iterator> it(db->NewIterator(ro, cfh));
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
      std::cout << it->key().ToString() << ": " << it->value().ToString() << std::endl;
      if (it->key().ToString() == "dummy_key_for_recovery") {
        found_dummy_key = true;
      }
      count++;
    }
    if (!it->status().ok()) {
      std::cerr << "Iterator status error: " << it->status().ToString() << std::endl;
    }
  }
  std::cout << "Total keys: " << count << std::endl;

  if (found_dummy_write && !found_dummy_key) {
    std::cerr << "Found dummy write in log file but not in DB" << std::endl;
    return 1;
  }

  // Now, before delete db, destroy all CF handles:
  for (auto cfh : handles) {
    s = db->DestroyColumnFamilyHandle(cfh);
    if (!s.ok()) {
      // handle error, but typically you still proceed
    }
  }


  delete db;
  return 0;
}
