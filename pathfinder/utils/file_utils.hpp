#pragma once

#include <boost/filesystem.hpp>

namespace pathfinder {

/**
 * Check if the path exists and create directory on the fly.
 */
void check_and_create_path(const boost::filesystem::path &dir);


/**
 * Get the directory name from a full path.
 */
std::string get_dir_name(const std::string filename);

/**
 * Return pair <parent directory name, file name> of a full path.
 */
std::pair<std::string, std::string> get_dir_and_name(const std::string& name);

/**
 * Given file offset and size, calculate the block ids
*/
std::pair<int, int> get_block_ids(const int offset, const int size, const int block_size);

/**
 * Check if two block ids are overlapping.
 */
bool is_block_ids_overlapping(const std::pair<int, int>& block_ids_a, const std::pair<int, int>& block_ids_b);

}  // namespace pathfinder
