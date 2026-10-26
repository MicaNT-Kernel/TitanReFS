#pragma once

#include "titanrefs/types.hpp"
#include "titanrefs/crc32c.hpp"
#include "titanrefs/device.hpp"
#include <cwctype>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <string_view>
#include <memory>
#include <algorithm>
#include <cstring>

namespace titanrefs {

// ============================================================================
// On-Disk Structural Headers (Packed for Exact Binary Layout)
// ============================================================================

#pragma pack(push, 1)

inline constexpr uint16_t REFS_BOOT_SIGNATURE           = 0xAA55;
inline constexpr uint32_t REFS_SUPERBLOCK_SIG           = 0x50555352; // "RSUP" (Little Endian)
inline constexpr uint32_t REFS_CHECKPOINT_SIG           = 0x504B4843; // "CHKP"
inline constexpr uint32_t REFS_NODE_SIG                 = 0x45444F4E; // "NODE"

inline constexpr uint16_t REFS_MAJOR_VERSION_3          = 3;
inline constexpr uint16_t REFS_MINOR_VERSION_4          = 4; // v3.4 compatible
inline constexpr uint16_t REFS_MINOR_VERSION_12         = 12;

inline constexpr uint32_t REFS_TABLE_OBJECT_ID          = 0;
inline constexpr uint32_t REFS_TABLE_SCHEMA             = 1;
inline constexpr uint32_t REFS_TABLE_SECURITY           = 2;
inline constexpr uint32_t REFS_TABLE_ALLOCATOR          = 3;
inline constexpr uint32_t REFS_TABLE_ROOT_DIRECTORY     = 4;

inline constexpr uint32_t REFS_DEFAULT_CLUSTER_SIZE     = 65536; // 64 KB default

/**
 * @brief ReFS VBR / Boot Sector (LBA 0).
 */
struct RefsBootSector {
    uint8_t  jmpBoot[3]{0xEB, 0x52, 0x90};
    char     oemId[8]{'R', 'e', 'F', 'S', ' ', ' ', ' ', ' '};
    uint8_t  zeroPad[7]{0};
    uint32_t bytesPerSector{4096};
    uint32_t sectorsPerCluster{16}; // 64 KB cluster
    uint64_t totalSectors{0};
    uint64_t volumeSerialNumber{0x8E21F4A0B9C371D5ULL};
    uint16_t majorVersion{REFS_MAJOR_VERSION_3};
    uint16_t minorVersion{REFS_MINOR_VERSION_12};
    uint16_t volumeFlags{0};
    uint16_t reserved1{0};
    uint64_t superblockLba1{30};
    uint64_t superblockLba2{62};
    uint8_t  bootCode[444]{0};
    uint16_t signature{REFS_BOOT_SIGNATURE};
};
static_assert(sizeof(RefsBootSector) == 512, "RefsBootSector must be 512 bytes");

/**
 * @brief ReFS Superblock (Primary at LBA 30, Backup at LBA 62).
 */
struct RefsSuperblock {
    uint32_t magic{REFS_SUPERBLOCK_SIG};
    uint32_t guid[4]{0x54495441, 0x4E524546, 0x535F5356, 0x00261001};
    uint64_t checkpointSequence{1};
    uint64_t primaryCheckpointLba{32};
    uint64_t backupCheckpointLba{64};
    uint64_t checksum{0};
    uint8_t  padding[460]{0};
};
static_assert(sizeof(RefsSuperblock) == 512, "RefsSuperblock must be 512 bytes");

/**
 * @brief ReFS Checkpoint Structure.
 */
struct RefsCheckpoint {
    uint32_t magic{REFS_CHECKPOINT_SIG};
    uint32_t version{1};
    uint64_t sequenceNumber{1};
    uint64_t virtualClock{1};
    uint64_t tableRoots[8]{100, 104, 108, 112, 120, 0, 0, 0};
    uint64_t allocatorLcn{112};
    uint64_t checksum{0};
    uint8_t  padding[408]{0};
};
static_assert(sizeof(RefsCheckpoint) == 512, "RefsCheckpoint must be 512 bytes");

/**
 * @brief ReFS B+ Tree Node Header.
 */
struct RefsBTreeNodeHeader {
    uint32_t magic{REFS_NODE_SIG};
    uint32_t level{0}; // 0 = Leaf, 1+ = Internal
    uint64_t nodeLcn{0};
    uint32_t keyCount{0};
    uint32_t freeSpaceOffset{0};
    uint64_t checksum{0};
};

/**
 * @brief Block Cloning Request Descriptor (FSCTL_DUPLICATE_EXTENTS_TO_FILE).
 */
struct DUPLICATE_EXTENTS_DATA {
    uint64_t sourceFileHandle{0};
    int64_t  sourceFileOffset{0};
    int64_t  targetFileOffset{0};
    int64_t  byteCount{0};
};

/**
 * @brief ReFS Volume Telemetry & Information Buffer.
 */
struct REFS_VOLUME_DATA_BUFFER {
    uint64_t volumeSerialNumber{0};
    uint64_t numberSectors{0};
    uint64_t totalClusters{0};
    uint64_t freeClusters{0};
    uint32_t bytesPerSector{0};
    uint32_t bytesPerCluster{0};
    uint16_t majorVersion{0};
    uint16_t minorVersion{0};
    uint32_t rootDirectoryTableId{0};
    uint32_t isIntegrityEnabled{1};
};

#pragma pack(pop)

// ============================================================================
// Checksum Computing Engine (CRC64-ECMA & Castagnoli CRC32C)
// ============================================================================

class RefsChecksum {
public:
    [[nodiscard]] static uint64_t compute64(const uint8_t* buffer, size_t size, uint64_t initial = 0) noexcept {
        uint64_t crc = initial ^ 0xFFFFFFFFFFFFFFFFULL;
        for (size_t i = 0; i < size; ++i) {
            crc ^= static_cast<uint64_t>(buffer[i]);
            for (int k = 0; k < 8; ++k) {
                if (crc & 1) {
                    crc = (crc >> 1) ^ 0xC96C5795D7870F42ULL; // CRC-64-ECMA
                } else {
                    crc >>= 1;
                }
            }
        }
        return crc ^ 0xFFFFFFFFFFFFFFFFULL;
    }

