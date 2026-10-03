#!/usr/bin/env python3
"""mkdb playground bridge.

Browser TCP nahi bol sakta, isliye ye chhota HTTP server beech me baithta hai:
browser -> HTTP -> bridge -> TCP -> mkdb_server

Chalane se pehle mkdb_server chalu hona chahiye. Sirf Python standard library.

    python playground/bridge.py [db_port] [web_port]
"""
import json
import os
import re
import socket
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DB_HOST = "127.0.0.1"
DB_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 7878
WEB_PORT = int(os.environ.get("PORT") or (sys.argv[2] if len(sys.argv) > 2 else 8000))
HERE = os.path.dirname(os.path.abspath(__file__))

lock = threading.Lock()  # ek hi db session hai, toh ek time pe ek query
sock = None
buf = b""


def close_sock():
    global sock, buf
    if sock is not None:
        try:
            sock.close()
        except OSError:
            pass
    sock = None
    buf = b""


def read_line():
    global buf
    while b"\r\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("server ne connection band kar diya")
        buf += chunk
    line, buf = buf.split(b"\r\n", 1)
    return line.decode("utf-8", "replace")


def read_n(n):
    global buf
    while len(buf) < n + 2:
        chunk = sock.recv(65536)
        if not chunk:
            raise ConnectionError("server ne connection band kar diya")
        buf += chunk
    data, buf = buf[:n], buf[n + 2:]
    return data.decode("utf-8", "replace")


def run_one(sql):
    """Ek statement bhejo. (ok, text) lautata hai."""
    global sock, buf
    # purana connection toota ho toh bhejte hi pata chalega, tab ek baar naya try
    for attempt in (0, 1):
        try:
            if sock is None:
                sock = socket.create_connection((DB_HOST, DB_PORT), timeout=10)
                buf = b""
            sock.sendall(sql.encode("utf-8") + b"\n")
            break
        except OSError:
            close_sock()
            if attempt == 1:
                return False, "mkdb_server se connect nahi hua (port %d). Server chalu hai?" % DB_PORT
    try:
        head = read_line()
        if head.startswith("$"):
            return True, read_n(int(head[1:]))
        if head.startswith("-ERR "):
            return False, head[5:]
        return False, "samajh nahi aaya: " + head
    except (OSError, ConnectionError) as e:
        close_sock()
        return False, "connection toot gaya (%s). Khuli transaction thi toh wo rollback ho gayi." % e


def split_sql(text):
    """Server line-based hai: ek line = ek statement. Yahan multi-line script ko
    ';' pe todte hain (quote ke andar ka ';' nahi), comments hata ke."""
    out, cur = [], []
    i, n, in_q = 0, len(text), False
    while i < n:
        c = text[i]
        if in_q:
            if c == "'":
                if i + 1 < n and text[i + 1] == "'":
                    cur.append("''")
                    i += 2
                    continue
                in_q = False
            cur.append(" " if c in "\r\n" else c)
        elif c == "'":
            in_q = True
            cur.append(c)
        elif c == "-" and text[i:i + 2] == "--":
            while i < n and text[i] != "\n":
                i += 1
            continue
        elif c == ";":
            out.append("".join(cur))
            cur = []
        elif c in " \t\r\n":
            cur.append(" ")
        else:
            cur.append(c)
        i += 1
    out.append("".join(cur))
    return [re.sub(r" +", " ", s).strip() for s in out if s.strip()]


def parse_table(text):
    lines = text.split("\n")
    if len(lines) >= 2 and re.fullmatch(r"\(\d+ rows?\)", lines[-1]):
        return {
            "cols": lines[0].split(" | "),
            "rows": [ln.split(" | ") for ln in lines[1:-1]],
            "count": int(re.findall(r"\d+", lines[-1])[0]),
        }
    return None


def run_script(text):
    results = []
    for sql in split_sql(text):
        t0 = time.perf_counter()
        ok, out = run_one(sql)
        item = {"sql": sql, "ok": ok, "text": out,
                "ms": round((time.perf_counter() - t0) * 1000, 2)}
        if ok:
            tbl = parse_table(out)
            if tbl:
                item["table"] = tbl
        results.append(item)
        if not ok:
            break  # pehli galti pe ruk jao
    return results


def schema():
    ok, out = run_one("SHOW TABLES")
    if not ok:
        return {"connected": False, "error": out, "tables": []}
    tbl = parse_table(out)
    tables = []
    for row in (tbl["rows"] if tbl else []):
        name = row[0]
        ok2, d = run_one("DESCRIBE " + name)
        dt = parse_table(d) if ok2 else None
        cols = []
        for r in (dt["rows"] if dt else []):
            cols.append({"name": r[0], "type": r[1], "key": r[2] if len(r) > 2 else ""})
        tables.append({"name": name, "columns": cols})
    return {"connected": True, "tables": tables}


class Handler(BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send_json(self, obj, code=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/schema":
            with lock:
                self.send_json(schema())
        elif self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                body = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            self.send_json({"error": "not found"}, 404)

    def do_POST(self):
        if self.path != "/api/run":
            self.send_json({"error": "not found"}, 404)
            return
        n = int(self.headers.get("Content-Length", 0))
        if n > 1 << 20:
            self.send_json({"error": "script bahut badi hai"}, 413)
            return
        try:
            sql = json.loads(self.rfile.read(n).decode("utf-8")).get("sql", "")
        except (ValueError, UnicodeDecodeError):
            self.send_json({"error": "galat request"}, 400)
            return
        with lock:
            self.send_json({"results": run_script(sql)})


if __name__ == "__main__":
    srv = ThreadingHTTPServer(("0.0.0.0", WEB_PORT), Handler)
    print("playground: http://0.0.0.0:%d   (mkdb_server port %d)" % (WEB_PORT, DB_PORT), flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass