FROM gcc:13 AS build
WORKDIR /app
COPY src ./src
RUN g++ -std=c++17 -O2 -Isrc -o mkdb_server src/server.cpp src/server_core.cpp \
    src/lexer.cpp src/parser.cpp src/schema.cpp src/slotted_page.cpp src/btree_node.cpp \
    src/btree.cpp src/pager.cpp src/index.cpp src/executor.cpp src/database.cpp \
    src/database_dml.cpp -pthread \
    -static-libstdc++ -static-libgcc

FROM python:3.12-slim
WORKDIR /app
COPY --from=build /app/mkdb_server .
COPY playground ./playground
CMD ["sh", "-c", "./mkdb_server /tmp/demo.db 7878 127.0.0.1 & sleep 1 && python playground/bridge.py 7878"]