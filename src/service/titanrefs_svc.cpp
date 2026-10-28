#include "titanrefs/refs.hpp"
#include "titanrefs/device.hpp"
#include "titanrefs/ipc.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <mutex>
#include <filesystem>

#ifdef _WIN32
#include "titanrefs/winfsp_bridge.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsvc.h>
#endif

namespace titanrefs {

struct MountedVolumeInfo {
    std::string devicePath;
    std::string driveLetter;
    std::shared_ptr<FileBlockDevice> device;
    std::shared_ptr<RefsFileSystem> fs;
#ifdef _WIN32
    std::shared_ptr<WinFspMountSession> session;
#endif
};

class TitanRefsDaemon {
public:
    static TitanRefsDaemon& Get() {
        static TitanRefsDaemon instance;
        return instance;
    }

    bool start() {
        if (running_) return true;
        running_ = true;

        std::cout << "========================================================================\n";
        std::cout << "        TitanReFS Windows NT Subsystem Service Daemon                   \n";
        std::cout << "========================================================================\n";
        std::cout << "[TitanReFS Daemon] Initializing filesystem and mount subsystem...\n";

#ifdef _WIN32
        auto& fsp = WinFspApi::Get();
        if (fsp.isAvailable()) {
            std::cout << "[TitanReFS Daemon] WinFsp dynamic engine loaded successfully.\n";
            loadConfigAndMount();
        } else {
            std::cout << "[TitanReFS Daemon] WinFsp not found; running in headless storage engine mode.\n";
        }
#endif

        // Start Named Pipe IPC Server
        ipcServer_.start([this](uint32_t cmd, const std::string& req, std::string& resp) {
            return this->handleIpcCommand(cmd, req, resp);
        });
        std::cout << "[TitanReFS Daemon] Named pipe IPC endpoint active: \\\\.\\pipe\\titanrefs_ipc\n";

        // Start 24-hour Autonomous Integrity Watchdog & Scrub Engine
        watchdogThread_ = std::thread(&TitanRefsDaemon::watchdogWorker, this);
        std::cout << "[TitanReFS Daemon] Autonomous integrity watchdog & bit-rot scrubber started.\n";
        std::cout << "[TitanReFS Daemon] Service is ready and accepting requests.\n\n";

        return true;
    }

    void stop() {
        if (!running_) return;
        std::cout << "\n[TitanReFS Daemon] Initiating graceful subsystem shutdown...\n";
        running_ = false;

        ipcServer_.stop();

        if (watchdogThread_.joinable()) {
            watchdogThread_.join();
        }

        std::lock_guard<std::mutex> lock(mountMutex_);
#ifdef _WIN32
        for (auto& [drive, vol] : mounts_) {
            std::cout << "[TitanReFS Daemon] Unmounting " << drive << " (" << vol.devicePath << ")...\n";
            if (vol.session) {
                vol.session->unmount();
            }
            if (vol.device) {
                vol.device->flush();
            }
        }
#endif
        mounts_.clear();
        std::cout << "[TitanReFS Daemon] Subsystem stopped. All ReFS volumes unmounted.\n";
    }

    [[nodiscard]] bool isRunning() const noexcept { return running_; }

    int mountVolume(const std::string& devPath, const std::string& driveLetter, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        std::string drive = normalizeDriveLetter(driveLetter);

        if (mounts_.find(drive) != mounts_.end()) {
            err = "Drive letter " + drive + " is already in use by TitanReFS.";
            return -1;
        }

#ifdef _WIN32
        auto& fsp = WinFspApi::Get();
        if (!fsp.isAvailable()) {
            err = "WinFsp dynamic driver library is not available.";
            return -1;
        }

        auto dev = std::make_shared<FileBlockDevice>("dev_" + drive, devPath, 4096, 256ULL * 1024 * 1024);
        if (!dev->isOnline()) {
            err = "Failed to open device or backing image: " + devPath;
            return -1;
        }

        auto fs = std::make_shared<RefsFileSystem>();
        auto st = fs->mount(dev);
        if (!IsSuccess(st)) {
            // Auto format if not yet initialized
            st = fs->format(*dev, REFS_DEFAULT_CLUSTER_SIZE, L"Resilient Volume");
            if (!IsSuccess(st)) {
                err = "Failed to mount or initialize ReFS volume.";
                return -1;
            }
        }

        std::wstring wDrive(drive.begin(), drive.end());
        auto session = std::make_shared<WinFspMountSession>();
        if (!session->mount(fs, wDrive)) {
            err = "WinFsp failed to mount filesystem to drive " + drive;
            return -1;
        }

        MountedVolumeInfo info;
        info.devicePath = devPath;
        info.driveLetter = drive;
        info.device = dev;
        info.fs = fs;
        info.session = session;

        mounts_[drive] = std::move(info);
        saveMountConfig();
        return 0;
#else
        (void)devPath;
        err = "WinFsp mounts are only supported on Windows NT.";
        return -1;
#endif
    }

