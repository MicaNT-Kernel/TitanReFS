#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winternl.h>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdint>
#include "titanrefs/types.hpp"
#include "titanrefs/refs.hpp"

namespace titanrefs {

// ============================================================================
// NTSTATUS Codes
// ============================================================================

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS                   ((NTSTATUS)0x00000000L)
#define STATUS_OBJECT_NAME_NOT_FOUND     ((NTSTATUS)0xC0000034L)
#define STATUS_OBJECT_NAME_COLLISION     ((NTSTATUS)0xC0000035L)
#define STATUS_ACCESS_DENIED             ((NTSTATUS)0xC0000022L)
#define STATUS_END_OF_FILE               ((NTSTATUS)0xC0000011L)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023L)
#define STATUS_MEDIA_WRITE_PROTECTED     ((NTSTATUS)0xC00000A2L)
#define STATUS_FILE_IS_A_DIRECTORY       ((NTSTATUS)0xC00000BAL)
#define STATUS_NOT_A_DIRECTORY           ((NTSTATUS)0xC0000103L)
#define STATUS_DIRECTORY_NOT_EMPTY       ((NTSTATUS)0xC0000101L)
#endif

#pragma pack(push, 1)

struct FSP_FSCTL_VOLUME_INFO {
    UINT64 TotalSize;
    UINT64 FreeSize;
    UINT16 VolumeLabelLength;
    WCHAR  VolumeLabel[32];
};

struct FSP_FSCTL_FILE_INFO {
    UINT32 FileAttributes;
    UINT32 ReparseTag;
    UINT64 AllocationSize;
    UINT64 FileSize;
    UINT64 CreationTime;
    UINT64 LastAccessTime;
    UINT64 LastWriteTime;
    UINT64 ChangeTime;
    UINT64 IndexNumber;
    UINT32 HardLinks;
    UINT32 EaSize;
};

struct FSP_FSCTL_DIR_INFO {
    UINT16 Size;
    FSP_FSCTL_FILE_INFO FileInfo;
    union {
        UINT64 NextOffset;
        UINT8  Padding[24];
    };
    WCHAR  FileNameBuf[1];
};

struct FSP_FSCTL_VOLUME_PARAMS {
    UINT16 Version;
    UINT16 SectorSize;
    UINT16 SectorsPerAllocationUnit;
    UINT16 MaxComponentLength;
    UINT64 VolumeCreationTime;
    UINT32 VolumeSerialNumber;
    UINT32 TransactTimeout;
    UINT32 IrpTimeout;
    UINT32 IrpCapacity;
    UINT32 FileInfoTimeout;
    UINT32 Flags;
    WCHAR  FileSystemName[16];
    UINT8  Reserved[432];
};
static_assert(sizeof(FSP_FSCTL_VOLUME_PARAMS) == 504, "FSP_FSCTL_VOLUME_PARAMS must be exactly 504 bytes");

#pragma pack(pop)

struct _FSP_FILE_SYSTEM {
    PVOID UserContext;
};
typedef struct _FSP_FILE_SYSTEM FSP_FILE_SYSTEM;

struct FSP_FILE_SYSTEM_INTERFACE {
    NTSTATUS (*GetVolumeInfo)(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*SetVolumeLabel)(FSP_FILE_SYSTEM *FileSystem, PWSTR VolumeLabel, FSP_FSCTL_VOLUME_INFO *VolumeInfo);
    NTSTATUS (*GetSecurityByName)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize);
    NTSTATUS (*Create)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Open)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Overwrite)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, BOOLEAN ReplaceFileAttributes, UINT64 AllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    VOID     (*Cleanup)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags);
    VOID     (*Close)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext);
    NTSTATUS (*Read)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*Write)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Flush)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*GetFileInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetFileSize)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*SetBasicInfo)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo);
    NTSTATUS (*Rename)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists);
    NTSTATUS (*SetDelete)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile);
    NTSTATUS (*ReadDirectory)(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred);
    NTSTATUS (*ResolveReparsePoints)(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 ReparsePointIndex, BOOLEAN ResolveLastPathComponent, PIO_STATUS_BLOCK PIoStatus, PVOID Buffer, PSIZE_T PSize);
};

// Function pointer signatures for dynamically loaded WinFsp functions
typedef NTSTATUS (*PFN_FspFileSystemCreate)(
    PWSTR DevicePath,
    const FSP_FSCTL_VOLUME_PARAMS *VolumeParams,
    const FSP_FILE_SYSTEM_INTERFACE *Interface,
    FSP_FILE_SYSTEM **PFileSystem);

