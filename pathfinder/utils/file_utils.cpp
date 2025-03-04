#include "file_utils.hpp"

#include <iostream>

namespace fs = boost::filesystem;
using namespace std;

namespace pathfinder {

void check_and_create_path(const fs::path &dir) {
    if (fs::is_regular_file(dir)) {
        cerr << "check_and_create_path: path provided not a directory" << endl;
        return;
    }
    if (fs::exists(dir)) {
        return;
    }
    if (!fs::exists(dir.parent_path())) {
        check_and_create_path(dir.parent_path());
    }
    fs::create_directory(dir);
}

// Assume a filename, and not a directory name like "/foo/bar/"
std::string get_dir_name(const std::string filename) {
  fs::path p(filename);
  return p.parent_path().string();
}

// Return pair <parent directory name, file name> of a full path.
std::pair<std::string, std::string> get_dir_and_name(const std::string& name) {
  fs::path p(name);
  std::string dirname = p.parent_path().string();
  std::string fname = p.filename().string();
  return std::make_pair(dirname, fname);
}

// Given file offset and size, calculate the block ids
std::pair<int, int> get_block_ids(const int offset, const int size, const int block_size) {
  int start_block = offset / block_size;
  int end_block = (offset + size - 1) / block_size;
  return std::make_pair(start_block, end_block);
}

// Check if two block ids are overlapping.
bool is_block_ids_overlapping(const std::pair<int, int>& block_ids_a, const std::pair<int, int>& block_ids_b) {
  if (block_ids_a == block_ids_b) {
      return true;
  }
  if (block_ids_a.first <= block_ids_b.first && block_ids_b.first <= block_ids_a.second) {
      return true;
  }
  if (block_ids_a.first <= block_ids_b.second && block_ids_b.second <= block_ids_a.second) {
      return true;
  }
  return false;
}

}  // namespace pathfinder