    int unmountVolume(const std::string& driveLetter, std::string& err) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        std::string drive = normalizeDriveLetter(driveLetter);

        auto it = mounts_.find(drive);
        if (it == mounts_.end()) {
            err = "Drive " + drive + " is not currently mounted.";
            return -1;
        }

#ifdef _WIN32
        if (it->second.session) {
            it->second.session->unmount();
        }
        if (it->second.device) {
            it->second.device->flush();
        }
#endif
        mounts_.erase(it);
        saveMountConfig();
        return 0;
    }

    void runScrub(std::vector<std::pair<std::string, std::vector<std::wstring>>>& results) {
        std::lock_guard<std::mutex> lock(mountMutex_);
        results.clear();
        for (const auto& [drive, vol] : mounts_) {
            if (vol.fs) {
                std::vector<std::wstring> corrupted;
                vol.fs->scrubIntegrityStreams(corrupted);
                results.push_back({drive, std::move(corrupted)});
            }
        }
        lastScrubTime_ = std::chrono::system_clock::now();
    }

private:
    TitanRefsDaemon() = default;

    static std::string normalizeDriveLetter(std::string d) {
        if (d.empty()) return "R:";
        if (d.size() == 1) return d + ":";
        if (d.size() > 2 && d[1] == ':') return d.substr(0, 2);
        return d;
    }

    void watchdogWorker() {
        while (running_) {
            // Sleep in 1-second increments for responsive shutdown (24 hours = 86400 seconds)
            for (int i = 0; i < 86400 && running_; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (!running_) break;

            std::cout << "[TitanReFS Watchdog] Running scheduled 24-hour integrity stream scrub...\n";
            std::vector<std::pair<std::string, std::vector<std::wstring>>> scrubResults;
            runScrub(scrubResults);

            for (const auto& [drive, corrupted] : scrubResults) {
                if (corrupted.empty()) {
                    std::cout << "[TitanReFS Watchdog] Drive " << drive << ": All integrity streams intact (0 errors).\n";
                } else {
                    std::cerr << "[TitanReFS Watchdog WARNING] Drive " << drive << ": Found "
                              << corrupted.size() << " corrupted files!\n";
                    for (const auto& cf : corrupted) {
                        std::string sName(cf.begin(), cf.end());
                        std::cerr << "  - Corrupt: " << sName << "\n";
                    }
                }
            }
        }
    }

    void loadConfigAndMount() {
#ifdef _WIN32
        // Read mounts from HKLM\SOFTWARE\TitanReFS\Mounts
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TitanReFS\\Mounts", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t valName[256];
            DWORD valNameLen = 256;
            wchar_t valData[1024];
            DWORD valDataLen = sizeof(valData);
            DWORD valType = 0;
            DWORD index = 0;

            while (RegEnumValueW(hKey, index++, valName, &valNameLen, nullptr, &valType,
                                 reinterpret_cast<LPBYTE>(valData), &valDataLen) == ERROR_SUCCESS) {
                if (valType == REG_SZ) {
                    std::wstring wDrive(valName);
                    std::wstring wDev(valData);
                    std::string sDrive(wDrive.begin(), wDrive.end());
                    std::string sDev(wDev.begin(), wDev.end());
                    std::string err;
                    if (mountVolume(sDev, sDrive, err) == 0) {
                        std::cout << "[TitanReFS Daemon] Auto-mounted " << sDrive << " -> " << sDev << "\n";
                    }
                }
                valNameLen = 256;
                valDataLen = sizeof(valData);
            }
            RegCloseKey(hKey);
        }
#endif
    }

