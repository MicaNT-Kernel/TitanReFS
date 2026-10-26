#pragma once

#include "titanrefs/types.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <fstream>
#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace titanrefs {

// ============================================================================
// Block Device Interface
// ============================================================================

class IBlockDevice {
public:
    virtual ~IBlockDevice() = default;

    [[nodiscard]] virtual uint64_t getBlockSize() const noexcept = 0;
    [[nodiscard]] virtual uint64_t getTotalBlocks() const noexcept = 0;
    [[nodiscard]] virtual bool isOnline() const noexcept = 0;
    virtual void setOnline(bool online) noexcept = 0;
    [[nodiscard]] virtual const std::string& getDeviceName() const noexcept = 0;

    virtual Status readBlock(uint64_t lba, std::span<uint8_t> dst) = 0;
    virtual Status writeBlock(uint64_t lba, std::span<const uint8_t> src) = 0;
    virtual Status flush() = 0;
};

// ============================================================================
// In-Memory Block Device (for fast unit testing & ramdisks)
// ============================================================================

class MemoryBlockDevice : public IBlockDevice {
public:
    MemoryBlockDevice(std::string name, uint64_t totalBlocks, uint64_t blockSize = 4096)
        : name_(std::move(name)), totalBlocks_(totalBlocks), blockSize_(blockSize) {
        storage_.resize(totalBlocks_ * blockSize_, 0);
    }

    [[nodiscard]] uint64_t getBlockSize() const noexcept override { return blockSize_; }
    [[nodiscard]] uint64_t getTotalBlocks() const noexcept override { return totalBlocks_; }
    [[nodiscard]] bool isOnline() const noexcept override { return isOnline_; }
    void setOnline(bool online) noexcept override { isOnline_ = online; }
    [[nodiscard]] const std::string& getDeviceName() const noexcept override { return name_; }

    Status readBlock(uint64_t lba, std::span<uint8_t> dst) override {
        if (!isOnline_) return Status::NoSuchDevice;
        if (lba >= totalBlocks_ || dst.size() < blockSize_) return Status::InvalidParameter;
        std::memcpy(dst.data(), storage_.data() + (lba * blockSize_), blockSize_);
        return Status::Success;
    }

    Status writeBlock(uint64_t lba, std::span<const uint8_t> src) override {
        if (!isOnline_) return Status::NoSuchDevice;
        if (lba >= totalBlocks_ || src.size() < blockSize_) return Status::InvalidParameter;
        std::memcpy(storage_.data() + (lba * blockSize_), src.data(), blockSize_);
        return Status::Success;
    }

    Status flush() override {
        return Status::Success;
    }

    std::vector<uint8_t>& getRawStorage() noexcept { return storage_; }

private:
    std::string name_;
    uint64_t totalBlocks_{0};
    uint64_t blockSize_{4096};
    bool isOnline_{true};
    std::vector<uint8_t> storage_;
};

// ============================================================================
// Win32 / POSIX File & Physical Drive Block Device
// ============================================================================

class FileBlockDevice : public IBlockDevice {
public:
    FileBlockDevice(std::string name, std::string filePath, uint64_t blockSize = 4096, uint64_t defaultSize = 128 * 1024 * 1024)
        : name_(std::move(name)), filePath_(std::move(filePath)), blockSize_(blockSize) {
        openFile(defaultSize);
    }

    ~FileBlockDevice() override {
        closeFile();
    }

    // Non-copyable
    FileBlockDevice(const FileBlockDevice&) = delete;
    FileBlockDevice& operator=(const FileBlockDevice&) = delete;

    // Movable
    FileBlockDevice(FileBlockDevice&& other) noexcept {
        *this = std::move(other);
    }

    FileBlockDevice& operator=(FileBlockDevice&& other) noexcept {
        if (this != &other) {
            closeFile();
            name_ = std::move(other.name_);
            filePath_ = std::move(other.filePath_);
            blockSize_ = other.blockSize_;
            totalBlocks_ = other.totalBlocks_;
            isOnline_ = other.isOnline_;
#ifdef _WIN32
            handle_ = other.handle_;
            other.handle_ = INVALID_HANDLE_VALUE;
#else
            fd_ = other.fd_;
            other.fd_ = -1;
#endif
        }
        return *this;
    }

