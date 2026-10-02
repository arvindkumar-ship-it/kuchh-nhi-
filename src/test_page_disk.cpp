#include <iostream>
#include <string>
#include <cstdio>
#include "pager.h"
#include "slotted_page.h"

using namespace mkdb;

static char first_char(const SlottedPage& sp, uint16_t idx) {
    uint16_t len;
    const char* p = sp.get(idx, &len);
    return p[0];
}

int main() {
    std::remove("page_test.db");

    char buf[PAGE_SIZE];
    SlottedPage sp(buf);
    sp.init();

    // 100 byte ke records se page bharo, har record ka pehla akshar alag
    std::string rec(100, 'x');
    int count = 0;
    while (true) {
        rec[0] = 'a' + count % 26;
        if (!sp.insert_at(sp.num_slots(), rec.c_str(), 100)) break;
        count++;
    }
    std::cout << "bhare = " << count << ", free = " << sp.free_space() << "\n";

    // beech ka ek record hatao
    sp.remove_at(5);
    std::cout << "remove ke baad free = " << sp.free_space() << "\n";

    // page "full" dikh raha hai, par insert compact karke jagah bana lega
    rec[0] = 'Z';
    bool ok = sp.insert_at(5, rec.c_str(), 100);
    std::cout << "dobara insert hua = " << ok << ", free = " << sp.free_space() << "\n";

    std::cout << "slot 5 ka pehla akshar = " << first_char(sp, 5) << "\n";
    std::cout << "slot 6 ka pehla akshar = " << first_char(sp, 6) << "\n";

    // ab isi page ko disk pe likho aur wapas padho
    {
        Pager p("page_test.db");
        PageId id = p.allocate_page();
        p.write_page(id, buf);
        p.flush();
    }

    Pager p2("page_test.db");
    char buf2[PAGE_SIZE];
    p2.read_page(0, buf2);
    SlottedPage sp2(buf2);
    std::cout << "disk se: slots = " << sp2.num_slots()
              << ", slot 5 = " << first_char(sp2, 5) << "\n";
    return 0;
}