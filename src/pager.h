#pragma once
#include <fstream>
#include <string>
#include "common.h"

namespace mkdb {

class Pager {
public:
    // file open karta hai, nahi hai toh bana deta hai
    explicit Pager(const std::string& path);

    // page id ka data out me copy karta hai (out ke paas 4096 bytes honi chahiye)
    void read_page(PageId id, char* out);

    // data ke 4096 bytes page id pe likhta hai
    void write_page(PageId id, const char* data);

    // file ke end me nayi khali page jodta hai, uska id deta hai
    PageId allocate_page();

    uint32_t page_count() const { return num_pages_; }

    // buffer ka data disk pe push karo
    void flush();

private:
    std::fstream file_;
    uint32_t num_pages_ = 0;
};

}  // namespace mkdb