    [[nodiscard]] uint64_t getBlockSize() const noexcept override { return blockSize_; }
    [[nodiscard]] uint64_t getTotalBlocks() const noexcept override { return totalBlocks_; }
    [[nodiscard]] bool isOnline() const noexcept override { return isOnline_; }
    void setOnline(bool online) noexcept override { isOnline_ = online; }
    [[nodiscard]] const std::string& getDeviceName() const noexcept override { return name_; }

    Status readBlock(uint64_t lba, std::span<uint8_t> dst) override {
        if (!isOnline_) return Status::NoSuchDevice;
        if (lba >= totalBlocks_ || dst.size() < blockSize_) return Status::InvalidParameter;

        const uint64_t offset = lba * blockSize_;
#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, li, nullptr, FILE_BEGIN)) {
            return Status::DeviceError;
        }
        DWORD bytesRead = 0;
        if (!ReadFile(handle_, dst.data(), static_cast<DWORD>(blockSize_), &bytesRead, nullptr) || bytesRead != blockSize_) {
            return Status::DeviceError;
        }
#else
        if (pread(fd_, dst.data(), blockSize_, offset) != static_cast<ssize_t>(blockSize_)) {
            return Status::DeviceError;
        }
#endif
        return Status::Success;
    }

    Status writeBlock(uint64_t lba, std::span<const uint8_t> src) override {
        if (!isOnline_) return Status::NoSuchDevice;
        if (lba >= totalBlocks_ || src.size() < blockSize_) return Status::InvalidParameter;

        const uint64_t offset = lba * blockSize_;
#ifdef _WIN32
        LARGE_INTEGER li;
        li.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, li, nullptr, FILE_BEGIN)) {
            return Status::DeviceError;
        }
        DWORD bytesWritten = 0;
        if (!WriteFile(handle_, src.data(), static_cast<DWORD>(blockSize_), &bytesWritten, nullptr) || bytesWritten != blockSize_) {
            return Status::DeviceError;
        }
#else
        if (pwrite(fd_, src.data(), blockSize_, offset) != static_cast<ssize_t>(blockSize_)) {
            return Status::DeviceError;
        }
#endif
        return Status::Success;
    }

    Status flush() override {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(handle_);
        }
#else
        if (fd_ >= 0) {
            fdatasync(fd_);
        }
#endif
        return Status::Success;
    }

private:
    void openFile(uint64_t defaultSize) {
#ifdef _WIN32
        handle_ = CreateFileA(filePath_.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            isOnline_ = false;
            return;
        }
        LARGE_INTEGER fileSize;
        if (GetFileSizeEx(handle_, &fileSize) && fileSize.QuadPart > 0) {
            totalBlocks_ = static_cast<uint64_t>(fileSize.QuadPart) / blockSize_;
        } else {
            // Initialize new file with default size
            LARGE_INTEGER targetSize;
            targetSize.QuadPart = static_cast<LONGLONG>(defaultSize);
            SetFilePointerEx(handle_, targetSize, nullptr, FILE_BEGIN);
            SetEndOfFile(handle_);
            totalBlocks_ = defaultSize / blockSize_;
        }
#else
        fd_ = open(filePath_.c_str(), O_RDWR | O_CREAT, 0644);
        if (fd_ < 0) {
            isOnline_ = false;
            return;
        }
        struct stat st{};
        if (fstat(fd_, &st) == 0 && st.st_size > 0) {
            totalBlocks_ = static_cast<uint64_t>(st.st_size) / blockSize_;
        } else {
            ftruncate(fd_, defaultSize);
            totalBlocks_ = defaultSize / blockSize_;
        }
#endif
        isOnline_ = true;
    }

    void closeFile() {
#ifdef _WIN32
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
#else
        if (fd_ >= 0) {
            close(fd_);
            fd_ = -1;
        }
#endif
        isOnline_ = false;
    }

    std::string name_;
    std::string filePath_;
    uint64_t blockSize_{4096};
    uint64_t totalBlocks_{0};
    bool isOnline_{false};
#ifdef _WIN32
    HANDLE handle_{INVALID_HANDLE_VALUE};
#else
    int fd_{-1};
#endif
};

} // namespace titanrefs
