// TCP server automated test: asli socket client se
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include "server_core.h"
#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  #define CLOSESOCK closesocket
  static void sleep_ms(int ms) { Sleep(ms); }
#else
  #include <arpa/inet.h>
  #include <netinet/in.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int sock_t;
  #define CLOSESOCK close
  static void sleep_ms(int ms) { usleep(ms * 1000); }
#endif

using namespace mkdb;
#define CHECK(c) do { if (!(c)) { std::cout << "FAIL line " << __LINE__ << ": " #c "\n"; return 1; } } while (0)
static bool has(const std::string& s, const std::string& t) { return s.find(t) != std::string::npos; }

static sock_t connect_to(int port) {
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a;
    std::memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(s, (sockaddr*)&a, sizeof(a)) != 0) return (sock_t)-1;
    return s;
}

static std::string read_line(sock_t s) {
    std::string l;
    char c;
    while (recv(s, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') l += c;
    }
    return l;
}

// "$n\r\n<payload>\r\n" ya "-ERR ...\r\n" -> payload ya "ERR: ..."
static std::string cmd(sock_t s, const std::string& q) {
    std::string out = q + "\n";
    send(s, out.data(), (int)out.size(), 0);
    std::string h = read_line(s);
    if (h.empty()) return "<closed>";
    if (h[0] == '-') return "ERR: " + h.substr(1);
    size_t n = (size_t)std::atoi(h.c_str() + 1);
    std::string payload;
    while (payload.size() < n) {
        char b[4096];
        int r = recv(s, b, (int)std::min<size_t>(sizeof(b), n - payload.size()), 0);
        if (r <= 0) break;
        payload.append(b, (size_t)r);
    }
    read_line(s);  // trailing \r\n
    return payload;
}

int main() {
    std::remove("t_srv.db");
    std::remove("t_srv.db-journal");
    {
        Database db("t_srv.db");
        Server srv(db);
        CHECK(srv.start("127.0.0.1", 0));
        srv.start_background();
        int port = srv.port();
        CHECK(port > 0);

        sock_t a = connect_to(port);
        CHECK(a != (sock_t)-1);
        CHECK(has(cmd(a, "CREATE TABLE t (id INT PRIMARY KEY, v TEXT)"), "table ban gayi"));
        CHECK(has(cmd(a, "INSERT INTO t VALUES (1, 'one')"), "1 row"));
        std::string sel = cmd(a, "SELECT * FROM t");
        CHECK(has(sel, "1 | one") && has(sel, "(1 rows)"));
        CHECK(has(cmd(a, "this is not sql"), "ERR:"));          // galti pe -ERR, connection zinda
        CHECK(has(cmd(a, "INSERT INTO t VALUES (1, 'dup')"), "ERR:"));
        CHECK(has(cmd(a, "SELECT * FROM t"), "(1 rows)"));

        // bada reply (overflow wali row) poora aata hai
        std::string big(30000, 'z');
        CHECK(has(cmd(a, "INSERT INTO t VALUES (2, '" + big + "')"), "1 row"));
        CHECK(has(cmd(a, "SELECT v FROM t WHERE id = 2"), big));

        // doosra client: dono ek hi database dekhte hain
        sock_t b = connect_to(port);
        CHECK(has(cmd(b, "SELECT * FROM t"), "(2 rows)"));

        // client BEGIN karke bina commit ke gaya => auto ROLLBACK
        sock_t c = connect_to(port);
        CHECK(has(cmd(c, "BEGIN"), "transaction"));
        CHECK(has(cmd(c, "INSERT INTO t VALUES (3, 'ghost')"), "1 row"));
        CLOSESOCK(c);
        sleep_ms(300);
        CHECK(has(cmd(b, "SELECT * FROM t"), "(2 rows)"));

        // COMMIT wali txn dikhti hai
        CHECK(has(cmd(b, "BEGIN"), "transaction"));
        CHECK(has(cmd(b, "INSERT INTO t VALUES (4, 'kept')"), "1 row"));
        CHECK(has(cmd(b, "COMMIT"), "commit"));
        CHECK(has(cmd(a, "SELECT * FROM t"), "(3 rows)"));

        send(a, "QUIT\n", 5, 0);
        CLOSESOCK(a);
        CLOSESOCK(b);
        sleep_ms(100);
        srv.stop();
    }
    std::remove("t_srv.db");
    std::remove("t_srv.db-journal");
    std::cout << "SERVER OK\n";
    return 0;
}
