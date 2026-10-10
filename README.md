# kuchh-nhi-

The C++17 engine implements a small SQL/parser/storage/index/TCP stack. It has 15 registered tests; Linux checks passed in the audit. It is an educational engine with a companion consensusdb cluster. Authentication, TLS, comprehensive crash durability and production database semantics are not established. Keep TCP access on a trusted local network and do not store sensitive data.

## Verification

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

[Audit fixes and limits](AUDIT_FIXES.md).
