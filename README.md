# TitanReFS: Sovereign Clean-Room ReFS Engine for Windows & Modern Platforms

[![TitanReFS CI](https://github.com/MicaNT-Kernel/TitanReFS/actions/workflows/ci.yml/badge.svg)](https://github.com/MicaNT-Kernel/TitanReFS/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-orange.svg)]()
[![Clean-Room Certified](https://img.shields.io/badge/Clean--Room-Sentinel%20Audited-brightgreen.svg)](CLEAN_ROOM_PROVENANCE.md)

**TitanReFS** is the world's first independent, clean-room, open-source **Resilient File System (ReFS v3.12)** implementation. Designed in modern ISO C++23 from first principles, TitanReFS unlocks ReFS volume formatting, read/write access, and proactive data scrubbing for all Windows users (including Windows 10/11 Home and Pro) without requiring Enterprise or Workstation SKU locks.

---

## 🌟 Key Features

- **Format Any Drive or Image to ReFS**: Unlock formatting on external SSDs, USB drives, and `.img`/`.vhdx` images on any Windows edition.
- **B+ Tree Architecture**: 64KB page nodes with exabyte-scale key-value table addressing.
- **Allocate-on-Write (CoW)**: Out-of-place atomic writes eliminating write holes and ensuring zero-downtime consistency.
- **Hardware-Accelerated CRC32C / CRC64 Integrity Streams**: Castagnoli CRC32C and CRC64 checksum verification protecting against silent bit-rot.
- **Instant Block Cloning (`FSCTL_DUPLICATE_EXTENTS_TO_FILE`)**: Zero-copy extent sharing with atomic reference counting for rapid virtual machine checkpointing.
- **Proactive Bit-Rot Scrubbing**: Automatic background verification of file integrity streams.
- **Strict Clean-Room Provenance**: 100% written from scratch with zero proprietary code exposure. Verified on every commit by our Clean-Room Sentinel.

---

## 🛠️ CLI Utilities

### 1. `titanrefs_format`
Format any disk image or physical drive to ReFS v3:
```bash
titanrefs_format my_disk.img --size 256 --cluster 65536 --label "Storage"
```

### 2. `titanrefs_check`
Mount, inspect on-disk structures, and scrub integrity streams:
```bash
titanrefs_check my_disk.img
```

---

## 🚀 Building & Running Tests

### Prerequisites
- CMake 3.24+
- Modern C++23 Compiler (MSVC 2022 v17.8+, Clang 18+, or GCC 14+)

### Build
```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```
Or with Ninja / Clang:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Run Test Battery
```bash
./build/titanrefs_tests
```

### Run Clean-Room Sentinel Audit
```bash
node scripts/clean_room_sentinel.js --local
```

---

## ⚖️ Legal & Clean-Room Architecture
TitanReFS is an independent clean-room implementation developed for functional interoperability under 17 U.S.C. § 1201(f) and SCOTUS fair use doctrine (*Google v. Oracle*). See [CLEAN_ROOM_PROVENANCE.md](CLEAN_ROOM_PROVENANCE.md) for full compliance declarations.

---

## 📄 License
Licensed under the Apache License, Version 2.0.
Copyright © 2026 Daniel Elliott <ssfdre38@gmail.com>.
