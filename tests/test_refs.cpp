#include "titanrefs/refs.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstring>

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            std::exit(1); \
        } \
    } while (0)

#define ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "[FAIL] " << msg << " (" << #a << " != " << #b << ") at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::exit(1); \
        } \
    } while (0)

void Test_CRC32C_Castagnoli() {
    std::cout << "[*] Running Test_CRC32C_Castagnoli...\n";
    // 32 bytes of zeros
    std::vector<uint8_t> zeros(32, 0);
    uint32_t c1 = titanrefs::Crc32c::Compute(zeros);
    ASSERT_TRUE(c1 != 0, "CRC32C of 32 zeros must not be zero");

    // Standard RFC 3720 check string: "123456789"
    const char* str = "123456789";
    uint32_t c2 = titanrefs::Crc32c::Compute(str, std::strlen(str));
    // Known Castagnoli CRC32C of "123456789" is 0xE3069283
    ASSERT_EQ(c2, 0xE3069283u, "Castagnoli CRC32C test vector mismatch");
    std::cout << "    [PASS] Castagnoli CRC32C verified with RFC 3720 test vector (0xE3069283)\n";
}

void Test_ReFS_Format_And_Mount() {
    std::cout << "[*] Running Test_ReFS_Format_And_Mount...\n";
    auto dev = std::make_shared<titanrefs::MemoryBlockDevice>("RamDisk", 16384, 4096); // 64 MB
    titanrefs::RefsFileSystem fs;

    auto st = fs.format(*dev, 65536, L"TEST_REFS");
    ASSERT_TRUE(titanrefs::IsSuccess(st), "Formatting must succeed");

    titanrefs::REFS_VOLUME_DATA_BUFFER info{};
    st = fs.getVolumeInformation(info);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "Querying volume info must succeed");
    ASSERT_EQ(info.bytesPerSector, 4096u, "Sector size must be 4096");
    ASSERT_EQ(info.bytesPerCluster, 65536u, "Cluster size must be 65536");
    ASSERT_EQ(info.majorVersion, 3, "Major version must be 3");

    fs.unmount();
    ASSERT_TRUE(!fs.isMounted(), "Volume must be unmounted");

    // Re-mount
    st = fs.mount(dev);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "Re-mounting formatted volume must succeed");
    std::cout << "    [PASS] ReFS volume formatted and remounted successfully\n";
}

void Test_ReFS_File_IO_And_IntegrityStreams() {
    std::cout << "[*] Running Test_ReFS_File_IO_And_IntegrityStreams...\n";
    auto dev = std::make_shared<titanrefs::MemoryBlockDevice>("RamDisk", 16384, 4096);
    titanrefs::RefsFileSystem fs;
    fs.format(*dev, 65536, L"INTEGRITY_TEST");

    // Create file with integrity stream
    auto st = fs.createFile(L"\\test_data.bin", titanrefs::FILE_ATTR_ARCHIVE | titanrefs::FILE_ATTR_INTEGRITY_STREAM, 0);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "createFile must succeed");

    std::vector<uint8_t> payload = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
    size_t written = 0;
    st = fs.writeFile(L"\\test_data.bin", 0, payload, written);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "writeFile must succeed");
    ASSERT_EQ(written, payload.size(), "Written size must match payload size");

    // Read back
    std::vector<uint8_t> readBuf(payload.size());
    size_t bytesRead = 0;
    st = fs.readFile(L"\\test_data.bin", 0, readBuf, bytesRead);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "readFile must succeed");
    ASSERT_EQ(bytesRead, payload.size(), "Read size must match payload size");
    ASSERT_TRUE(std::memcmp(readBuf.data(), payload.data(), payload.size()) == 0, "Data read must match data written");

    // Inject bit-rot corruption
    st = fs.corruptFileData(L"\\test_data.bin", 2);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "corruptFileData must succeed");

    // Read back must now fail with DataChecksumError
    st = fs.readFile(L"\\test_data.bin", 0, readBuf, bytesRead);
    ASSERT_EQ(st, titanrefs::Status::DataChecksumError, "readFile must detect bit-rot and return STATUS_DATA_CHECKSUM_ERROR");

    // Scrubber must identify the corrupted file
    std::vector<std::wstring> corrupted;
    fs.scrubIntegrityStreams(corrupted);
    ASSERT_EQ(corrupted.size(), 1u, "Scrubber must report exactly 1 corrupted file");
    ASSERT_TRUE(corrupted[0] == L"\\test_data.bin", "Corrupted filename must match");
    std::cout << "    [PASS] Integrity streams and bit-rot detection verified\n";
}