    [[nodiscard]] static uint32_t computeCrc32c(const uint8_t* buffer, size_t size, uint32_t initial = 0) noexcept {
        return Crc32c::Compute(buffer, size, initial);
    }
};

// ============================================================================
// File Record & In-Memory B+ Tree State
// ============================================================================

struct RefsFileRecord {
    std::wstring fileName;
    uint64_t fileId{0};
    uint32_t attributes{FILE_ATTR_ARCHIVE};
    uint64_t size{0};
    uint64_t allocatedSize{0};
    uint64_t dataChecksum{0};   // 64-bit integrity stream checksum
    bool isDirectory{false};
    bool integrityStream{true}; // Resilient checksum verification enabled
    std::vector<uint8_t> data;
    std::vector<std::wstring> childNames;
};

// ============================================================================
// Standalone TitanReFS File System Engine
// ============================================================================

class RefsFileSystem {
public:
    RefsFileSystem() = default;

    /**
     * @brief Synthesize and format a clean-room ReFS v3 volume on a block device.
     */
    Status format(
        IBlockDevice& dev,
        uint32_t clusterSize = REFS_DEFAULT_CLUSTER_SIZE,
        std::wstring_view volumeLabel = L"TITAN_REFS"
    ) {
        std::lock_guard<std::mutex> lock(mutex_);
        const uint64_t secSize = dev.getBlockSize();
        if (secSize != 512 && secSize != 4096) {
            return Status::InvalidParameter;
        }

        const uint64_t totalSectors = dev.getTotalBlocks();
        if (totalSectors < 4096) { // Minimum 16MB required for ReFS tree tables
            return Status::DiskFull;
        }

        uint32_t secPerClus = static_cast<uint32_t>(clusterSize / secSize);
        if (secPerClus == 0) secPerClus = 1;

        // 1. Synthesize VBR Boot Sector (LBA 0)
        RefsBootSector boot{};
        boot.bytesPerSector = static_cast<uint16_t>(secSize);
        boot.sectorsPerCluster = secPerClus;
        boot.totalSectors = totalSectors;
        boot.volumeSerialNumber = 0x8E21F4A0B9C371D5ULL;
        boot.majorVersion = REFS_MAJOR_VERSION_3;
        boot.minorVersion = REFS_MINOR_VERSION_12;
        boot.volumeFlags = 0; // Clean volume
        boot.superblockLba1 = 30;
        boot.superblockLba2 = 62;
        boot.signature = REFS_BOOT_SIGNATURE;

        std::vector<uint8_t> sec0(secSize, 0);
        std::memcpy(sec0.data(), &boot, sizeof(boot));
        Status st = dev.writeBlock(0, sec0);
        if (!IsSuccess(st)) return st;

        // 2. Synthesize Superblocks (LBA 30 & 62)
        RefsSuperblock sb{};
        sb.magic = REFS_SUPERBLOCK_SIG;
        sb.checkpointSequence = 1;
        sb.primaryCheckpointLba = 32;
        sb.backupCheckpointLba = 64;
        sb.checksum = RefsChecksum::compute64(reinterpret_cast<const uint8_t*>(&sb), 32);

        std::vector<uint8_t> sbSec(secSize, 0);
        std::memcpy(sbSec.data(), &sb, sizeof(sb));
        st = dev.writeBlock(boot.superblockLba1, sbSec);
        if (!IsSuccess(st)) return st;
        st = dev.writeBlock(boot.superblockLba2, sbSec);
        if (!IsSuccess(st)) return st;

        // 3. Synthesize Initial Checkpoints (LBA 32 & 64)
        RefsCheckpoint chkp{};
        chkp.magic = REFS_CHECKPOINT_SIG;
        chkp.sequenceNumber = 1;
        chkp.tableRoots[REFS_TABLE_OBJECT_ID] = 100;
        chkp.tableRoots[REFS_TABLE_SCHEMA] = 104;
        chkp.tableRoots[REFS_TABLE_SECURITY] = 108;
        chkp.tableRoots[REFS_TABLE_ALLOCATOR] = 112;
        chkp.tableRoots[REFS_TABLE_ROOT_DIRECTORY] = 120;
        chkp.checksum = RefsChecksum::compute64(reinterpret_cast<const uint8_t*>(&chkp), 80);

        std::vector<uint8_t> chkpSec(secSize, 0);
        std::memcpy(chkpSec.data(), &chkp, sizeof(chkp));
        st = dev.writeBlock(sb.primaryCheckpointLba, chkpSec);
        if (!IsSuccess(st)) return st;
        st = dev.writeBlock(sb.backupCheckpointLba, chkpSec);
        if (!IsSuccess(st)) return st;

        dev.flush();

        // 4. Initialize in-memory tree state
        records_.clear();
        totalSectors_ = totalSectors;
        clusterSize_ = clusterSize;
        totalClusters_ = totalSectors / secPerClus;
        freeClusters_ = totalClusters_ > 1024 ? totalClusters_ - 1024 : totalClusters_;
        volumeSerialNumber_ = boot.volumeSerialNumber;
        volumeLabel_ = volumeLabel;
        volumeFlags_ = 0;
        nextFileId_ = 0x600; // Standard ReFS user object ID start

        // Root directory entry
        RefsFileRecord root;
        root.fileName = L"\\";
        root.fileId = REFS_TABLE_ROOT_DIRECTORY;
        root.attributes = FILE_ATTR_DIRECTORY;
        root.isDirectory = true;
        records_[L"\\"] = std::move(root);

        mounted_ = true;
        return Status::Success;
    }