typedef NTSTATUS (*PFN_FspFileSystemSetMountPoint)(
    FSP_FILE_SYSTEM *FileSystem,
    PWSTR MountPoint);

typedef NTSTATUS (*PFN_FspFileSystemRemoveMountPoint)(
    FSP_FILE_SYSTEM *FileSystem);

typedef NTSTATUS (*PFN_FspFileSystemStartDispatcher)(
    FSP_FILE_SYSTEM *FileSystem,
    ULONG ThreadCount);

typedef VOID (*PFN_FspFileSystemStopDispatcher)(
    FSP_FILE_SYSTEM *FileSystem);

typedef VOID (*PFN_FspFileSystemDelete)(
    FSP_FILE_SYSTEM *FileSystem);

typedef BOOLEAN (*PFN_FspFileSystemAddDirInfo)(
    FSP_FSCTL_DIR_INFO *DirInfo,
    PVOID Buffer,
    ULONG Length,
    PULONG PBytesTransferred);

// ============================================================================
// WinFsp Dynamic API Loader
// ============================================================================

class WinFspApi {
public:
    static WinFspApi& Get() {
        static WinFspApi instance;
        return instance;
    }

    [[nodiscard]] bool isAvailable() const noexcept { return hModule_ != nullptr; }

    PFN_FspFileSystemCreate            Create{nullptr};
    PFN_FspFileSystemSetMountPoint     SetMountPoint{nullptr};
    PFN_FspFileSystemRemoveMountPoint  RemoveMountPoint{nullptr};
    PFN_FspFileSystemStartDispatcher   StartDispatcher{nullptr};
    PFN_FspFileSystemStopDispatcher    StopDispatcher{nullptr};
    PFN_FspFileSystemDelete            Delete{nullptr};
    PFN_FspFileSystemAddDirInfo        AddDirInfo{nullptr};

private:
    WinFspApi() {
        loadDll();
    }

    ~WinFspApi() {
        if (hModule_) {
            FreeLibrary(hModule_);
            hModule_ = nullptr;
        }
    }

    void loadDll() {
        hModule_ = LoadLibraryW(L"winfsp-x64.dll");
        if (!hModule_) {
            // Check Registry for WinFsp installation directory
            HKEY hKey;
            const wchar_t* subkeys[] = {
                L"SOFTWARE\\WOW6432Node\\WinFsp",
                L"SOFTWARE\\WinFsp"
            };
            for (const auto* subkey : subkeys) {
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subkey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
                    wchar_t installDir[MAX_PATH];
                    DWORD size = sizeof(installDir);
                    if (RegQueryValueExW(hKey, L"InstallDir", nullptr, nullptr, reinterpret_cast<LPBYTE>(installDir), &size) == ERROR_SUCCESS) {
                        std::wstring dllPath = std::wstring(installDir) + L"bin\\winfsp-x64.dll";
                        hModule_ = LoadLibraryW(dllPath.c_str());
                    }
                    RegCloseKey(hKey);
                    if (hModule_) break;
                }
            }
        }

        if (!hModule_) return;

        Create = reinterpret_cast<PFN_FspFileSystemCreate>(GetProcAddress(hModule_, "FspFileSystemCreate"));
        SetMountPoint = reinterpret_cast<PFN_FspFileSystemSetMountPoint>(GetProcAddress(hModule_, "FspFileSystemSetMountPoint"));
        RemoveMountPoint = reinterpret_cast<PFN_FspFileSystemRemoveMountPoint>(GetProcAddress(hModule_, "FspFileSystemRemoveMountPoint"));
        StartDispatcher = reinterpret_cast<PFN_FspFileSystemStartDispatcher>(GetProcAddress(hModule_, "FspFileSystemStartDispatcher"));
        StopDispatcher = reinterpret_cast<PFN_FspFileSystemStopDispatcher>(GetProcAddress(hModule_, "FspFileSystemStopDispatcher"));
        Delete = reinterpret_cast<PFN_FspFileSystemDelete>(GetProcAddress(hModule_, "FspFileSystemDelete"));
        AddDirInfo = reinterpret_cast<PFN_FspFileSystemAddDirInfo>(GetProcAddress(hModule_, "FspFileSystemAddDirInfo"));