void Test_ReFS_Block_Cloning() {
    std::cout << "[*] Running Test_ReFS_Block_Cloning...\n";
    auto dev = std::make_shared<titanrefs::MemoryBlockDevice>("RamDisk", 16384, 4096);
    titanrefs::RefsFileSystem fs;
    fs.format(*dev, 65536, L"CLONE_TEST");

    // Create source file
    std::vector<uint8_t> sourceData(4096, 0xAA);
    size_t written = 0;
    fs.writeFile(L"\\source_vm.vhdx", 0, sourceData, written);

    // Create target file
    fs.createFile(L"\\clone_vm.vhdx", titanrefs::FILE_ATTR_ARCHIVE, 0);

    // Duplicate extents (FSCTL_DUPLICATE_EXTENTS_TO_FILE)
    auto st = fs.duplicateExtents(L"\\source_vm.vhdx", 0, L"\\clone_vm.vhdx", 0, 4096);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "duplicateExtents must succeed");

    // Verify clone content
    std::vector<uint8_t> cloneData(4096, 0);
    size_t bytesRead = 0;
    fs.readFile(L"\\clone_vm.vhdx", 0, cloneData, bytesRead);
    ASSERT_EQ(bytesRead, 4096u, "Cloned file must read 4096 bytes");
    ASSERT_TRUE(std::memcmp(cloneData.data(), sourceData.data(), 4096) == 0, "Cloned data must match source data exactly");
    std::cout << "    [PASS] Block Cloning (FSCTL_DUPLICATE_EXTENTS_TO_FILE) verified\n";
}

void Test_ReFS_Directory_Lifecycle() {
    std::cout << "[*] Running Test_ReFS_Directory_Lifecycle...\n";
    auto dev = std::make_shared<titanrefs::MemoryBlockDevice>("RamDisk", 16384, 4096);
    titanrefs::RefsFileSystem fs;
    fs.format(*dev, 65536, L"DIR_TEST");

    // Create nested dirs
    auto st = fs.createDirectory(L"\\Storage");
    ASSERT_TRUE(titanrefs::IsSuccess(st), "createDirectory must succeed");

    st = fs.createFile(L"\\Storage\\doc.txt", titanrefs::FILE_ATTR_ARCHIVE, 0);
    ASSERT_TRUE(titanrefs::IsSuccess(st), "createFile inside directory must succeed");

    // Attempting to remove non-empty directory must fail with DirectoryNotEmpty
    st = fs.removeDirectory(L"\\Storage");
    ASSERT_EQ(st, titanrefs::Status::DirectoryNotEmpty, "removeDirectory on non-empty directory must fail");

    // Recursive removal must succeed
    st = fs.removeDirectoryRecursive(L"\\Storage");
    ASSERT_TRUE(titanrefs::IsSuccess(st), "removeDirectoryRecursive must succeed");

    // File inside removed directory must no longer exist
    size_t readBytes = 0;
    std::vector<uint8_t> buf(16);
    st = fs.readFile(L"\\Storage\\doc.txt", 0, buf, readBytes);
    ASSERT_EQ(st, titanrefs::Status::NoSuchFile, "File inside deleted directory tree must be gone");
    std::cout << "    [PASS] Directory lifecycle and recursive removal verified\n";
}

int main() {
    std::cout << "========================================================\n"
              << "TitanReFS Clean-Room Verification Suite (v3.12)\n"
              << "========================================================\n";

    Test_CRC32C_Castagnoli();
    Test_ReFS_Format_And_Mount();
    Test_ReFS_File_IO_And_IntegrityStreams();
    Test_ReFS_Block_Cloning();
    Test_ReFS_Directory_Lifecycle();

    std::cout << "========================================================\n"
              << "[+] All TitanReFS unit tests passed successfully (100% Green)\n"
              << "========================================================\n";
    return 0;
}
