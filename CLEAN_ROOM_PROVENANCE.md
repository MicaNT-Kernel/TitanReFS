# TitanReFS Clean-Room Provenance & Compliance Declaration

## 1. Executive Summary & Purpose
**TitanReFS** is an independent, sovereign, clean-room implementation of the Resilient File System (ReFS v3.x on-disk architecture). The purpose of this project is to achieve **functional interoperability** with storage volumes and disk images formatted according to standard ReFS layouts, enabling open read/write access, volume formatting, and integrity stream verification across diverse platforms.

---

## 2. Legal Framework & Fair Use Interoperability
This implementation is developed in accordance with established United States and international copyright laws and fair use precedents governing computer program interoperability:
- **17 U.S.C. § 1201(f) (Reverse Engineering & Interoperability Exemption)**: Expressly permits reverse engineering and functional analysis for the sole purpose of enabling interoperability of independently created software.
- **Google LLC v. Oracle America, Inc. (141 S. Ct. 1183, 2021)**: Confirmed that functional declarations, data formats, and API structures essential for software compatibility and interoperability fall squarely within fair use.
- **Sega Enterprises Ltd. v. Accolade, Inc. (977 F.2d 1510, 9th Cir. 1992)** & **Sony Computer Entertainment, Inc. v. Connectix Corp. (203 F.3d 596, 9th Cir. 2000)**: Established the clean-room "Chinese Wall" reverse-engineering standard as a legally valid mechanism to create compatible implementations.

---

## 3. Strict Clean-Room Rules & Zero-Exposure Guarantee
1. **Zero Proprietary Code Contamination**:
   - Neither the developers nor the codebase have consulted, copied, or incorporated any proprietary, leaked, or confidential Microsoft source code (including but not limited to the Windows Research Kernel, Windows NT 4.0/2000 leaks, or private Microsoft symbols).
2. **Original ISO C++23 Expression**:
   - All source code, B+ tree algorithms, checksum tables, and filesystem state machines are 100% original creative expressions implemented in freestanding modern C++23.
3. **Public Technical References**:
   - Structural alignments, magic numbers (`0x0000000053466552ULL`), and field offsets are derived solely from publicly available technical papers, published reverse-engineering research (e.g. `refsprogs`, `libfsrefs`), public hex dumps of formatted test images, and Microsoft's public documentation on block cloning (`FSCTL_DUPLICATE_EXTENTS_TO_FILE`).
4. **Automated Clean-Room Sentinel CI Enforcement**:
   - Every commit and pull request is scrutinized by an automated Clean-Room Sentinel (`scripts/clean_room_sentinel.js`) to block any proprietary identifiers, leaked symbols, or non-compliant constructs from ever entering the repository.
