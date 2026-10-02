#include "slotted_page.h"
#include <cstring>

namespace mkdb {

// header me kahan kya hai
static const uint32_t OFF_NUM_SLOTS = 0;
static const uint32_t OFF_FREE_END  = 2;
static const uint32_t HEADER_SIZE   = 4;
static const uint32_t SLOT_SIZE     = 2;
static const uint32_t CELL_LEN_SIZE = 2;

uint16_t SlottedPage::get16(uint32_t off) const {
    uint16_t v;
    std::memcpy(&v, buf_ + off, 2);
    return v;
}

void SlottedPage::set16(uint32_t off, uint16_t v) {
    std::memcpy(buf_ + off, &v, 2);
}

void SlottedPage::init() {
    std::memset(buf_, 0, size_);
    set16(OFF_NUM_SLOTS, 0);
    set16(OFF_FREE_END, (uint16_t)size_);  // cells peeche se aayenge
}

uint16_t SlottedPage::num_slots() const {
    return get16(OFF_NUM_SLOTS);
}

uint16_t SlottedPage::free_space() const {
    uint32_t slots_end = HEADER_SIZE + num_slots() * SLOT_SIZE;
    return get16(OFF_FREE_END) - slots_end;
}

bool SlottedPage::insert_at(uint16_t idx, const char* data, uint16_t len) {
    uint16_t n = num_slots();
    if (idx > n) return false;

    // cell (len + data) aur ek naya slot, dono ki jagah chahiye
    uint32_t need = CELL_LEN_SIZE + len + SLOT_SIZE;
    if (free_space() < need) {
        // shayad holes me jagah ho, compact karke dobara dekho
        compact();
        if (free_space() < need) return false;
    }

    // cell peeche se left me rakho
    uint16_t new_end = get16(OFF_FREE_END) - (CELL_LEN_SIZE + len);
    set16(new_end, len);
    std::memcpy(buf_ + new_end + CELL_LEN_SIZE, data, len);

    // idx ke baad ke slots ko ek slot aage khiskao
    char* slot_pos = buf_ + HEADER_SIZE + idx * SLOT_SIZE;
    std::memmove(slot_pos + SLOT_SIZE, slot_pos, (n - idx) * SLOT_SIZE);

    // naya slot likho
    set16(HEADER_SIZE + idx * SLOT_SIZE, new_end);

    set16(OFF_NUM_SLOTS, n + 1);
    set16(OFF_FREE_END, new_end);
    return true;
}

bool SlottedPage::remove_at(uint16_t idx) {
    uint16_t n = num_slots();
    if (idx >= n) return false;

    // idx ke baad ke slots ko ek peeche khiskao, slot khatam
    char* slot_pos = buf_ + HEADER_SIZE + idx * SLOT_SIZE;
    std::memmove(slot_pos, slot_pos + SLOT_SIZE, (n - idx - 1) * SLOT_SIZE);

    set16(OFF_NUM_SLOTS, n - 1);
    // cell ko haath nahi lagaya, wo ab hole hai
    return true;
}

void SlottedPage::compact() {
    char tmp[PAGE_SIZE];
    uint16_t n = num_slots();
    uint16_t end = (uint16_t)size_;

    for (uint16_t i = 0; i < n; i++) {
        uint16_t off = get16(HEADER_SIZE + i * SLOT_SIZE);
        uint16_t total = CELL_LEN_SIZE + get16(off);

        end -= total;
        std::memcpy(tmp + end, buf_ + off, total);
        set16(HEADER_SIZE + i * SLOT_SIZE, end);  // slot ko naya offset
    }

    // jama kiye hue cells wapas page me
    std::memcpy(buf_ + end, tmp + end, size_ - end);
    set16(OFF_FREE_END, end);
}

const char* SlottedPage::get(uint16_t idx, uint16_t* len) const {
    if (idx >= num_slots()) return nullptr;
    uint16_t off = get16(HEADER_SIZE + idx * SLOT_SIZE);
    *len = get16(off);
    return buf_ + off + CELL_LEN_SIZE;
}

}  // namespace mkdb