#include <iostream>
#include <string>
#include "database.h"

using namespace mkdb;

int main(int argc, char** argv) {
    try {
        Database db(argc > 1 ? argv[1] : "mkdb.db");
        std::cout << "mkdb shell. .tables, .exit\n";
        std::string line;
        while (std::cout << "mkdb> " << std::flush, std::getline(std::cin, line)) {
            if (line.empty()) continue;
            if (line == ".exit") break;
            if (line == ".tables") {
                for (const std::string& n : db.table_names()) std::cout << n << "\n";
                continue;
            }
            try {
                std::cout << db.execute(line) << "\n";
            } catch (const std::exception& e) {
                std::cout << "error: " << e.what() << "\n";
            }
        }
    } catch (const std::exception& e) {
        std::cout << "fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}