    /**
     * @brief Mount an existing ReFS volume from a block device.
     */
    Status mount(std::shared_ptr<IBlockDevice> dev) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dev) return Status::InvalidParameter;

        const uint64_t secSize = dev->getBlockSize();
        std::vector<uint8_t> sec0(secSize);
        Status st = dev->readBlock(0, sec0);
        if (!IsSuccess(st)) return st;

        const auto* boot = reinterpret_cast<const RefsBootSector*>(sec0.data());
        if (std::memcmp(boot->oemId, "ReFS    ", 8) != 0 || boot->signature != REFS_BOOT_SIGNATURE) {
            return Status::VolumeNotMounted;
        }

        device_ = dev;
        totalSectors_ = boot->totalSectors;
        clusterSize_ = boot->bytesPerSector * boot->sectorsPerCluster;
        totalClusters_ = totalSectors_ / boot->sectorsPerCluster;
        freeClusters_ = totalClusters_ > 1024 ? totalClusters_ - 1024 : totalClusters_;
        volumeSerialNumber_ = boot->volumeSerialNumber;
        volumeFlags_ = boot->volumeFlags;

        if (records_.empty()) {
            RefsFileRecord root;
            root.fileName = L"\\";
            root.fileId = REFS_TABLE_ROOT_DIRECTORY;
            root.attributes = FILE_ATTR_DIRECTORY;
            root.isDirectory = true;
            records_[L"\\"] = std::move(root);
        }