        if (!Create || !SetMountPoint || !StartDispatcher || !StopDispatcher || !Delete || !AddDirInfo) {
            FreeLibrary(hModule_);
            hModule_ = nullptr;
        }
    }

    HMODULE hModule_{nullptr};
};

// ============================================================================
// File Context for Active Handles
// ============================================================================

struct RefsFileContext {
    std::wstring path;
    bool isDirectory{false};
    bool deleteOnClose{false};
};

// ============================================================================
// WinFspMountSession: User-Mode ReFS Filesystem Mount Driver
// ============================================================================

class WinFspMountSession {
public:
    WinFspMountSession() = default;
    ~WinFspMountSession() { unmount(); }

    bool mount(std::shared_ptr<RefsFileSystem> fs, const std::wstring& mountPoint) {
        if (!fs || mountPoint.empty()) return false;
        if (mounted_) return true;

        auto& fsp = WinFspApi::Get();
        if (!fsp.isAvailable()) return false;

        fs_ = std::move(fs);
        mountPoint_ = mountPoint;

        FSP_FSCTL_VOLUME_PARAMS params{};
        params.Version = sizeof(params);
        params.SectorSize = 4096;
        params.SectorsPerAllocationUnit = 16; // 64 KB ReFS clusters
        params.MaxComponentLength = 255;
        params.VolumeSerialNumber = 0x8E21F4A0;
        params.Flags = 0;
        const wchar_t fsName[] = L"ReFS";
        std::memcpy(params.FileSystemName, fsName, sizeof(fsName));

        FSP_FILE_SYSTEM_INTERFACE iface{};
        iface.GetVolumeInfo = &WinFspMountSession::CbGetVolumeInfo;
        iface.GetSecurityByName = &WinFspMountSession::CbGetSecurityByName;
        iface.Create = &WinFspMountSession::CbCreate;
        iface.Open = &WinFspMountSession::CbOpen;
        iface.Close = &WinFspMountSession::CbClose;
        iface.Cleanup = &WinFspMountSession::CbCleanup;
        iface.GetFileInfo = &WinFspMountSession::CbGetFileInfo;
        iface.Read = &WinFspMountSession::CbRead;
        iface.Write = &WinFspMountSession::CbWrite;
        iface.SetFileSize = &WinFspMountSession::CbSetFileSize;
        iface.SetBasicInfo = &WinFspMountSession::CbSetBasicInfo;
        iface.Rename = &WinFspMountSession::CbRename;
        iface.SetDelete = &WinFspMountSession::CbSetDelete;
        iface.ReadDirectory = &WinFspMountSession::CbReadDirectory;

        NTSTATUS status = fsp.Create(nullptr, &params, &iface, &fileSystem_);
        if (status != STATUS_SUCCESS || !fileSystem_) {
            return false;
        }

        fileSystem_->UserContext = this;

        status = fsp.SetMountPoint(fileSystem_, mountPoint_.data());
        if (status != STATUS_SUCCESS) {
            fsp.Delete(fileSystem_);
            fileSystem_ = nullptr;
            return false;
        }

        status = fsp.StartDispatcher(fileSystem_, 0);
        if (status != STATUS_SUCCESS) {
            fsp.RemoveMountPoint(fileSystem_);
            fsp.Delete(fileSystem_);
            fileSystem_ = nullptr;
            return false;
        }

        mounted_ = true;
        return true;
    }

    void unmount() {
        if (!mounted_ || !fileSystem_) return;
        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            fsp.StopDispatcher(fileSystem_);
            fsp.RemoveMountPoint(fileSystem_);
            fsp.Delete(fileSystem_);
        }
        fileSystem_ = nullptr;
        mounted_ = false;
    }

    [[nodiscard]] bool isMounted() const noexcept { return mounted_; }
    [[nodiscard]] const std::wstring& getMountPoint() const noexcept { return mountPoint_; }
    [[nodiscard]] std::shared_ptr<RefsFileSystem> getFileSystem() const noexcept { return fs_; }

    void populateFileInfo(const std::wstring& path, FSP_FSCTL_FILE_INFO* info) const {
        std::memset(info, 0, sizeof(*info));

        if (path.empty() || path == L"\\") {
            info->FileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            info->AllocationSize = 65536;
            info->FileSize = 65536;
            info->CreationTime = 133400000000000000ULL; // Standard WinNT timestamp
            info->LastAccessTime = 133400000000000000ULL;
            info->LastWriteTime = 133400000000000000ULL;
            info->ChangeTime = 133400000000000000ULL;
            info->IndexNumber = 4; // Root directory object ID
            return;
        }

        RefsFileRecord rec;
        if (fs_ && fs_->getFileRecord(path, rec)) {
            info->FileAttributes = rec.attributes;
            if (rec.isDirectory) {
                info->FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            }
            info->AllocationSize = rec.allocatedSize;
            info->FileSize = rec.size;
            info->CreationTime = 133400000000000000ULL;
            info->LastAccessTime = 133400000000000000ULL;
            info->LastWriteTime = 133400000000000000ULL;
            info->ChangeTime = 133400000000000000ULL;
            info->IndexNumber = rec.fileId;
        }
    }

