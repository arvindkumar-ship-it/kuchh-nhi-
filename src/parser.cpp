#include "pager.h"
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
  #include <io.h>
  #include <windows.h>
  #define SEEK64(f, off, wh) _fseeki64(f, (long long)(off), wh)
  #define TELL64(f) _ftelli64(f)
  #define FSYNC(f) _commit(_fileno(f))
#else
  #include <sys/types.h>
  #include <unistd.h>
  #define SEEK64(f, off, wh) fseeko(f, (off_t)(off), wh)
  #define TELL64(f) ftello(f)
  #define FSYNC(f) fsync(fileno(f))
#endif

namespace mkdb {

static const uint32_t JMAGIC = 0x314A4B4D;  // "MKJ1"
static const size_t FREE_HEAD_OFF = 16;      // page 0 me freelist head

static bool file_exists(const std::string& p) {
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fclose(f);
    return true;
}
static void remove_file(const std::string& p) { std::remove(p.c_str()); }

// file ko size tak chhota karo (file band honi chahiye)
static void truncate_file(const std::string& p, uint64_t size) {
#ifdef _WIN32
    HANDLE h = CreateFileA(p.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) throw std::runtime_error("truncate: file khuli nahi");
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)size;
    bool ok = SetFilePointerEx(h, li, nullptr, FILE_BEGIN) && SetEndOfFile(h);
    CloseHandle(h);
    if (!ok) throw std::runtime_error("truncate fail");
#else
    if (truncate(p.c_str(), (off_t)size) != 0) throw std::runtime_error("truncate fail");
#endif
}

Pager::Pager(const std::string& path, size_t cache_pages)
    : path_(path), jpath_(path + "-journal"), frames_(cache_pages ? cache_pages : 1) {
    open_file();

    // pichhli baar crash hua tha toh adhura journal pada hoga: pehle undo
    if (file_exists(jpath_)) restore_from_journal();

    SEEK64(file_, 0, SEEK_END);
    long long size = TELL64(file_);
    if (size < 0 || size % PAGE_SIZE != 0) {
        throw std::runtime_error("file ka size 4096 ka multiple nahi, file kharab hai");
    }
    num_pages_ = (uint32_t)(size / PAGE_SIZE);
}

Pager::~Pager() {
    // explicit txn khuli hai toh journal chhod do (agli open pe rollback = crash jaisa)
    try {
        if (!in_txn_) commit();
    } catch (...) {
    }
    close_file();
    if (jfile_) fclose(jfile_);
}

void Pager::open_file() {
    file_ = fopen(path_.c_str(), "r+b");
    if (!file_) {
        FILE* c = fopen(path_.c_str(), "wb");
        if (c) fclose(c);
        file_ = fopen(path_.c_str(), "r+b");
    }
    if (!file_) throw std::runtime_error("file khul nahi payi: " + path_);
}

void Pager::close_file() {
    if (file_) {
        fclose(file_);
        file_ = nullptr;
    }
}

void Pager::raw_read(PageId id, char* out) {
    if (SEEK64(file_, (uint64_t)id * PAGE_SIZE, SEEK_SET) != 0 || fread(out, 1, PAGE_SIZE, file_) != PAGE_SIZE)
        throw std::runtime_error("read fail ho gaya");
}

// db file me koi bhi page likhne se pehle journal disk pe pakka hona chahiye
void Pager::raw_write(PageId id, const char* data) {
    sync_journal();
    if (SEEK64(file_, (uint64_t)id * PAGE_SIZE, SEEK_SET) != 0 || fwrite(data, 1, PAGE_SIZE, file_) != PAGE_SIZE)
        throw std::runtime_error("write fail ho gaya");
}

void Pager::sync_journal() {
    if (!journal_open_ || !journal_unsynced_) return;
    if (fflush(jfile_) != 0 || FSYNC(jfile_) != 0) throw std::runtime_error("journal fsync fail");
    journal_unsynced_ = false;
}

void Pager::sync_file() {
    if (fflush(file_) != 0 || FSYNC(file_) != 0) throw std::runtime_error("db fsync fail");
}

// ---------- clock cache ----------

size_t Pager::evict_slot() {
    while (true) {
        size_t cur = hand_;
        hand_ = (hand_ + 1) % frames_.size();
        Frame& f = frames_[cur];
        if (!f.valid) return cur;
        if (f.ref) {  // doosra mauka
            f.ref = false;
            continue;
        }
        if (f.dirty) {
            raw_write(f.id, f.data.data());
            f.dirty = false;
        }
        map_.erase(f.id);
        f.valid = false;
        return cur;
    }
}

size_t Pager::install(PageId id) {
    size_t slot = evict_slot();
    Frame& f = frames_[slot];
    if (f.data.empty()) f.data.resize(PAGE_SIZE);
    f.id = id;
    f.valid = true;
    f.ref = true;
    f.dirty = false;
    map_[id] = slot;
    return slot;
}

void Pager::read_page(PageId id, char* out) {
    if (id >= num_pages_) throw std::runtime_error("read: page exist nahi karti");
    auto it = map_.find(id);
    if (it != map_.end()) {
        hits_++;
        Frame& f = frames_[it->second];
        f.ref = true;
        std::copy(f.data.begin(), f.data.end(), out);
        return;
    }
    misses_++;
    size_t slot = install(id);
    Frame& f = frames_[slot];
    raw_read(id, f.data.data());
    std::copy(f.data.begin(), f.data.end(), out);
}

