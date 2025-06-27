/*
 * Simple demonstration of WiredTiger backup using C++ (calls the C API).
 *
 * To compile:
 *    g++ -std=c++11 -o wt_backup_example wt_backup_example.cpp -lwiredtiger
 * Run:
 *    ./wt_backup_example
 *
 * The code creates a WiredTiger "home" directory, populates a few objects,
 * uses a backup cursor to copy them to "COPYDIR", then opens a second
 * WiredTiger connection in that copy directory.
 */

#include <wiredtiger.h>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

static const char *HOME_DIR = "WT_HOME";
static const char *COPY_DIR = "COPYDIR";

// Simple helper function to check return status and exit on error.
static void
check(int ret, const char *msg)
{
    if (ret != 0) {
        std::cerr << msg << ": " << wiredtiger_strerror(ret) << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// Copy a file from srcDir to dstDir with the same filename.
static int
copy_file(const std::string &srcDir, const std::string &dstDir, const std::string &filename)
{
    std::string srcPath = srcDir + "/" + filename;
    std::string dstPath = dstDir + "/" + filename;

    std::ifstream src(srcPath, std::ios::binary);
    if (!src.is_open()) {
        std::cerr << "Cannot open source file: " << srcPath << std::endl;
        return -1;
    }
    std::ofstream dst(dstPath, std::ios::binary);
    if (!dst.is_open()) {
        std::cerr << "Cannot open destination file: " << dstPath << std::endl;
        return -1;
    }
    dst << src.rdbuf();
    return 0;
}

// Create and populate some WiredTiger objects (tables / files) with sample data.
static void
populate_data(WT_SESSION *session)
{
    // Example objects. These mimic the Python test: some "file:" and some "table:" URIs.
    // (We won't create LSM tables for simplicity; WiredTiger must be built with LSM support.)
    const char* objects[] = {
        "file:test_backup.1",
        "file:test_backup.2",
        "table:test_backup.3",
        "table:test_backup.4",
        "table:test_backup.5",
        "table:test_backup.6"
        // If you want LSM or more complex data sets, add them here as needed.
    };

    // Create each table/file and insert some rows.
    for (auto &obj : objects) {
        std::cout << "Creating and populating " << obj << " ..." << std::endl;

        // A simple key-value schema. For "file:", must specify a key format.
        // For "table:", you can omit that if you want a default 'r' row-store.
        // We’ll just do a row-store for everything: "key_format=S,value_format=S".
        std::string createConfig = "key_format=S,value_format=S";
        int ret = session->create(session, obj, createConfig.c_str());
        check(ret, "session.create");

        // Open a cursor, write some data.
        WT_CURSOR *cursor = nullptr;
        ret = session->open_cursor(session, obj, nullptr, nullptr, &cursor);
        check(ret, "session.open_cursor");

        // Insert a few key/value pairs.
        for (int i = 0; i < 5; i++) {
            std::string key = "key" + std::to_string(i);
            std::string val = "value" + std::to_string(i);

            cursor->set_key(cursor, key.c_str());
            cursor->set_value(cursor, val.c_str());
            ret = cursor->insert(cursor);
            check(ret, "cursor.insert");
        }

        ret = cursor->close(cursor);
        check(ret, "cursor.close");
    }
}

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <wiredtiger_db_path>" << std::endl;
        return 1;
    }

    const char* db_path = argv[1];

    mkdir(db_path, 0755);

    std::cout << "Opening database at path: " << db_path << std::endl;

    // Open a connection to WiredTiger in HOME_DIR.
    WT_CONNECTION *conn = nullptr;
    int ret = wiredtiger_open(db_path, nullptr, "create", &conn);
    check(ret, "wiredtiger_open");

    // Open a session.
    WT_SESSION *session = nullptr;
    ret = conn->open_session(conn, nullptr, nullptr, &session);
    check(ret, "conn.open_session");

    // Populate data (mimics the Python classes).
    populate_data(session);

    // Open a backup cursor.
    std::cout << "\nOpening backup cursor..." << std::endl;
    WT_CURSOR *backup_cursor = nullptr;
    ret = session->open_cursor(session, "backup:", nullptr, nullptr, &backup_cursor);
    check(ret, "session.open_cursor(backup:)");

    // Create the COPYDIR directory if it doesn’t exist.

    std::string copy_dir = std::string(db_path) + "/" + COPY_DIR;
    mkdir(copy_dir.c_str(), 0755);

    // Iterate over the backup cursor and copy each file from HOME_DIR to COPY_DIR.
    std::cout << "Copying files listed by the backup cursor..." << std::endl;
    const char *filename = nullptr;
    while ((ret = backup_cursor->next(backup_cursor)) == 0) {
        ret = backup_cursor->get_key(backup_cursor, &filename);
        check(ret, "backup_cursor.get_key");

        std::cout << "  " << filename << std::endl;

        // Copy this file from HOME_DIR to COPYDIR.
        if (copy_file(db_path, copy_dir, filename) != 0) {
            std::cerr << "Failed to copy file: " << filename << std::endl;
            // Not exiting here, but you could handle errors as you wish.
        }
    }
    if (ret != WT_NOTFOUND) {
        // Something else went wrong (not just end of cursor).
        check(ret, "backup_cursor->next error");
    }

    // Close the backup cursor.
    backup_cursor->close(backup_cursor);

    // Close the session and the original connection.
    std::cout << "\nClosing original WiredTiger connection..." << std::endl;
    session->close(session, nullptr);
    conn->close(conn, nullptr);

    // Now open a new WiredTiger connection in COPY_DIR to demonstrate success.
    std::cout << "\nOpening WiredTiger connection in COPYDIR..." << std::endl;
    WT_CONNECTION *copy_conn = nullptr;
    ret = wiredtiger_open(copy_dir.c_str(), nullptr, "create", &copy_conn);
    check(ret, "wiredtiger_open(COPYDIR)");

    // For demonstration, just close it.
    copy_conn->close(copy_conn, nullptr);

    std::cout << "Backup test completed successfully." << std::endl;
    return 0;
}
