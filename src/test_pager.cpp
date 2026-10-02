#include <iostream>
#include <cstring>
#include <cstdio>
#include "pager.h"

using namespace mkdb;

int main() {
    std::remove("test.db");  // purani file hata do

    {
        Pager p("test.db");
        PageId a = p.allocate_page();
        PageId b = p.allocate_page();

        char buf[PAGE_SIZE] = {0};
        std::strcpy(buf, "hello page zero");
        p.write_page(a, buf);

        std::memset(buf, 0, PAGE_SIZE);
        std::strcpy(buf, "hello page one");
        p.write_page(b, buf);

        p.flush();
    }  // yahan Pager khatam, file band

    // dobara kholke dekho data bacha ya nahi
    Pager p2("test.db");
    std::cout << "pages = " << p2.page_count() << "\n";

    char out[PAGE_SIZE];
    p2.read_page(0, out);
    std::cout << "page 0: " << out << "\n";
    p2.read_page(1, out);
    std::cout << "page 1: " << out << "\n";

    return 0;
}