void Pager::write_page(PageId id, const char* data) {
    if (id >= num_pages_) throw std::runtime_error("write: pehle allocate_page karo");
    journal_page(id);  // badalne se pehle purani copy journal me
    auto it = map_.find(id);
    size_t slot = (it != map_.end()) ? it->second : install(id);
    Frame& f = frames_[slot];
    std::copy(data, data + PAGE_SIZE, f.data.begin());
    f.ref = true;
    f.dirty = true;
}

// ---------- freelist ----------

PageId Pager::free_head() {
    char b[PAGE_SIZE];
    read_page(0, b);
    PageId h;
    std::memcpy(&h, b + FREE_HEAD_OFF, 4);
    return h;  // 0 = khali (page 0 kabhi free nahi hota)
}

void Pager::set_free_head(PageId h) {
    char b[PAGE_SIZE];
    read_page(0, b);
    std::memcpy(b + FREE_HEAD_OFF, &h, 4);
    write_page(0, b);
}

void Pager::free_page(PageId id) {
    if (!freelist_) return;
    if (id == 0 || id >= num_pages_) throw std::runtime_error("free_page: galat page");
    char b[PAGE_SIZE] = {0};
    PageId head = free_head();
    std::memcpy(b, &head, 4);  // purana head is page ke baad
    write_page(id, b);
    set_free_head(id);
}

PageId Pager::allocate_page() {
    open_journal();  // orig_pages_ pehle note ho, phir count badhe
    if (freelist_ && num_pages_ > 0) {
        PageId h = free_head();
        if (h != 0) {
            char b[PAGE_SIZE];
            read_page(h, b);
            PageId next;
            std::memcpy(&next, b, 4);
            set_free_head(next);
            char zeros[PAGE_SIZE] = {0};
            write_page(h, zeros);
            return h;
        }
    }
    PageId id = num_pages_++;
    size_t slot = install(id);
    Frame& f = frames_[slot];
    std::fill(f.data.begin(), f.data.end(), 0);
    f.dirty = true;
    return id;
}

// ---------- journal / transaction ----------

void Pager::open_journal() {
    if (journal_open_) return;
    jfile_ = fopen(jpath_.c_str(), "wb");
    if (!jfile_) throw std::runtime_error("journal ban nahi payi");
    orig_pages_ = num_pages_;
    uint32_t magic = JMAGIC;
    if (fwrite(&magic, 1, 4, jfile_) != 4 || fwrite(&orig_pages_, 1, 4, jfile_) != 4)
        throw std::runtime_error("journal header likh nahi paya");
    journal_open_ = true;
    journal_unsynced_ = true;
    journaled_.clear();
}

void Pager::journal_page(PageId id) {
    open_journal();
    if (id >= orig_pages_ || journaled_.count(id)) return;
    // is txn me ye page abhi tak badli nahi, toh disk wali copy hi original hai
    std::vector<char> buf(PAGE_SIZE);
    raw_read(id, buf.data());
    if (fwrite(&id, 1, 4, jfile_) != 4 || fwrite(buf.data(), 1, PAGE_SIZE, jfile_) != PAGE_SIZE)
        throw std::runtime_error("journal write fail");
    journal_unsynced_ = true;  // fsync tab hoga jab db file me pehla page likhna ho
    journaled_.insert(id);
}

void Pager::begin() {
    if (in_txn_) throw std::runtime_error("transaction pehle se chal rahi hai");
    commit();  // koi implicit kaam pending ho toh pehle final
    in_txn_ = true;
}

void Pager::commit() {
    for (Frame& f : frames_) {
        if (f.valid && f.dirty) {
            raw_write(f.id, f.data.data());
            f.dirty = false;
        }
    }
    sync_file();  // db file disk pe pakki hone ke BAAD hi journal hatao
    if (journal_open_) {
        fclose(jfile_);
        jfile_ = nullptr;
        remove_file(jpath_);
        journal_open_ = false;
        journal_unsynced_ = false;
        journaled_.clear();
    }
    in_txn_ = false;
}

void Pager::flush() {
    if (in_txn_) return;
    commit();
}

bool Pager::rollback() {
    bool had = journal_open_;
    in_txn_ = false;
    if (!had) return false;
    sync_journal();
    fclose(jfile_);
    jfile_ = nullptr;
    journal_open_ = false;
    journaled_.clear();
    for (Frame& f : frames_) f.valid = f.dirty = f.ref = false;
    map_.clear();
    hand_ = 0;
    restore_from_journal();
    return true;
}

// journal ke pages db file pe wapas, file purane size pe, journal delete
void Pager::restore_from_journal() {
    FILE* j = fopen(jpath_.c_str(), "rb");
    if (j) {
        uint32_t magic = 0, orig = 0;
        bool ok = fread(&magic, 1, 4, j) == 4 && fread(&orig, 1, 4, j) == 4 && magic == JMAGIC;
        if (ok) {
            std::vector<char> buf(PAGE_SIZE);
            uint32_t id;
            // adhuri aakhri entry (crash beech me) ignore: tab tak db me us page ko haath nahi laga tha
            while (fread(&id, 1, 4, j) == 4 && fread(buf.data(), 1, PAGE_SIZE, j) == PAGE_SIZE) {
                if (SEEK64(file_, (uint64_t)id * PAGE_SIZE, SEEK_SET) != 0 ||
                    fwrite(buf.data(), 1, PAGE_SIZE, file_) != PAGE_SIZE)
                    throw std::runtime_error("recovery: write fail");
            }
            sync_file();
            close_file();
            truncate_file(path_, (uint64_t)orig * PAGE_SIZE);
            open_file();
            sync_file();
            num_pages_ = orig;
        }
        fclose(j);
    }
    remove_file(jpath_);
}

}  // namespace mkdb