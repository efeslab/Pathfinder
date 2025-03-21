#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iomanip> // For std::setw and std::setfill
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <stdexcept>
#include <memory>

extern "C" {
	#include <b64/cencode.h>
	#include <b64/cdecode.h>
}

#define BUFFER_SIZE 1000000

// Supported flags in octal
#define GENMC_O_RDONLY  00000000
#define GENMC_O_WRONLY  00000001
#define GENMC_O_RDWR    00000002
#define GENMC_O_CREAT   00000100
#define GENMC_O_TRUNC   00001000
#define GENMC_O_APPEND  00002000
#define GENMC_O_SYNC    04010000
#define GENMC_O_DSYNC   00010000

// Mapping for C functions
std::map<std::string, std::string> syscall_to_c = {
    {"OPEN", "open"},
    {"WRITE", "write"},
    {"CLOSE", "close"},
    {"FDATASYNC", "fdatasync"},
    {"RENAME", "rename"},
    {"UNLINK", "unlink"},
    {"FTRUNCATE", "ftruncate"},
    {"FSYNC", "fsync"},
    {"FALLOCATE", "fallocate"},
    {"PWRITE64", "pwrite64"},
    {"MKDIR", "mkdir"}
};

// File descriptor map
std::map<int, std::string> fd_map;

// Utility to replace a substring within a string
std::string replace_substring(const std::string &str, const std::string &from, const std::string &to) {
    std::string result = str;
    size_t start_pos = result.find(from);
    if (start_pos != std::string::npos) {
        result.replace(start_pos, from.length(), to);
    }
    return result;
}

int sanitize_open_flags(int flags) {
    int supported_flags = GENMC_O_RDONLY | GENMC_O_WRONLY | GENMC_O_RDWR | GENMC_O_CREAT | GENMC_O_TRUNC | GENMC_O_APPEND | GENMC_O_SYNC | GENMC_O_DSYNC;
    return flags & supported_flags;
}

std::shared_ptr<char> base64_decode(const char* input, uint32_t size)
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
    std::shared_ptr<char> final_output = std::shared_ptr<char>(new char[c - output + 1], std::default_delete<char[]>());
    memcpy(final_output.get(), output, c - output + 1);
    free(output);
	
	return final_output;
}

