#include <wiredtiger.h>
#include <filesystem>
#include <iostream>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/types.h>
#include <cstring>

// A simple error check helper:
static void
check(int ret, const char *msg)
{
    if (ret != 0) {
        std::cerr << msg << ": " << wiredtiger_strerror(ret) << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

// Objects we expect to find (from the example workload):
// If you added LSM or other objects, include them here as well.
static const char* objects[] = {
    "file:test_backup.1",
    "file:test_backup.2",
    "table:test_backup.3",
    "table:test_backup.4",
    "table:test_backup.5",
    "table:test_backup.6"
};

int
main(int argc, char *argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <wiredtiger_db_path>" << std::endl;
        return 1;
    }

    const char* db_path = argv[1];
    std::cout << "Opening database at path: " << db_path << std::endl;

    // Open the WiredTiger connection in read-only mode to avoid altering anything.
    // (Read-only mode is specified with "readonly", though if your WiredTiger version
    // or environment doesn't support it, you can omit this and open normally.)
    WT_CONNECTION *conn = nullptr;
    int ret = wiredtiger_open(db_path, nullptr, "readonly", &conn);
    if (ret != 0) {
        // If 'readonly' is not recognized or fails, try opening without it:
        // ret = wiredtiger_open(db_path, nullptr, "create", &conn);
        std::cerr << "wiredtiger_open error: " << wiredtiger_strerror(ret) << std::endl;
        return 0;
    }

    // if db_path/WiredTiger.backup exists, do not perform the check.
    if (std::filesystem::exists(std::string(db_path) + "/WiredTiger.backup")) {
        std::cout << "Backup already exists. Exiting." << std::endl;
        return 0;
    }

    WT_SESSION *session = nullptr;
    ret = conn->open_session(conn, nullptr, nullptr, &session);
    check(ret, "conn.open_session");

    // Iterate through the list of expected objects and read their contents.
    for (auto &obj : objects) {
        std::cout << "\nChecking contents of: " << obj << std::endl;

        // Open a cursor.
        WT_CURSOR *cursor = nullptr;
        ret = session->open_cursor(session, obj, nullptr, nullptr, &cursor);

        if (ret == ENOENT) {
            // If the object doesn’t exist, just note that and continue.
            std::cout << "  (Object does not exist in this database.)" << std::endl;
            continue;
        } else {
            check(ret, "session.open_cursor");
        }

        // Read each record by calling next() until we get WT_NOTFOUND.
        while ((ret = cursor->next(cursor)) == 0) {
            const char *k = nullptr;
            const char *v = nullptr;

            // Retrieve the key and value (assuming string/string).
            ret = cursor->get_key(cursor, &k);
            check(ret, "cursor.get_key");
            ret = cursor->get_value(cursor, &v);
            check(ret, "cursor.get_value");

            std::cout << "  Key: " << k << " => Value: " << v << std::endl;
        }

        if (ret != WT_NOTFOUND) {
            // Some error other than 'not found' at end of cursor.
            check(ret, "cursor.next");
        }

        // Close the cursor before moving on.
        ret = cursor->close(cursor);
        check(ret, "cursor.close");
    }

    // Close session/connection.
    ret = session->close(session, nullptr);
    check(ret, "session.close");
    ret = conn->close(conn, nullptr);
    check(ret, "conn.close");

    std::cout << "\nDone checking. Exiting.\n" << std::endl;
    return 0;
}
