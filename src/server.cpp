// mkdb TCP server: mkdb_server [dbfile] [port] [host]   (protocol: server_core.h)
#include <cstdlib>
#include <iostream>
#include <string>
#include "server_core.h"

int main(int argc, char** argv) {
    std::string path = argc > 1 ? argv[1] : "mkdb.db";
    int port = argc > 2 ? std::atoi(argv[2]) : 7878;
    std::string host = argc > 3 ? argv[3] : "127.0.0.1";  // default sirf local machine
    try {
        mkdb::Database db(path);
        mkdb::Server srv(db);
        if (!srv.start(host, port)) {
            std::cerr << "bind/listen fail (port " << port << " busy ya host galat?)\n";
            return 1;
        }
        std::cout << "mkdb server " << host << ":" << srv.port() << " db=" << path << std::endl;
        srv.serve_forever();
    } catch (const std::exception& e) {
        std::cerr << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}