#include "pager.h"
#include <stdexcept>
#include <vector>

namespace mkdb {

Pager::Pager(const std::string& path) {
    // pehle existing file kholne ki koshish
    file_.open(path, std::ios::in | std::ios::out | std::ios::binary);

    if (!file_.is_open()) {
        // file nahi thi, pehle bana lo, phir dobara kholo
        std::ofstream create(path, std::ios::binary);
        create.close();
        file_.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file_.is_open()) {
        throw std::runtime_error("file khul nahi payi: " + path);
    }

    // end me jaake size dekho
    file_.seekg(0, std::ios::end);
    uint64_t size = (uint64_t)file_.tellg();

    if (size % PAGE_SIZE != 0) {
        throw std::runtime_error("file ka size 4096 ka multiple nahi, file kharab hai");
    }
    num_pages_ = (uint32_t)(size / PAGE_SIZE);
}

void Pager::read_page(PageId id, char* out) {
    if (id >= num_pages_) {
        throw std::runtime_error("read: page exist nahi karti");
    }
    file_.clear();  // pichhla koi error flag ho toh hata do
    file_.seekg((uint64_t)id * PAGE_SIZE);
    file_.read(out, PAGE_SIZE);
    if (!file_) {
        throw std::runtime_error("read fail ho gaya");
    }
}

void Pager::write_page(PageId id, const char* data) {
    if (id >= num_pages_) {
        throw std::runtime_error("write: pehle allocate_page karo");
    }
    file_.clear();
    file_.seekp((uint64_t)id * PAGE_SIZE);
    file_.write(data, PAGE_SIZE);
    if (!file_) {
        throw std::runtime_error("write fail ho gaya");
    }
}

PageId Pager::allocate_page() {
    PageId id = num_pages_;
    std::vector<char> zeros(PAGE_SIZE, 0);

    file_.clear();
    file_.seekp((uint64_t)id * PAGE_SIZE);
    file_.write(zeros.data(), PAGE_SIZE);
    if (!file_) {
        throw std::runtime_error("allocate fail ho gaya");
    }

    num_pages_++;
    return id;
}

void Pager::flush() {
    file_.flush();
}

}  // namespace mkdb