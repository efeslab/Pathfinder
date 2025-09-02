// posix_persistence_demo.cpp
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <iostream>

static void terminate(const char* msg) {
    perror(msg);
    std::exit(1);
}

static void append_write(const std::string& path, const std::string& data) {
    int fd = ::open(path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) terminate(("open " + path).c_str());

    const char* p = data.c_str();
    ssize_t nleft = (ssize_t)data.size();
    while (nleft > 0) {
        ssize_t n = ::write(fd, p, (size_t)nleft);
        if (n < 0) terminate(("write " + path).c_str());
        nleft -= n;
        p += n;
    }
    if (::close(fd) < 0) terminate(("close " + path).c_str());
}

// Fn2(f) { write(f); write(f); }
static void Fn2(const std::string& f) {
    append_write(f, "Fn2: first line\n");
    append_write(f, "Fn2: second line\n");
}

// Fn4(f1,f2) { write(f1); write(f2); fdatasync(f2); }
static void Fn4(const std::string& f1, const std::string& f2) {
    append_write(f1, "Fn4: write to f1\n");
    // open f2 and keep the fd to call fdatasync on it
    int fd2 = ::open(f2.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd2 < 0) terminate(("open " + f2).c_str());
    const char* data = "Fn4: write to f2\n";
    append_write(f2, data);
    if (::fdatasync(fd2) < 0) terminate(("fdatasync " + f2).c_str());
    if (::close(fd2) < 0) terminate(("close " + f2).c_str());
}

// Fn5(f) { rename(f); sync(); }
static void Fn5(const std::string& f) {
    std::string newname = f + ".renamed";
    if (::rename(f.c_str(), newname.c_str()) < 0) terminate(("rename " + f).c_str());
    ::sync();  // flush filesystem metadata to disk
}

// Fn3(f1,f2,rename_flag) { Fn4(f1); if (rename_flag) Fn5(f2); }
static void Fn3(const std::string& f1, const std::string& f2, bool rename_flag) {
    Fn4(f1, f2);
    if (rename_flag) Fn5(f2);
}

// Fn1() {
//   Fn2(f1);
//   Fn3(f1,f2,true);
//   ...
//   Fn2(f7);make 
//   Fn3(f7,f8,false);
// }
static void Fn1(const std::string& dir) {
    std::string f1 = dir + "/f1.txt";
    std::string f2 = dir + "/f2.txt";
    std::string f7 = dir + "/f7.txt";
    std::string f8 = dir + "/f8.txt";

    Fn2(f1);
    Fn3(f1, f2, true);

    // (Ellipsis in the figure — do anything else you want in between)

    Fn2(f7);
    Fn3(f7, f8, false);

    std::cout << "Done. See files under ./" << dir << "\n";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <data_dir>\n";
        return 1;
    }
    std::string data_dir = argv[1];

    std::string dir = data_dir;
    ::mkdir(dir.c_str(), 0755); // ignore errors if exists

    Fn1(dir);
    return 0;
}
