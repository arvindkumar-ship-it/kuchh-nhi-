#pragma once
#include <cstddef>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "common.h"

namespace mkdb {

// Page cache (Clock replacement) + rollback journal + freelist + fsync.
class Pager {
public:
    explicit Pager(const std::string& path, size_t cache_pages = 64);
    ~Pager();
    Pager(const Pager&) = delete;
    Pager& operator=(const Pager&) = delete;

    void read_page(PageId id, char* out);
    void write_page(PageId id, const char* data);
    PageId allocate_page();  // freelist ho toh pehle wahan se, warna file ke end me
    uint32_t page_count() const { return num_pages_; }

    // Freelist opt-in. Page 0 ke bytes 16..19 me free list ka head rehta hai, isliye
    // page 0 ko header banane wala (Database) hi ise on karta hai.
    void enable_freelist() { freelist_ = true; }
    // page wapas free list me (freelist off ho toh kuch nahi: page leak)
    void free_page(PageId id);

    // explicit transaction nahi hai toh flush() = commit.
    // BEGIN ke baad flush() kuch nahi karta, commit() hi final karta hai
    void flush();

    void begin();
    void commit();
    // true agar kuch undo hua. journal se purane pages wapas, cache saaf
    bool rollback();
    bool in_txn() const { return in_txn_; }

    size_t cache_hits() const { return hits_; }
    size_t cache_misses() const { return misses_; }
    size_t cache_capacity() const { return frames_.size(); }

private:
    struct Frame {
        PageId id = INVALID_PAGE;
        bool valid = false;
        bool ref = false;    // clock reference bit
        bool dirty = false;  // disk se alag hai
        std::vector<char> data;
    };

    std::string path_, jpath_;
    FILE* file_ = nullptr;
    uint32_t num_pages_ = 0;

    std::vector<Frame> frames_;
    std::unordered_map<PageId, size_t> map_;
    size_t hand_ = 0;
    size_t hits_ = 0, misses_ = 0;

    bool freelist_ = false;

    bool in_txn_ = false;
    bool journal_open_ = false;
    bool journal_unsynced_ = false;  // journal me naya likha jo fsync nahi hua
    uint32_t orig_pages_ = 0;
    FILE* jfile_ = nullptr;
    std::unordered_set<PageId> journaled_;

    void open_file();
    void close_file();
    void raw_read(PageId id, char* out);
    void raw_write(PageId id, const char* data);
    void sync_journal();
    void sync_file();
    size_t evict_slot();
    size_t install(PageId id);  // id ke liye frame, map me daal ke
    void open_journal();
    void journal_page(PageId id);
    void restore_from_journal();
    PageId free_head();
    void set_free_head(PageId h);
};

}  // namespace mkdb