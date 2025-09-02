// check_persistence.cpp
#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <data_dir>\n";
        return 1;
    }
    std::string data_dir = argv[1];

    fs::path dir = data_dir;
    fs::path f1  = dir / "f1.txt";
    bool rename_applied = false;

    // Check if a renamed f2 file exists
    for (auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file() &&
            entry.path().filename().string().find("f2.txt") != std::string::npos &&
            entry.path().filename() != "f2.txt") {
            std::cout << "rename_applied: " << rename_applied << std::endl;
            rename_applied = true;
            break;
        }
    }

    // Check if "Fn4: write to f1" is in f1.txt
    bool f1_has_write = false;
    std::ifstream fin(f1);
    if (fin) {
        std::string line;
        while (std::getline(fin, line)) {
            std::cout << "line: " << line << std::endl;
            if (line.find("Fn4: write to f1") != std::string::npos) {
                std::cout << "f1_has_write: " << f1_has_write << std::endl;
                f1_has_write = true;
            }
        }
    }

    // Report result
    if (rename_applied && !f1_has_write) {
        std::cout << "[POSSIBLE ANOMALY] rename persisted but f1 write did not.\n";
        return 1; // anomaly
    } else {
        std::cout << "[OK] Either rename not applied, or f1 write present.\n";
        return 0; // normal
    }
}