    void saveMountConfig() {
#ifdef _WIN32
        HKEY hKey;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TitanReFS\\Mounts", 0, nullptr,
                            REG_OPTION_NON_VOLATILE, KEY_WRITE | KEY_ALL_ACCESS, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            // Clear existing keys
            RegDeleteTreeW(hKey, nullptr);

            for (const auto& [drive, vol] : mounts_) {
                std::wstring wDrive(drive.begin(), drive.end());
                std::wstring wDev(vol.devicePath.begin(), vol.devicePath.end());
                RegSetValueExW(hKey, wDrive.c_str(), 0, REG_SZ,
                               reinterpret_cast<const BYTE*>(wDev.c_str()),
                               static_cast<DWORD>((wDev.size() + 1) * sizeof(wchar_t)));
            }
            RegCloseKey(hKey);
        }
#endif
    }

    int handleIpcCommand(uint32_t cmd, const std::string& request, std::string& response) {
        switch (cmd) {
            case IpcCmdPing:
                response = "PONG";
                return 0;

            case IpcCmdServiceStatus: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                std::ostringstream ss;
                ss << "Status: Active\n";
                ss << "Mounted Volumes: " << mounts_.size() << "\n";
                response = ss.str();
                return 0;
            }

            case IpcCmdListMounts: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                std::ostringstream ss;
                for (const auto& [drive, vol] : mounts_) {
                    ss << drive << " -> " << vol.devicePath << "\n";
                }
                response = ss.str();
                return 0;
            }

            case IpcCmdVolumeMount: {
                // Request format: "<devicePath>|<driveLetter>"
                size_t sep = request.find('|');
                if (sep == std::string::npos) {
                    response = "Invalid mount request format. Expected: <devicePath>|<driveLetter>";
                    return 1;
                }
                std::string devPath = request.substr(0, sep);
                std::string drive = request.substr(sep + 1);
                std::string err;
                int ret = mountVolume(devPath, drive, err);
                if (ret != 0) {
                    response = err;
                    return 1;
                }
                response = "Mounted " + devPath + " to " + drive;
                return 0;
            }

            case IpcCmdVolumeUnmount: {
                std::string err;
                int ret = unmountVolume(request, err);
                if (ret != 0) {
                    response = err;
                    return 1;
                }
                response = "Unmounted " + request;
                return 0;
            }

            case IpcCmdScrubStart: {
                std::vector<std::pair<std::string, std::vector<std::wstring>>> results;
                runScrub(results);
                std::ostringstream ss;
                ss << "Scrub completed across " << results.size() << " volume(s).\n";
                for (const auto& [drive, corrupted] : results) {
                    ss << "Drive " << drive << ": " << corrupted.size() << " corrupted files detected.\n";
                    for (const auto& cf : corrupted) {
                        std::string s(cf.begin(), cf.end());
                        ss << "  - " << s << "\n";
                    }
                }
                response = ss.str();
                return 0;
            }

            case IpcCmdScrubStatus: {
                std::lock_guard<std::mutex> lock(mountMutex_);
                std::ostringstream ss;
                ss << "Last Scrub: "
                   << std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::system_clock::now() - lastScrubTime_).count()
                   << " seconds ago\n";
                response = ss.str();
                return 0;
            }

            default:
                response = "Unknown command";
                return 2;
        }
    }

    std::atomic<bool> running_{false};
    std::mutex mountMutex_;
    std::map<std::string, MountedVolumeInfo> mounts_;
    IpcServer ipcServer_;
    std::thread watchdogThread_;
    std::chrono::system_clock::time_point lastScrubTime_{std::chrono::system_clock::now()};
};

#ifdef _WIN32
// ============================================================================
// Windows NT Service Control Manager Dispatcher
// ============================================================================

SERVICE_STATUS g_ServiceStatus{};
SERVICE_STATUS_HANDLE g_hStatusHandle{nullptr};

VOID WINAPI ServiceCtrlHandler(DWORD request) {
    switch (request) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
            g_ServiceStatus.dwCurrentState = SERVICE_STOP_PENDING;
            SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);
            TitanRefsDaemon::Get().stop();
            g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
            SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);
            break;
        default:
            SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);
            break;
    }
}

VOID WINAPI ServiceMain(DWORD, LPWSTR*) {
    g_hStatusHandle = RegisterServiceCtrlHandlerW(L"TitanReFS", ServiceCtrlHandler);
    if (!g_hStatusHandle) return;

    g_ServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_ServiceStatus.dwCurrentState = SERVICE_START_PENDING;
    g_ServiceStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;
    SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);

    auto& daemon = TitanRefsDaemon::Get();
    if (daemon.start()) {
        g_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
        SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);

        while (daemon.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    g_ServiceStatus.dwCurrentState = SERVICE_STOPPED;
    SetServiceStatus(g_hStatusHandle, &g_ServiceStatus);
}