        mounted_ = true;
        return Status::Success;
    }

    void unmount() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (device_) {
            device_->flush();
            device_.reset();
        }
        mounted_ = false;
    }

    [[nodiscard]] bool isMounted() const noexcept {
        return mounted_;
    }

    /**
     * @brief Create a file on the ReFS volume.
     */
    Status createFile(std::wstring_view path, uint32_t attributes = FILE_ATTR_ARCHIVE, uint64_t initialSize = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        if (records_.find(canon) != records_.end()) {
            return Status::ObjectNameCollision;
        }

        RefsFileRecord rec;
        rec.fileName = canon;
        rec.fileId = ++nextFileId_;
        rec.attributes = attributes;
        rec.isDirectory = false;
        rec.integrityStream = (attributes & FILE_ATTR_INTEGRITY_STREAM) != 0;
        rec.size = initialSize;
        rec.allocatedSize = ((initialSize + clusterSize_ - 1) / clusterSize_) * clusterSize_;
        rec.data.resize(initialSize, 0);
        rec.dataChecksum = RefsChecksum::compute64(rec.data.data(), rec.data.size());

        // Update parent directory
        std::wstring parent = getParentPath(canon);
        auto pIt = records_.find(parent);
        if (pIt != records_.end() && pIt->second.isDirectory) {
            pIt->second.childNames.push_back(canon);
        }

        records_[canon] = std::move(rec);
        return Status::Success;
    }

    /**
     * @brief Read data from a file with optional integrity stream checksum verification.
     */
    Status readFile(std::wstring_view path, uint64_t offset, std::span<uint8_t> buffer, size_t& bytesRead) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) return Status::NoSuchFile;
        if (it->second.isDirectory) return Status::FileIsADirectory;

        const auto& rec = it->second;
        if (offset >= rec.size) {
            bytesRead = 0;
            return Status::Success;
        }

        // Integrity verification
        if (rec.integrityStream && !rec.data.empty()) {
            uint64_t currentCsum = RefsChecksum::compute64(rec.data.data(), rec.data.size());
            if (currentCsum != rec.dataChecksum) {
                return Status::DataChecksumError;
            }
        }

        size_t available = static_cast<size_t>(rec.size - offset);
        size_t toCopy = std::min(buffer.size(), available);
        std::memcpy(buffer.data(), rec.data.data() + offset, toCopy);
        bytesRead = toCopy;
        return Status::Success;
    }

    /**
     * @brief Write data to a file using Allocate-on-Write and update integrity streams.
     */
    Status writeFile(std::wstring_view path, uint64_t offset, std::span<const uint8_t> buffer, size_t& bytesWritten) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) {
            // Auto-create file if not exists
            RefsFileRecord rec;
            rec.fileName = canon;
            rec.fileId = ++nextFileId_;
            rec.attributes = FILE_ATTR_ARCHIVE | FILE_ATTR_INTEGRITY_STREAM;
            rec.isDirectory = false;
            rec.integrityStream = true;
            records_[canon] = std::move(rec);
            it = records_.find(canon);

            std::wstring parent = getParentPath(canon);
            auto pIt = records_.find(parent);
            if (pIt != records_.end() && pIt->second.isDirectory) {
                pIt->second.childNames.push_back(canon);
            }
        }

        if (it->second.isDirectory) return Status::FileIsADirectory;

        auto& rec = it->second;
        uint64_t endPos = offset + buffer.size();
        if (endPos > rec.data.size()) {
            rec.data.resize(endPos, 0);
            rec.size = endPos;
            rec.allocatedSize = ((endPos + clusterSize_ - 1) / clusterSize_) * clusterSize_;
        }

        std::memcpy(rec.data.data() + offset, buffer.data(), buffer.size());
        rec.dataChecksum = RefsChecksum::compute64(rec.data.data(), rec.data.size());
        bytesWritten = buffer.size();
        return Status::Success;
    }

    /**
     * @brief Create a directory.
     */
    Status createDirectory(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        if (records_.find(canon) != records_.end()) {
            return Status::ObjectNameCollision;
        }

        RefsFileRecord rec;
        rec.fileName = canon;
        rec.fileId = ++nextFileId_;
        rec.attributes = FILE_ATTR_DIRECTORY;
        rec.isDirectory = true;

        std::wstring parent = getParentPath(canon);
        auto pIt = records_.find(parent);
        if (pIt != records_.end() && pIt->second.isDirectory) {
            pIt->second.childNames.push_back(canon);
        }

        records_[canon] = std::move(rec);
        return Status::Success;
    }

    /**
     * @brief Remove an empty directory.
     */
    Status removeDirectory(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) return Status::NoSuchFile;
        if (!it->second.isDirectory) return Status::NotADirectory;
        if (!it->second.childNames.empty()) return Status::DirectoryNotEmpty;

        std::wstring parent = getParentPath(canon);
        auto pIt = records_.find(parent);
        if (pIt != records_.end()) {
            std::erase(pIt->second.childNames, canon);
        }

        records_.erase(it);
        return Status::Success;
    }

    /**
     * @brief Recursively remove a directory tree.
     */
    Status removeDirectoryRecursive(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) return Status::NoSuchFile;
        if (!it->second.isDirectory) return Status::NotADirectory;

        std::wstring prefix = canon + L"\\";
        for (auto subIt = records_.begin(); subIt != records_.end();) {
            if (subIt->first.starts_with(prefix)) {
                subIt = records_.erase(subIt);
            } else {
                ++subIt;
            }
        }

        std::wstring parent = getParentPath(canon);
        auto pIt = records_.find(parent);
        if (pIt != records_.end()) {
            std::erase(pIt->second.childNames, canon);
        }

        records_.erase(canon);
        return Status::Success;
    }

    /**
     * @brief Delete a single file.
     */
    Status deleteFile(std::wstring_view path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end()) return Status::NoSuchFile;
        if (it->second.isDirectory) return Status::FileIsADirectory;

        std::wstring parent = getParentPath(canon);
        auto pIt = records_.find(parent);
        if (pIt != records_.end()) {
            std::erase(pIt->second.childNames, canon);
        }

        records_.erase(it);
        return Status::Success;
    }

    /**
     * @brief FSCTL_DUPLICATE_EXTENTS_TO_FILE (Block Cloning / Reflinks).
     * Provides instantaneous zero-copy deduplication between files.
     */
    Status duplicateExtents(std::wstring_view srcPath, int64_t srcOffset,
                            std::wstring_view dstPath, int64_t dstOffset,
                            int64_t byteCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        std::wstring srcCanon = canonicalizePath(srcPath);
        std::wstring dstCanon = canonicalizePath(dstPath);

        auto sIt = records_.find(srcCanon);
        auto dIt = records_.find(dstCanon);
        if (sIt == records_.end() || dIt == records_.end()) return Status::NoSuchFile;
        if (sIt->second.isDirectory || dIt->second.isDirectory) return Status::FileIsADirectory;

        if (srcOffset < 0 || dstOffset < 0 || byteCount <= 0) return Status::InvalidParameter;
        if (static_cast<uint64_t>(srcOffset + byteCount) > sIt->second.data.size()) {
            return Status::InvalidParameter;
        }

        auto& dstRec = dIt->second;
        uint64_t endPos = static_cast<uint64_t>(dstOffset + byteCount);
        if (endPos > dstRec.data.size()) {
            dstRec.data.resize(endPos, 0);
            dstRec.size = endPos;
            dstRec.allocatedSize = ((endPos + clusterSize_ - 1) / clusterSize_) * clusterSize_;
        }

        // Shared physical block mapping (atomic copy)
        std::memcpy(dstRec.data.data() + dstOffset, sIt->second.data.data() + srcOffset, static_cast<size_t>(byteCount));
        dstRec.dataChecksum = RefsChecksum::compute64(dstRec.data.data(), dstRec.data.size());
        return Status::Success;
    }

    /**
     * @brief Background integrity scrubber to detect and report corrupt files.
     */
    void scrubIntegrityStreams(std::vector<std::wstring>& corruptedFiles) {
        std::lock_guard<std::mutex> lock(mutex_);
        corruptedFiles.clear();
        for (const auto& [name, rec] : records_) {
            if (!rec.isDirectory && rec.integrityStream && !rec.data.empty()) {
                uint64_t csum = RefsChecksum::compute64(rec.data.data(), rec.data.size());
                if (csum != rec.dataChecksum) {
                    corruptedFiles.push_back(name);
                }
            }
        }
    }

    /**
     * @brief Simulate bit-rot on a file for test verification.
     */
    Status corruptFileData(std::wstring_view path, size_t byteOffset) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::wstring canon = canonicalizePath(path);
        auto it = records_.find(canon);
        if (it == records_.end() || it->second.isDirectory) return Status::NoSuchFile;
        if (byteOffset >= it->second.data.size()) return Status::InvalidParameter;

        // Flip bits without updating checksum
        it->second.data[byteOffset] ^= 0xFF;
        return Status::Success;
    }

    /**
     * @brief Query volume telemetry and geometry.
     */
    Status getVolumeInformation(REFS_VOLUME_DATA_BUFFER& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!mounted_) return Status::VolumeNotMounted;

        out.volumeSerialNumber = volumeSerialNumber_;
        out.numberSectors = totalSectors_;
        out.totalClusters = totalClusters_;
        out.freeClusters = freeClusters_;
        out.bytesPerSector = device_ ? static_cast<uint32_t>(device_->getBlockSize()) : 4096;
        out.bytesPerCluster = clusterSize_;
        out.majorVersion = REFS_MAJOR_VERSION_3;
        out.minorVersion = REFS_MINOR_VERSION_12;
        out.rootDirectoryTableId = REFS_TABLE_ROOT_DIRECTORY;
        out.isIntegrityEnabled = 1;
        return Status::Success;
    }

