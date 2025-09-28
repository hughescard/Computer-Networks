# Link-Chat (Data Link Layer MVP)

> Minimal, educational project to implement a custom Layer‑2 chat/file transfer over Ethernet frames.

## Overview

* **Goal:** send/receive simple messages and chunked file transfers directly over Ethernet (L2), with a tiny custom header.
* **Scope (MVP):** CLI stubs today; raw sockets, framing, and reassembly next.
* **Platform:** Linux/WSL (Ubuntu recommended).

## Repository structure

```
linkchat/
├─ src/
│  ├─ main.cpp
│  ├─ cli.hpp / cli.cpp
│  ├─ frame.hpp          # protocol header (minimal) — user‑implemented
│  ├─ eth.hpp / eth.cpp  # net interface helpers (raw in D2)
│  └─ util/
│     ├─ mac.hpp / mac.cpp
│     └─ crc32.hpp / crc32.cpp
├─ CMakeLists.txt        # optional
├─ README.md
└─ .gitignore
```

## Build

### Option A — Direct (g++)

```bash
g++ -std=c++20 -O2 -Wall -Wextra $(find src -name '*.cpp' | sort) -o linkchat
```

### Option B — CMake

```bash
cmake -S . -B build
cmake --build build -j
```

## Quick test

```bash
./linkchat help && echo OK || echo FAIL
```

Expected: command list (stub) and `OK` printed by the shell.

## CLI (MVP stubs)

```
./linkchat help
./linkchat recv
./linkchat send <MAC> <text>
./linkchat sendfile <MAC> <path>
./linkchat scan
./linkchat broadcast <msg>
```