static bool InstallService() {
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) {
        std::cerr << "Failed to get current executable path.\n";
        return false;
    }

    SC_HANDLE schSCManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        std::cerr << "Failed to open Service Control Manager (Administrator privileges required).\n";
        return false;
    }

    SC_HANDLE schService = CreateServiceW(
        schSCManager,
        L"TitanReFS",
        L"TitanReFS Volume Engine & Scrubber Service",
        SERVICE_ALL_ACCESS,
        SERVICE_WIN32_OWN_PROCESS,
        SERVICE_AUTO_START,
        SERVICE_ERROR_NORMAL,
        path,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr
    );

    if (!schService) {
        DWORD err = GetLastError();
        if (err == ERROR_SERVICE_EXISTS) {
            std::cout << "Service 'TitanReFS' is already installed.\n";
        } else {
            std::cerr << "CreateService failed with error: " << err << "\n";
        }
        CloseServiceHandle(schSCManager);
        return false;
    }

    SERVICE_DESCRIPTIONW desc;
    wchar_t descStr[] = L"TitanReFS clean-room ReFS v3.12 filesystem engine, auto-mount driver, and background bit-rot scrub watchdog.";
    desc.lpDescription = descStr;
    ChangeServiceConfig2W(schService, SERVICE_CONFIG_DESCRIPTION, &desc);

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);

    std::cout << "Successfully registered 'TitanReFS' with Windows Service Control Manager (AUTO_START).\n";
    return true;
}

static bool UninstallService() {
    SC_HANDLE schSCManager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!schSCManager) {
        std::cerr << "Failed to open Service Control Manager (Administrator privileges required).\n";
        return false;
    }

    SC_HANDLE schService = OpenServiceW(schSCManager, L"TitanReFS", SERVICE_STOP | DELETE);
    if (!schService) {
        std::cerr << "Service 'TitanReFS' not found.\n";
        CloseServiceHandle(schSCManager);
        return false;
    }

    SERVICE_STATUS status;
    ControlService(schService, SERVICE_CONTROL_STOP, &status);

    if (!DeleteService(schService)) {
        std::cerr << "DeleteService failed with error: " << GetLastError() << "\n";
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return false;
    }

    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);

    std::cout << "Successfully uninstalled 'TitanReFS' Windows Service.\n";
    return true;
}
#endif

} // namespace titanrefs

// ============================================================================
// Main Entrypoint
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc >= 2) {
        std::string action = argv[1];

#ifdef _WIN32
        if (action == "install" || action == "--install") {
            return titanrefs::InstallService() ? 0 : 1;
        }
        if (action == "uninstall" || action == "--uninstall") {
            return titanrefs::UninstallService() ? 0 : 1;
        }
#endif
        if (action == "run" || action == "--console" || action == "-c" || action == "--debug") {
            auto& daemon = titanrefs::TitanRefsDaemon::Get();
            daemon.start();

            std::signal(SIGINT, [](int) {
                titanrefs::TitanRefsDaemon::Get().stop();
            });
            std::signal(SIGTERM, [](int) {
                titanrefs::TitanRefsDaemon::Get().stop();
            });

            while (daemon.isRunning()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            return 0;
        }

        std::cout << "TitanReFS Windows NT Subsystem Service Daemon\n";
        std::cout << "Usage: titanrefs_svc [command]\n\n";
        std::cout << "Commands:\n";
        std::cout << "  run, --console, -c     Run daemon directly in console (interactive/debug)\n";
#ifdef _WIN32
        std::cout << "  install, --install     Register as Windows NT Auto-Start Service\n";
        std::cout << "  uninstall, --uninstall Unregister and remove Windows NT Service\n";
#endif
        return 0;
    }

#ifdef _WIN32
    // Started by Windows Service Control Manager
    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        { const_cast<LPWSTR>(L"TitanReFS"), titanrefs::ServiceMain },
        { nullptr, nullptr }
    };

    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        // Fall back to console mode if executed without arguments in an interactive shell
        auto& daemon = titanrefs::TitanRefsDaemon::Get();
        daemon.start();
        while (daemon.isRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
#else
    auto& daemon = titanrefs::TitanRefsDaemon::Get();
    daemon.start();
    while (daemon.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
#endif
    return 0;
}
