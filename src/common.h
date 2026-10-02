#pragma once
#include <cstdint>

namespace mkdb {

// ek page 4 KiB ka hoga, poore project me yehi chalega
constexpr uint32_t PAGE_SIZE = 4096;

// page ka number. 0 se shuru hota hai
using PageId = uint32_t;

// "koi page nahi" dikhane ke liye, jaise null
constexpr PageId INVALID_PAGE = 0xFFFFFFFF;

}  // namespace mkdb