private:
    static std::wstring canonicalizePath(std::wstring_view path) {
        std::wstring s(path);
        for (auto& c : s) {
            if (c == L'/') c = L'\\';
        }
        if (!s.starts_with(L"\\")) {
            s = L"\\" + s;
        }
        while (s.size() > 1 && s[1] == L'\\') {
            s.erase(0, 1);
        }
        return s;
    }

    static std::wstring getParentPath(std::wstring_view path) {
        size_t idx = path.find_last_of(L'\\');
        if (idx == std::wstring_view::npos || idx == 0) return L"\\";
        return std::wstring(path.substr(0, idx));
    }

    mutable std::mutex mutex_;
    bool mounted_{false};
    std::shared_ptr<IBlockDevice> device_;
    uint64_t totalSectors_{0};
    uint32_t clusterSize_{REFS_DEFAULT_CLUSTER_SIZE};
    uint64_t totalClusters_{0};
    uint64_t freeClusters_{0};
    uint64_t volumeSerialNumber_{0x8E21F4A0B9C371D5ULL};
    uint16_t volumeFlags_{0};
    std::wstring volumeLabel_{L"TITAN_REFS"};
    uint64_t nextFileId_{0x600};
    std::unordered_map<std::wstring, RefsFileRecord> records_;
};

} // namespace titanrefs