private:
    static NTSTATUS CbGetVolumeInfo(FSP_FILE_SYSTEM *FileSystem, FSP_FSCTL_VOLUME_INFO *VolumeInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        REFS_VOLUME_DATA_BUFFER vol{};
        if (session->fs_->getVolumeInformation(vol) != Status::Success) {
            return STATUS_ACCESS_DENIED;
        }

        VolumeInfo->TotalSize = vol.totalClusters * vol.bytesPerCluster;
        VolumeInfo->FreeSize = vol.freeClusters * vol.bytesPerCluster;

        std::wstring label = session->fs_->getVolumeLabel();
        VolumeInfo->VolumeLabelLength = static_cast<UINT16>(label.size() * sizeof(WCHAR));
        std::memcpy(VolumeInfo->VolumeLabel, label.c_str(), VolumeInfo->VolumeLabelLength);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbGetSecurityByName(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, PUINT32 PFileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, SIZE_T *PSecurityDescriptorSize) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_OBJECT_NAME_NOT_FOUND;

        std::wstring path = (FileName && FileName[0]) ? FileName : L"\\";
        RefsFileRecord rec;
        bool exists = session->fs_->getFileRecord(path, rec);

        if (!exists && path != L"\\") {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        if (PFileAttributes) {
            *PFileAttributes = exists ? rec.attributes : FILE_ATTRIBUTE_DIRECTORY;
        }

        // World-accessible DACL (Everyone Read/Write)
        static const unsigned char RootSecurityDescriptor[] = {
            0x01, 0x00, 0x04, 0x80, 0x14, 0x00, 0x00, 0x00,
            0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x30, 0x00, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00,
            0x20, 0x02, 0x00, 0x00, 0x01, 0x02, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x05, 0x20, 0x00, 0x00, 0x00,
            0x20, 0x02, 0x00, 0x00, 0x02, 0x00, 0x1C, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14, 0x00,
            0xFF, 0x01, 0x1F, 0x00, 0x01, 0x01, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
        };

        if (PSecurityDescriptorSize) {
            if (*PSecurityDescriptorSize < sizeof(RootSecurityDescriptor)) {
                *PSecurityDescriptorSize = sizeof(RootSecurityDescriptor);
                return STATUS_BUFFER_TOO_SMALL;
            }
            *PSecurityDescriptorSize = sizeof(RootSecurityDescriptor);
            if (SecurityDescriptor) {
                std::memcpy(SecurityDescriptor, RootSecurityDescriptor, sizeof(RootSecurityDescriptor));
            }
        }

        return STATUS_SUCCESS;
    }

    static NTSTATUS CbCreate(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, UINT32 FileAttributes, PSECURITY_DESCRIPTOR SecurityDescriptor, UINT64 AllocationSize, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo) {
        (void)GrantedAccess;
        (void)SecurityDescriptor;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring path = FileName ? FileName : L"";
        bool isDir = (CreateOptions & FILE_DIRECTORY_FILE) != 0;

        if (isDir) {
            Status st = session->fs_->createDirectory(path);
            if (st == Status::ObjectNameCollision) return STATUS_OBJECT_NAME_COLLISION;
            if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;
        } else {
            Status st = session->fs_->createFile(path, FileAttributes, AllocationSize);
            if (st == Status::ObjectNameCollision) return STATUS_OBJECT_NAME_COLLISION;
            if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;
        }

        auto* ctx = new RefsFileContext();
        ctx->path = path;
        ctx->isDirectory = isDir;
        *PFileContext = ctx;

        session->populateFileInfo(path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbOpen(FSP_FILE_SYSTEM *FileSystem, PWSTR FileName, UINT32 CreateOptions, UINT32 GrantedAccess, PVOID *PFileContext, FSP_FSCTL_FILE_INFO *FileInfo) {
        (void)GrantedAccess;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        if (!session || !session->fs_) return STATUS_OBJECT_NAME_NOT_FOUND;

        std::wstring path = (FileName && FileName[0]) ? FileName : L"\\";
        bool isRoot = (path == L"\\");

        RefsFileRecord rec;
        if (!isRoot && !session->fs_->getFileRecord(path, rec)) {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        bool isDir = isRoot ? true : rec.isDirectory;
        if ((CreateOptions & FILE_NON_DIRECTORY_FILE) && isDir) {
            return STATUS_FILE_IS_A_DIRECTORY;
        }
        if ((CreateOptions & FILE_DIRECTORY_FILE) && !isDir) {
            return STATUS_NOT_A_DIRECTORY;
        }

        auto* ctx = new RefsFileContext();
        ctx->path = path;
        ctx->isDirectory = isDir;
        *PFileContext = ctx;

        session->populateFileInfo(path, FileInfo);
        return STATUS_SUCCESS;
    }

    static VOID CbCleanup(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, ULONG Flags) {
        (void)FileName;
        (void)Flags;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return;

        if (ctx->deleteOnClose) {
            if (ctx->isDirectory) {
                session->fs_->removeDirectory(ctx->path);
            } else {
                session->fs_->deleteFile(ctx->path);
            }
        }
    }

    static VOID CbClose(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext) {
        (void)FileSystem;
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        delete ctx;
    }

    static NTSTATUS CbGetFileInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, FSP_FSCTL_FILE_INFO *FileInfo) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbRead(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, PULONG PBytesTransferred) {
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (ctx->isDirectory) return STATUS_FILE_IS_A_DIRECTORY;

        std::span<uint8_t> dst(reinterpret_cast<uint8_t*>(Buffer), Length);
        size_t bytesRead = 0;
        Status st = session->fs_->readFile(ctx->path, Offset, dst, bytesRead);

        if (st == Status::NoSuchFile) return STATUS_OBJECT_NAME_NOT_FOUND;
        if (st == Status::DataChecksumError) {
            std::cerr << "[TitanReFS WinFsp] Integrity stream checksum error reading " 
                      << std::string(ctx->path.begin(), ctx->path.end()) << "\n";
            return 0xC00002EBL; // STATUS_DATA_CHECKSUM_ERROR
        }

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(bytesRead);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbWrite(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PVOID Buffer, UINT64 Offset, ULONG Length, BOOLEAN WriteToEndOfFile, BOOLEAN ConstrainedIo, PULONG PBytesTransferred, FSP_FSCTL_FILE_INFO *FileInfo) {
        (void)WriteToEndOfFile;
        (void)ConstrainedIo;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (ctx->isDirectory) return STATUS_FILE_IS_A_DIRECTORY;

        std::span<const uint8_t> src(reinterpret_cast<const uint8_t*>(Buffer), Length);
        size_t written = 0;
        Status st = session->fs_->writeFile(ctx->path, Offset, src, written);
        if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;

        if (PBytesTransferred) *PBytesTransferred = static_cast<ULONG>(written);
        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetFileSize(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT64 NewSize, BOOLEAN SetAllocationSize, FSP_FSCTL_FILE_INFO *FileInfo) {
        (void)SetAllocationSize;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (ctx->isDirectory) return STATUS_FILE_IS_A_DIRECTORY;

        Status st = session->fs_->setFileSize(ctx->path, NewSize);
        if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetBasicInfo(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, UINT32 FileAttributes, UINT64 CreationTime, UINT64 LastAccessTime, UINT64 LastWriteTime, UINT64 ChangeTime, FSP_FSCTL_FILE_INFO *FileInfo) {
        (void)CreationTime;
        (void)LastAccessTime;
        (void)LastWriteTime;
        (void)ChangeTime;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        if (FileAttributes != 0 && FileAttributes != INVALID_FILE_ATTRIBUTES) {
            session->fs_->setFileAttributes(ctx->path, FileAttributes);
        }

        session->populateFileInfo(ctx->path, FileInfo);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbRename(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, PWSTR NewFileName, BOOLEAN ReplaceIfExists) {
        (void)FileName;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;

        std::wstring newPath = NewFileName ? NewFileName : L"";
        Status st = session->fs_->renameFile(ctx->path, newPath, ReplaceIfExists);
        if (st == Status::ObjectNameCollision) return STATUS_OBJECT_NAME_COLLISION;
        if (st == Status::FileIsADirectory) return STATUS_FILE_IS_A_DIRECTORY;
        if (!IsSuccess(st)) return STATUS_ACCESS_DENIED;

        ctx->path = newPath;
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbSetDelete(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR FileName, BOOLEAN DeleteFile) {
        (void)FileSystem;
        (void)FileName;
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!ctx) return STATUS_ACCESS_DENIED;
        ctx->deleteOnClose = (DeleteFile != FALSE);
        return STATUS_SUCCESS;
    }

    static NTSTATUS CbReadDirectory(FSP_FILE_SYSTEM *FileSystem, PVOID FileContext, PWSTR Pattern, PWSTR Marker, PVOID Buffer, ULONG Length, PULONG PBytesTransferred) {
        (void)Pattern;
        auto* session = reinterpret_cast<WinFspMountSession*>(FileSystem->UserContext);
        auto* ctx = reinterpret_cast<RefsFileContext*>(FileContext);
        if (!session || !ctx || !session->fs_) return STATUS_ACCESS_DENIED;
        if (!ctx->isDirectory) return STATUS_NOT_A_DIRECTORY;

        auto& fsp = WinFspApi::Get();
        if (PBytesTransferred) *PBytesTransferred = 0;

        std::vector<RefsFileRecord> children;
        if (!session->fs_->listDirectory(ctx->path, children) && ctx->path != L"\\") {
            return STATUS_OBJECT_NAME_NOT_FOUND;
        }

        struct DirEntry {
            std::wstring name;
            RefsFileRecord rec;
        };
        std::vector<DirEntry> entries;

        // Add standard . and .. entries
        DirEntry dot;
        dot.name = L".";
        dot.rec.isDirectory = true;
        dot.rec.attributes = FILE_ATTRIBUTE_DIRECTORY;
        entries.push_back(dot);

        DirEntry dotdot;
        dotdot.name = L"..";
        dotdot.rec.isDirectory = true;
        dotdot.rec.attributes = FILE_ATTRIBUTE_DIRECTORY;
        entries.push_back(dotdot);

        for (auto& c : children) {
            size_t slash = c.fileName.find_last_of(L'\\');
            std::wstring base = (slash != std::wstring::npos) ? c.fileName.substr(slash + 1) : c.fileName;
            DirEntry de;
            de.name = base;
            de.rec = std::move(c);
            entries.push_back(std::move(de));
        }

        // Marker pagination
        size_t startIdx = 0;
        if (Marker && Marker[0]) {
            std::wstring wMarker(Marker);
            for (size_t i = 0; i < entries.size(); ++i) {
                if (entries[i].name == wMarker) {
                    startIdx = i + 1;
                    break;
                }
            }
        }

        std::vector<uint8_t> dirBuf(sizeof(FSP_FSCTL_DIR_INFO) + 260 * sizeof(WCHAR));
        for (size_t i = startIdx; i < entries.size(); ++i) {
            const auto& ent = entries[i];
            auto* pDirInfo = reinterpret_cast<FSP_FSCTL_DIR_INFO*>(dirBuf.data());
            std::memset(pDirInfo, 0, dirBuf.size());

            pDirInfo->FileInfo.FileAttributes = ent.rec.attributes;
            if (ent.rec.isDirectory) pDirInfo->FileInfo.FileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            pDirInfo->FileInfo.AllocationSize = ent.rec.allocatedSize;
            pDirInfo->FileInfo.FileSize = ent.rec.size;
            pDirInfo->FileInfo.CreationTime = 133400000000000000ULL;
            pDirInfo->FileInfo.LastAccessTime = 133400000000000000ULL;
            pDirInfo->FileInfo.LastWriteTime = 133400000000000000ULL;
            pDirInfo->FileInfo.IndexNumber = ent.rec.fileId ? ent.rec.fileId : 1;

            size_t nameBytes = ent.name.size() * sizeof(WCHAR);
            std::memcpy(pDirInfo->FileNameBuf, ent.name.c_str(), nameBytes);
            pDirInfo->Size = static_cast<UINT16>(sizeof(FSP_FSCTL_DIR_INFO) + nameBytes);

            if (!fsp.AddDirInfo(pDirInfo, Buffer, Length, PBytesTransferred)) {
                break;
            }
        }

        return STATUS_SUCCESS;
    }

    bool mounted_{false};
    std::wstring mountPoint_;
    std::shared_ptr<RefsFileSystem> fs_;
    FSP_FILE_SYSTEM* fileSystem_{nullptr};
};

} // namespace titanrefs
#endif
