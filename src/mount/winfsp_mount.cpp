#include "titanrefs/refs.hpp"
#include "titanrefs/device.hpp"

#ifdef _WIN32
#include "titanrefs/winfsp_bridge.hpp"
#include <csignal>
#include <atomic>
#include <iostream>
#include <filesystem>

using namespace titanrefs;

namespace {
std::atomic<bool> g_Running{true};
WinFspMountSession g_Session;

void SigHandler(int) {
    std::cout << "\n[TitanReFS Mount] Stop signal received. Unmounting volume cleanly...\n";
    g_Running = false;
    g_Session.unmount();
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "========================================================================\n";
        std::cout << "    TitanReFS Filesystem Mount Driver (WinFsp Userland Bridge)         \n";
        std::cout << "========================================================================\n\n";
        std::cout << "Usage: titanrefs_mount <image_path_or_device> <drive_letter:> [options]\n\n";
        std::cout << "Options:\n";
        std::cout << "  --format          Format image as clean ReFS v3.12 volume before mounting\n";
        std::cout << "  --label <name>    Set volume label (default: Resilient Volume)\n";
        std::cout << "  --size <MB>       Backing file size in megabytes if created (default: 256)\n\n";
        std::cout << "Examples:\n";
        std::cout << "  titanrefs_mount refs_disk.raw R:\n";
        std::cout << "  titanrefs_mount refs_disk.raw R: --format --label MY_REFS\n";
        std::cout << "  titanrefs_mount \\\\.\\PhysicalDrive1 R:\n\n";
        return 0;
    }

    std::string devicePath;
    std::string driveLetter;
    bool doFormat = false;
    std::wstring volumeLabel = L"Resilient Volume";
    uint64_t diskSizeMB = 256;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--format") {
            doFormat = true;
        } else if (arg == "--label" && i + 1 < argc) {
            std::string lStr = argv[++i];
            volumeLabel = std::wstring(lStr.begin(), lStr.end());
        } else if (arg == "--size" && i + 1 < argc) {
            diskSizeMB = std::stoull(argv[++i]);
            if (diskSizeMB < 32) diskSizeMB = 32;
        } else if (devicePath.empty()) {
            devicePath = arg;
        } else if (driveLetter.empty()) {
            driveLetter = arg;
        }
    }

    if (devicePath.empty() || driveLetter.empty()) {
        std::cerr << "[TitanReFS Error] Both device/image path and drive letter must be specified.\n";
        return 1;
    }

    // Validate drive letter format (e.g., "R:" or "R:\")
    if (driveLetter.size() == 1) {
        driveLetter += ":";
    } else if (driveLetter.size() > 2 && driveLetter[1] == ':') {
        driveLetter = driveLetter.substr(0, 2);
    }

    auto& fsp = WinFspApi::Get();
    if (!fsp.isAvailable()) {
        std::cerr << "[TitanReFS Error] WinFsp dynamic library (winfsp-x64.dll) was not found.\n";
        std::cerr << "Please install WinFsp on this machine (e.g. run titanrefs_setup.exe or 'winget install WinFsp.WinFsp').\n";
        return 1;
    }

    uint64_t defaultBytes = diskSizeMB * 1024ULL * 1024ULL;
    auto dev = std::make_shared<FileBlockDevice>("refs_dev", devicePath, 4096, defaultBytes);
    if (!dev->isOnline()) {
        std::cerr << "[TitanReFS Error] Failed to open block device or raw image: " << devicePath << "\n";
        return 1;
    }

    auto fs = std::make_shared<RefsFileSystem>();

    if (doFormat) {
        std::cout << "[TitanReFS] Formatting " << devicePath << " with ReFS v3.12 (64KB clusters)...\n";
        auto st = fs->format(*dev, REFS_DEFAULT_CLUSTER_SIZE, volumeLabel);
        if (!IsSuccess(st)) {
            std::cerr << "[TitanReFS Error] Formatting failed with status code: 0x"
                      << std::hex << static_cast<uint32_t>(st) << "\n";
            return 1;
        }
        std::cout << "[TitanReFS] Formatting complete.\n";
    } else {
        auto st = fs->mount(dev);
        if (!IsSuccess(st)) {
            std::cout << "[TitanReFS Notice] Device does not contain a valid ReFS signature. Initializing clean ReFS volume...\n";
            st = fs->format(*dev, REFS_DEFAULT_CLUSTER_SIZE, volumeLabel);
            if (!IsSuccess(st)) {
                std::cerr << "[TitanReFS Error] Failed to initialize volume: 0x"
                          << std::hex << static_cast<uint32_t>(st) << "\n";
                return 1;
            }
        }
    }

    // Seed welcome document if empty
    std::vector<RefsFileRecord> rootFiles;
    if (fs->listDirectory(L"\\", rootFiles) && rootFiles.empty()) {
        const std::string welcomeText =
            "========================================================================\n"
            "   Welcome to TitanReFS!\n"
            "   Clean-Room ReFS v3.12 Native Storage Volume Engine for Windows NT\n"
            "========================================================================\n\n"
            "- Filesystem: Resilient File System (ReFS v3.12)\n"
            "- Allocation Unit: 64 KB Clusters\n"
            "- Integrity Streams: Active (CRC64 & Castagnoli CRC32C)\n"
            "- Block Cloning: Supported (FSCTL_DUPLICATE_EXTENTS_TO_FILE)\n"
            "- Clean-Room Provenance: 17 U.S.C. 1201(f) & Google v. Oracle Interoperability\n";

        fs->createFile(L"\\README.txt", FILE_ATTR_ARCHIVE | FILE_ATTR_INTEGRITY_STREAM, 0);
        size_t written = 0;
        fs->writeFile(L"\\README.txt", 0,
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(welcomeText.data()), welcomeText.size()),
            written);
        fs->createDirectory(L"\\Data");
    }

    std::signal(SIGINT, SigHandler);
    std::signal(SIGTERM, SigHandler);

    std::wstring wDrive(driveLetter.begin(), driveLetter.end());
    if (!g_Session.mount(fs, wDrive)) {
        std::cerr << "[TitanReFS Error] Failed to mount ReFS volume to drive " << driveLetter << "\n";
        return 1;
    }

    std::cout << "========================================================================\n";
    std::cout << "[TitanReFS SUCCESS] ReFS volume is MOUNTED to " << driveLetter << "\n";
    std::cout << "========================================================================\n";
    std::cout << "Device: " << devicePath << "\n";
    std::cout << "Drive Letter: " << driveLetter << "\\\n";
    std::cout << "Cluster Size: 64 KB\n";
    std::cout << "Integrity Streams: Enabled\n";
    std::cout << "Visible in Windows File Explorer, cmd, PowerShell, and all applications.\n";
    std::cout << "Press Ctrl+C to unmount cleanly...\n\n";

    while (g_Running) {
        Sleep(500);
    }

    g_Session.unmount();
    std::cout << "[TitanReFS] Volume " << driveLetter << " unmounted cleanly.\n";
    return 0;
}
#else
int main() {
    std::cout << "TitanReFS WinFsp driver bridge is a Windows NT native component.\n";
    return 0;
}
#endif
