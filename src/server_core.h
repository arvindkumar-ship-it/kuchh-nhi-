#pragma once
#include <string>
#include "database.h"

namespace mkdb {

// TCP server. Protocol (Redis jaisa, text):
//   client: ek line me ek SQL command ('\n' se khatam). QUIT se band.
//   server: kaamyabi -> "$<len>\r\n<payload>\r\n"   galti -> "-ERR <msg>\r\n"
// Ek Database, mutex se serialize. BEGIN karne wala client COMMIT/ROLLBACK/disconnect
// tak lock pakde rehta hai (disconnect pe auto ROLLBACK).
class Server {
public:
    explicit Server(Database& db);
    ~Server();

    // port 0 = OS koi khali port de deta hai. false agar bind/listen fail
    bool start(const std::string& host, int port);
    int port() const { return port_; }

    void serve_forever();       // blocking: accept loop jab tak stop() na ho
    void start_background();    // alag thread me serve_forever
    void stop();

private:
    struct Impl;
    Impl* impl_;
    int port_ = 0;
};

}  // namespace mkdb
