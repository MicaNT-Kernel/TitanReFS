#pragma once

#include <cstdint>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <array>
#include <algorithm>
#include <chrono>

namespace titanrefs {

// ============================================================================
// Status Codes (Freestanding & NT Compatible)
// ============================================================================

enum class Status : uint32_t {
    Success                  = 0x00000000,
    NoSuchDevice             = 0xC000000E,
    NoSuchFile               = 0xC000000F,
    AccessDenied             = 0xC0000022,
    BufferTooSmall           = 0xC0000023,
    ObjectNameNotFound       = 0xC0000034,
    ObjectNameCollision      = 0xC0000035,
    DataChecksumError        = 0xC00002EB,
    DeviceError              = 0xC000009C,
    DiskFull                 = 0xC000007F,
    VolumeNotMounted         = 0xC00000A1,
    MediaWriteProtected      = 0xC00000A2,
    InvalidParameter         = 0xC000000D,
    NotImplemented           = 0xC0000002,
    DirectoryNotEmpty        = 0xC0000101,
    FileIsADirectory         = 0xC00000BA,
    NotADirectory            = 0xC0000103,
    CorruptVolume            = 0xC0000102
};

[[nodiscard]] inline constexpr bool IsSuccess(Status status) noexcept {
    return status == Status::Success;
}

[[nodiscard]] inline const char* StatusToString(Status s) noexcept {
    switch (s) {
        case Status::Success:             return "STATUS_SUCCESS";
        case Status::NoSuchDevice:        return "STATUS_NO_SUCH_DEVICE";
        case Status::NoSuchFile:          return "STATUS_NO_SUCH_FILE";
        case Status::AccessDenied:        return "STATUS_ACCESS_DENIED";
        case Status::BufferTooSmall:      return "STATUS_BUFFER_TOO_SMALL";
        case Status::ObjectNameNotFound:  return "STATUS_OBJECT_NAME_NOT_FOUND";
        case Status::ObjectNameCollision: return "STATUS_OBJECT_NAME_COLLISION";
        case Status::DataChecksumError:   return "STATUS_DATA_CHECKSUM_ERROR";
        case Status::DeviceError:         return "STATUS_DEVICE_ERROR";
        case Status::DiskFull:            return "STATUS_DISK_FULL";
        case Status::VolumeNotMounted:    return "STATUS_VOLUME_NOT_MOUNTED";
        case Status::MediaWriteProtected: return "STATUS_MEDIA_WRITE_PROTECTED";
        case Status::InvalidParameter:    return "STATUS_INVALID_PARAMETER";
        case Status::DirectoryNotEmpty:   return "STATUS_DIRECTORY_NOT_EMPTY";
        case Status::FileIsADirectory:    return "STATUS_FILE_IS_A_DIRECTORY";
        case Status::NotADirectory:       return "STATUS_NOT_A_DIRECTORY";
        case Status::CorruptVolume:       return "STATUS_FILE_CORRUPT_ERROR";
        default:                          return "STATUS_UNSUCCESSFUL";
    }
}

// ============================================================================
// ReFS Constants & On-Disk Format Specifications
// ============================================================================

inline constexpr uint32_t REFS_SECTOR_SIZE                  = 4096;
inline constexpr uint32_t REFS_CLUSTER_SIZE                 = 65536; // 64 KB Cluster
inline constexpr uint16_t REFS_MAJOR_VERSION                = 3;
inline constexpr uint16_t REFS_MINOR_VERSION                = 12;    // ReFS v3.12 (Standard Windows 11 / Server 2025 format)
inline constexpr uint64_t REFS_MAGIC_SIGNATURE              = 0x0000000053466552ULL; // "ReFS"
inline constexpr uint32_t FSCTL_DUPLICATE_EXTENTS_TO_FILE   = 0x00098344;
inline constexpr uint32_t FSCTL_SET_INTEGRITY_INFORMATION   = 0x00090280;
inline constexpr uint32_t FSCTL_GET_INTEGRITY_INFORMATION   = 0x0009027C;

// File Attributes
inline constexpr uint32_t FILE_ATTR_READONLY         = 0x00000001;
inline constexpr uint32_t FILE_ATTR_HIDDEN           = 0x00000002;
inline constexpr uint32_t FILE_ATTR_SYSTEM           = 0x00000004;
inline constexpr uint32_t FILE_ATTR_DIRECTORY        = 0x00000010;
inline constexpr uint32_t FILE_ATTR_ARCHIVE          = 0x00000020;
inline constexpr uint32_t FILE_ATTR_NORMAL           = 0x00000080;
inline constexpr uint32_t FILE_ATTR_TEMPORARY        = 0x00000100;
inline constexpr uint32_t FILE_ATTR_SPARSE_FILE      = 0x00000200;
inline constexpr uint32_t FILE_ATTR_REPARSE_POINT    = 0x00000400;
inline constexpr uint32_t FILE_ATTR_COMPRESSED       = 0x00000800;
inline constexpr uint32_t FILE_ATTR_INTEGRITY_STREAM = 0x00008000;

// Integrity Checksum Types
enum class ChecksumType : uint16_t {
    None    = 0,
    Crc32c  = 1,
    Sha256  = 2
};

[[nodiscard]] inline const char* ChecksumTypeToString(ChecksumType ct) noexcept {
    switch (ct) {
        case ChecksumType::None:   return "NONE";
        case ChecksumType::Crc32c: return "CRC32C (Castagnoli)";
        case ChecksumType::Sha256: return "SHA-256";
        default:                   return "UNKNOWN";
    }
}

#pragma pack(push, 1)

// ReFS Superblock / Volume Header (4KB on-disk structure)
struct ReFsSuperblock {
    uint8_t  jumpBoot[3]{0xEB, 0x52, 0x90};
    char     fsSignature[8]{'R', 'e', 'F', 'S', 0, 0, 0, 0};
    uint16_t bytesPerSector{REFS_SECTOR_SIZE};
    uint32_t sectorsPerCluster{16}; // 16 * 4096 = 64KB
    uint16_t majorVersion{REFS_MAJOR_VERSION};
    uint16_t minorVersion{REFS_MINOR_VERSION};
    uint64_t totalSectors{0};
    uint64_t objectIdTableLcn{0x1000};
    uint64_t schemaTableLcn{0x2000};
    uint32_t volumeGuid[4]{0x54495441, 0x4E524546, 0x535F5356, 0x00261001}; // "TITANREFS_SV"
    uint32_t superblockChecksum{0};
    uint8_t  padding[4031]{0};
};

// ReFS Checkpoint Header (64-bit Transaction Checkpoint)
struct ReFsCheckpoint {
    uint64_t signature{0x544954414E43484BULL}; // "TITANCHK"
    uint64_t sequenceNumber{1};
    uint64_t transactionGroupId{1};
    uint64_t rootTreeLcn{0};
    uint64_t allocatorLcn{0};
    uint32_t checksum{0};
    uint8_t  reserved[20]{0};
};

#pragma pack(pop)

static_assert(sizeof(ReFsSuperblock) == 4096, "ReFsSuperblock must be exactly 4096 bytes");
static_assert(sizeof(ReFsCheckpoint) == 64, "ReFsCheckpoint must be exactly 64 bytes");

} // namespace titanrefs
