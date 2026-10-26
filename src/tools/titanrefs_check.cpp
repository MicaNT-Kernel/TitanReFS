#include "titanrefs/refs.hpp"
#include <iostream>
#include <string>
#include <vector>

void printUsage() {
    std::cout << "TitanReFS Check & Scrubber Utility (v3.12 Clean-Room Edition)\n"
              << "Usage: titanrefs_check <target_image_or_drive>\n\n"
              << "Inspects ReFS VBR boot sectors, superblocks, checkpoints, and audits\n"
              << "integrity streams against silent bit-rot.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    std::string target = argv[1];
    std::cout << "[*] Inspecting device: " << target << "\n";

    auto dev = std::make_shared<titanrefs::FileBlockDevice>("RefSDev", target, 4096);
    if (!dev->isOnline()) {
        std::cerr << "[-] Error: Failed to open target block device: " << target << "\n";
        return 1;
    }

    titanrefs::RefsFileSystem fs;
    auto status = fs.mount(dev);
    if (!titanrefs::IsSuccess(status)) {
        std::cerr << "[-] Mount failed: " << titanrefs::StatusToString(status) << "\n";
        return 1;
    }

    titanrefs::REFS_VOLUME_DATA_BUFFER info{};
    fs.getVolumeInformation(info);

    std::cout << "[+] ReFS Volume Mounted Successfully:\n"
              << "    Serial Number:  0x" << std::hex << info.volumeSerialNumber << std::dec << "\n"
              << "    Total Sectors:  " << info.numberSectors << "\n"
              << "    Total Clusters: " << info.totalClusters << "\n"
              << "    Free Clusters:  " << info.freeClusters << "\n"
              << "    Cluster Size:   " << info.bytesPerCluster << " bytes\n"
              << "    Version:        ReFS " << info.majorVersion << "." << info.minorVersion << "\n";

    std::cout << "[*] Running background integrity stream scrubber...\n";
    std::vector<std::wstring> corrupted;
    fs.scrubIntegrityStreams(corrupted);

    if (corrupted.empty()) {
        std::cout << "[+] Scrub Result: CLEAN. All integrity streams verified with zero bit-rot!\n";
    } else {
        std::cout << "[!] Scrub Result: " << corrupted.size() << " corrupted files detected!\n";
        for (const auto& f : corrupted) {
            std::wcout << L"    - " << f << L"\n";
        }
    }

    fs.unmount();
    return 0;
}
