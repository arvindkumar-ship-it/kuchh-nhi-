# MiniDB build and verification instructions

Repair branch: `fix/internship-audit-20261010`. Changes are scoped to audit findings; no deployment or live provider action is included.

## Changed

Added repository-level documentation and Linux/Windows CMake/CTest CI for the existing engine. No speculative engine changes were made.

## Verification

`cmake -S . -B build; cmake --build build --config Release; ctest --test-dir build -C Release --output-on-failure`

Commands are entry points, not a claim that every live integration was executed. See the repair bundle for actual results.

## Setup and remaining evidence

The C++17 engine implements a small SQL/parser/storage/index/TCP stack. It has 15 registered tests; Linux checks passed in the audit. It is an educational engine with a companion consensusdb cluster. Authentication, TLS, comprehensive crash durability and production database semantics are not established. Keep TCP access on a trusted local network and do not store sensitive data.
