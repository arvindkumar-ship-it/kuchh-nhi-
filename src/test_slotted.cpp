#include <iostream>
#include <cstring>
#include <string>
#include "slotted_page.h"

using namespace mkdb;

static void show(const SlottedPage& sp) {
    for (uint16_t i = 0; i < sp.num_slots(); i++) {
        uint16_t len;
        const char* p = sp.get(i, &len);
        std::cout << "  slot " << i << " = " << std::string(p, len) << "\n";
    }
}

int main() {
    char buf[PAGE_SIZE];
    SlottedPage sp(buf);
    sp.init();

    std::cout << "shuru me free = " << sp.free_space() << "\n";

    sp.insert_at(0, "banana", 6);
    sp.insert_at(0, "apple", 5);    // banana se pehle
    sp.insert_at(2, "cherry", 6);   // sabse end me

    show(sp);
    std::cout << "3 record ke baad free = " << sp.free_space() << "\n";

    // 100 byte ke record tab tak daalo jab tak page full na ho
    std::string big(100, 'x');
    int count = 0;
    while (sp.insert_at(sp.num_slots(), big.c_str(), 100)) count++;

    std::cout << "bade record gaye = " << count << "\n";
    std::cout << "ab free = " << sp.free_space() << "\n";
    return 0;
}