#include "titanrefs/refs.hpp"
#include <iostream>
#include <string>
#include <vector>

void printUsage() {
    std::cout << "TitanReFS Format Utility (v3.12 Clean-Room Edition)\n"
              << "Usage: titanrefs_format <target_image_or_drive> [options]\n\n"
              << "Options:\n"
              << "  --cluster <size>   Cluster size in bytes (default: 65536 / 64KB)\n"
              << "  --label <name>     Volume label (default: TITAN_REFS)\n"
              << "  --size <MB>        Size in megabytes for new image files (default: 64)\n\n"
              << "Example:\n"
              << "  titanrefs_format disk.img --size 128 --label MyStorage\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string target = argv[1];
    uint32_t clusterSize = 65536;
    std::wstring label = L"TITAN_REFS";
    uint64_t sizeMb = 64;

    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--cluster" && i + 1 < argc) {
            clusterSize = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--label" && i + 1 < argc) {
            std::string l = argv[++i];
            label = std::wstring(l.begin(), l.end());
        } else if (arg == "--size" && i + 1 < argc) {
            sizeMb = std::stoull(argv[++i]);
        }
    }

    std::cout << "[*] Initializing block device: " << target << " (" << sizeMb << " MB)\n";
    auto dev = std::make_shared<titanrefs::FileBlockDevice>("RefSDev", target, 4096, sizeMb * 1024 * 1024);
    if (!dev->isOnline()) {
        std::cerr << "[-] Error: Failed to open target block device: " << target << "\n";
        return 1;
    }

    std::cout << "[*] Formatting volume with ReFS v3.12 architecture...\n";
    titanrefs::RefsFileSystem fs;
    auto status = fs.format(*dev, clusterSize, label);
    if (!titanrefs::IsSuccess(status)) {
        std::cerr << "[-] Format failed: " << titanrefs::StatusToString(status) << "\n";
        return 1;
    }

    titanrefs::REFS_VOLUME_DATA_BUFFER info{};
    fs.getVolumeInformation(info);

    std::cout << "[+] ReFS volume successfully synthesized and formatted!\n"
              << "    Total Clusters: " << info.totalClusters << "\n"
              << "    Cluster Size:   " << info.bytesPerCluster << " bytes\n"
              << "    ReFS Version:   " << info.majorVersion << "." << info.minorVersion << "\n"
              << "    Integrity:      " << (info.isIntegrityEnabled ? "ENABLED (CRC32C/CRC64)" : "DISABLED") << "\n";

    fs.unmount();
    return 0;
}
