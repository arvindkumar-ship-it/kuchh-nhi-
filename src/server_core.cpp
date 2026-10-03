#include "server_core.h"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  static const sock_t BAD_SOCK = INVALID_SOCKET;
  typedef int socklen_t_;
#else
  #include <pthread.h>
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <sys/time.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
  static const sock_t BAD_SOCK = -1;
  typedef socklen_t socklen_t_;
#endif

namespace mkdb {

// std::mutex/std::thread MinGW (win32 threads) me nahi milte, isliye seedhe OS ke apne
#ifdef _WIN32
struct Mutex {
    CRITICAL_SECTION cs;
    Mutex() { InitializeCriticalSection(&cs); }
    void lock() { EnterCriticalSection(&cs); }
    void unlock() { LeaveCriticalSection(&cs); }
};
#else
struct Mutex {
    pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    void lock() { pthread_mutex_lock(&m); }
    void unlock() { pthread_mutex_unlock(&m); }
};
#endif

struct Server::Impl {
    Database* db;
    Mutex mu;
    sock_t ls = BAD_SOCK;
    std::atomic<bool> stopping{false};
    std::string host;
};

struct Conn {
    sock_t cs;
    Database* db;
    Mutex* mu;
};

// ek line (statement) ki max lambai
static const size_t MAX_LINE = 1u << 20;  // 1 MiB
// khuli transaction wale client ko itni der chup rehne do, phir kaat do
static const int TXN_IDLE_MS = 60000;

// recv ka timeout. ms = 0 matlab bina timeout
static void set_recv_timeout(sock_t s, int ms) {
#ifdef _WIN32
    DWORD t = (DWORD)ms;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof(t));
#else
    struct timeval tv;
    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

static bool send_all(sock_t s, const std::string& d) {
    size_t off = 0;
    while (off < d.size()) {
        int n = (int)send(s, d.data() + off, (int)(d.size() - off), 0);
        if (n <= 0) return false;
        off += (size_t)n;
    }
    return true;
}

static void handle(sock_t cs, Database* db, Mutex* mu) {
    bool held = false;
    std::string buf;
    char tmp[4096];
    bool alive = true;
    while (alive) {
        // lock tabhi pakda hota hai jab txn khuli ho, tab idle client ko time-limit
        set_recv_timeout(cs, held ? TXN_IDLE_MS : 0);
        int n = (int)recv(cs, tmp, sizeof(tmp), 0);
        if (n <= 0) break;  // band hua, error, ya txn me bahut der chup raha
        buf.append(tmp, (size_t)n);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos) {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            if (line == "QUIT" || line == ".exit") { alive = false; break; }

            std::string reply;
            if (!held) { mu->lock(); held = true; }
            try {
                std::string out = db->execute(line);
                reply = "$" + std::to_string(out.size()) + "\r\n" + out + "\r\n";
            } catch (const std::exception& e) {
                std::string m = e.what();
                for (char& c : m) if (c == '\n' || c == '\r') c = ' ';
                reply = "-ERR " + m + "\r\n";
            }
            if (!db->in_transaction()) { mu->unlock(); held = false; }  // txn khuli hai toh lock rakho
            if (!send_all(cs, reply)) { alive = false; break; }
        }

        // newline aaya hi nahi aur buffer bahut bada ho gaya
        if (alive && buf.size() > MAX_LINE) {
            send_all(cs, "-ERR line bahut lambi hai\r\n");
            alive = false;
        }
    }
    if (!held) { mu->lock(); held = true; }
    if (db->in_transaction()) {
        try { db->execute("ROLLBACK"); } catch (...) {}
    }
    mu->unlock();
    CLOSESOCK(cs);
}

#ifdef _WIN32
static DWORD WINAPI conn_main(LPVOID p) {
    Conn* c = (Conn*)p;
    handle(c->cs, c->db, c->mu);
    delete c;
    return 0;
}
static void spawn(LPTHREAD_START_ROUTINE fn, void* arg) {
    HANDLE h = CreateThread(nullptr, 0, fn, arg, 0, nullptr);
    if (h) CloseHandle(h);
}
static DWORD WINAPI bg_main(LPVOID p) {
    ((Server*)p)->serve_forever();
    return 0;
}
#else
static void* conn_main(void* p) {
    Conn* c = (Conn*)p;
    handle(c->cs, c->db, c->mu);
    delete c;
    return nullptr;
}
static void spawn(void* (*fn)(void*), void* arg) {
    pthread_t t;
    if (pthread_create(&t, nullptr, fn, arg) == 0) pthread_detach(t);
}
static void* bg_main(void* p) {
    ((Server*)p)->serve_forever();
    return nullptr;
}
#endif

Server::Server(Database& db) : impl_(new Impl) {
    impl_->db = &db;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

Server::~Server() {
    stop();
    delete impl_;
}

bool Server::start(const std::string& host, int port) {
    impl_->host = host;
    sock_t ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls == BAD_SOCK) return false;
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) { CLOSESOCK(ls); return false; }
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(ls, 16) != 0) { CLOSESOCK(ls); return false; }

    sockaddr_in got;
    socklen_t_ len = sizeof(got);
    getsockname(ls, (sockaddr*)&got, &len);
    port_ = ntohs(got.sin_port);
    impl_->ls = ls;
    return true;
}

void Server::serve_forever() {
    while (!impl_->stopping) {
        sock_t cs = accept(impl_->ls, nullptr, nullptr);
        if (impl_->stopping) {
            if (cs != BAD_SOCK) CLOSESOCK(cs);
            break;
        }
        if (cs == BAD_SOCK) continue;
        spawn(conn_main, new Conn{cs, impl_->db, &impl_->mu});
    }
}

void Server::start_background() { spawn(bg_main, this); }

void Server::stop() {
    if (impl_->ls == BAD_SOCK || impl_->stopping) return;
    impl_->stopping = true;
    // accept() ko jagane ke liye khud se ek connection
    sock_t c = socket(AF_INET, SOCK_STREAM, 0);
    if (c != BAD_SOCK) {
        sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons((unsigned short)port_);
        addr.sin_addr.s_addr = inet_addr(impl_->host.c_str());
        connect(c, (sockaddr*)&addr, sizeof(addr));
        CLOSESOCK(c);
    }
    // thoda ruko taaki accept thread nikal jaye, phir listen socket band
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif
    CLOSESOCK(impl_->ls);
    impl_->ls = BAD_SOCK;
}

}  // namespace mkdb