#include <iostream>
#include "common.h"

int main() {
    std::cout << "page size = " << mkdb::PAGE_SIZE << "\n";
    mkdb::PageId p = 3;
    std::cout << "page 3 ka file offset = " << (uint64_t)p * mkdb::PAGE_SIZE << "\n";
    return 0;
}