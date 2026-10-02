#pragma once
#include <cstdint>
#include "common.h"

namespace mkdb {

// buffer pe ek "view". size batao toh utne bytes me kaam karega,
// nahi batao toh poore 4096
class SlottedPage {
public:
    explicit SlottedPage(char* buf, uint32_t size = PAGE_SIZE)
        : buf_(buf), size_(size) {}

    // nayi khali page banao (sab zero, header set)
    void init();

    uint16_t num_slots() const;
    uint16_t free_space() const;

    // data ko slot number idx pe daalo, baaki slots ek aage khisak jaate hain
    // jagah nahi hai toh false. zarurat pade toh khud compact kar leta hai
    bool insert_at(uint16_t idx, const char* data, uint16_t len);

    // slot idx hata do. cell ki jagah hole ban jaati hai
    bool remove_at(uint16_t idx);

    // idx wale cell ka data pointer, len me uski lambai. galat idx pe nullptr
    const char* get(uint16_t idx, uint16_t* len) const;

    // holes hata ke saare cells page ke end me jama karo
    void compact();

private:
    char* buf_;
    uint32_t size_;

    uint16_t get16(uint32_t off) const;
    void set16(uint32_t off, uint16_t v);
};

}  // namespace mkdb