// Parse the trace and generate the C++ code
void parse_trace_and_generate_cpp(const std::string &input_file, const std::string &src_dir, const std::string &dst_dir, const std::string &output_file) {
    std::vector<std::string> cpp_code = {
        "#include <fcntl.h>",
        "#include <unistd.h>",
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "#include <sys/stat.h>",
        "#include <sys/types.h>",
        "",
        "int main() {"
    };

    std::set<int> fd_declaration_set; // Track already declared file descriptors

    std::ifstream infile(input_file);
    if (!infile) {
        throw std::runtime_error("Failed to open input file.");
    }

    std::string line;
    while (std::getline(infile, line)) {
        line = line.substr(0, line.find(';')); // Strip off call stack
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::vector<std::string> parts;
        std::string token;
        while (std::getline(iss, token, ',')) {
            parts.push_back(token);
        }

        std::string syscall_type = parts[2];
        std::vector<std::string> args(parts.begin() + 3, parts.end());

        if (syscall_type == "OPEN") {
            std::string path = replace_substring(args[0], src_dir, dst_dir);
            int flags = std::stoi(args[1]);
            int mode = std::stoi(args[2]);
            int fd = std::stoi(args[3]);

            // Sanitize flags
            flags = sanitize_open_flags(flags);


            if (fd_declaration_set.find(fd) == fd_declaration_set.end()) {
                cpp_code.push_back("    int fd" + std::to_string(fd) + " = open(\"" + path + "\", " + std::to_string(flags) + ", " + std::to_string(mode) + ");");
                fd_declaration_set.insert(fd);
            } else {
                cpp_code.push_back("    fd" + std::to_string(fd) + " = open(\"" + path + "\", " + std::to_string(flags) + ", " + std::to_string(mode) + ");");
            }
            // cpp_code.push_back("    if (fd" + std::to_string(fd) + " < 0) { perror(\"open\"); return 1; }");
            fd_map[fd] = "fd" + std::to_string(fd);
        } else if (syscall_type == "WRITE") {
            int fd = std::stoi(args[0]);
            int size = std::stoi(args[2]);
            std::string content_b64 = args[3];
            
            // Decode Base64
            try {
                std::shared_ptr<char> content = base64_decode(content_b64.c_str(), content_b64.size());
                std::ostringstream content_hex;
                for (int i = 0; i< size; i++) {
                    // content_hex << "\\x" << std::hex << (int)(content.get()[i]);
                    // content_hex << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (int)(content.get()[i]);
                    content_hex << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (content.get()[i] & 0xFF);
                }
                cpp_code.push_back("    write(" + fd_map[fd] + ", \"" + content_hex.str() + "\", " + std::to_string(size) + ");");
            } catch (const std::exception &e) {
                std::cerr << "Error decoding Base64 content for WRITE syscall: " << e.what() << std::endl;
                cpp_code.push_back("    // Skipped WRITE syscall for fd " + std::to_string(fd) + " due to Base64 decoding error");
            }
        } else if (syscall_type == "CLOSE") {
            int fd = std::stoi(args[0]);
            cpp_code.push_back("    close(" + fd_map[fd] + ");");
        } else if (syscall_type == "FDATASYNC") {
            // we will use fsync instead since fdatasync is not supported by GenMC
            int fd = std::stoi(args[0]);
            cpp_code.push_back("    fsync(" + fd_map[fd] + ");");
        } else if (syscall_type == "RENAME") {
            std::string old_path = replace_substring(args[0], src_dir, dst_dir);
            std::string new_path = replace_substring(args[1], src_dir, dst_dir);
            cpp_code.push_back("    rename(\"" + old_path + "\", \"" + new_path + "\");");
        } else if (syscall_type == "UNLINK") {
            std::string path = replace_substring(args[0], src_dir, dst_dir);
            cpp_code.push_back("    unlink(\"" + path + "\");");
        }  else if (syscall_type == "FTRUNCATE") {
            std::string path = replace_substring(args[1], src_dir, dst_dir);
            int length = std::stoi(args[2]);
            cpp_code.push_back("    truncate(\"" + path + "\", " + std::to_string(length) + ");");
        } else if (syscall_type == "FSYNC") {
            int fd = std::stoi(args[0]);
            cpp_code.push_back("    fsync(" + fd_map[fd] + ");");
        } 
        else if (syscall_type == "FALLOCATE") {
            std::string path = replace_substring(args[1], src_dir, dst_dir);
            int length = std::stoi(args[2]);
            cpp_code.push_back("    truncate(\"" + path + "\", " + std::to_string(length-1) + ");");
        } 
        else if (syscall_type == "PWRITE64") {
            int fd = std::stoi(args[0]);
            int offset = std::stoi(args[2]);
            int size = std::stoi(args[3]);
            std::string content_b64 = args[4];
            
            // Decode Base64
            try {
                std::shared_ptr<char> content = base64_decode(content_b64.c_str(), content_b64.size());
                std::ostringstream content_hex;
                for (int i = 0; i < size; i++) {
                    content_hex << "\\x" << std::hex << std::setw(2) << std::setfill('0') << (content.get()[i] & 0xFF);
                }
                cpp_code.push_back("    pwrite(" + fd_map[fd] + ", \"" + content_hex.str() + "\", " + std::to_string(size) + ", " + std::to_string(offset) + ");");
            } catch (const std::exception &e) {
                std::cerr << "Error decoding Base64 content for PWRITE64 syscall: " << e.what() << std::endl;
                cpp_code.push_back("    // Skipped PWRITE64 syscall for fd " + std::to_string(fd) + " due to Base64 decoding error");
            }
        } else if (syscall_type == "MKDIR") {
            std::string path = replace_substring(args[0], src_dir, dst_dir);
            int mode = std::stoi(args[1]);
            cpp_code.push_back("    mkdir(\"" + path + "\", " + std::to_string(mode) + ");");
        }
    }
    cpp_code.push_back("    return 0;");
    cpp_code.push_back("}");

    std::ofstream outfile(output_file);
    if (!outfile) {
        throw std::runtime_error("Failed to open output file.");
    }

    for (const auto &line : cpp_code) {
        outfile << line << "\n";
    }

    std::cout << "C++ code has been generated in " << output_file << std::endl;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <input_file> <src_dir> <dst_dir> <output_file>" << std::endl;
        return 1;
    }

    std::string input_file = argv[1];
    std::string src_dir = argv[2];
    std::string dst_dir = argv[3];
    std::string output_file = argv[4];

    try {
        parse_trace_and_generate_cpp(input_file, src_dir, dst_dir, output_file);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}