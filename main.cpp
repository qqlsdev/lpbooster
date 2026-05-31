#include "Logger.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

const std::vector<std::string_view> vtDisks = {"loop", "ram", "dm", "nbd",
                                               "md"};

const std::array<std::string, 32> services = {
    "bluetooth.service",
    "cups.service",
    "cups-browsed.service",
    "pcscd.service",
    "brltty.service",
    "speech-dispatcher.service",
    "spice-vdagent.service",
    "qemu-guest-agent.service",
    "unattended-upgrades.service",
    "apport.service",
    "whoopsie.service",
    "geoclue.service",
    "systemd-networkd.service",
    "dhcpcd.service",
    "pstore.service",
    "systemd-pstore.service",
    "smartd.service",
    "systemd-boot-check-no-failures.service",
    "avahi-daemon.service",
    "irqbalance.service",
    "rpcbind.socket",
    "rpcbind.service",
    "nfs-server.service",
    "rpc-statd.service",
    "evolution-addressbook-factory.service",
    "evolution-calendar-factory.service",
    "gssproxy.service",
    "apt-daily.timer",
    "apt-daily-upgrade.timer",
    "hv-kvp-daemon.service",
    "hv-vss-daemon.service",
    "hv-fcopy-daemon.service",
};

namespace Colors {
constexpr std::string_view RESET = "\033[0m";
constexpr std::string_view BOLD = "\033[1m";
constexpr std::string_view GREEN = "\033[32m";
constexpr std::string_view CYAN = "\033[36m";
constexpr std::string_view YELLOW = "\033[33m";
} // namespace Colors

void helpMsg() {
  std::cout << Colors::BOLD << "Usage:" << Colors::RESET << " lpboost "
            << Colors::CYAN << "[OPTIONS]" << Colors::RESET << "\n\n"
            << Colors::BOLD << "System Optimization Utility\n\n"
            << Colors::RESET << Colors::BOLD << "Options:\n"
            << Colors::RESET << "  " << Colors::GREEN << "-h, --help"
            << Colors::RESET << "\t\tShow this help message and exit\n"
            << "  " << Colors::GREEN << "--clean-system" << Colors::RESET
            << "\tClean garbage, caches, and old system logs\n"
            << "  " << Colors::GREEN << "--disable-services" << Colors::RESET
            << "\tDisable unnecessary systemd services & timers\n"
            << "  " << Colors::GREEN << "--game-mode" << Colors::RESET
            << "\t\tOptimize OS tweaks for maximum gaming performance\n"
            << "  " << Colors::GREEN << "--disk-optimization" << Colors::RESET
            << "\tOptimize I/O scheduler for SSD/NVMe drives\n\n"
            << Colors::YELLOW << "Note:" << Colors::RESET
            << " Most options require root privileges (" << Colors::BOLD
            << "sudo" << Colors::RESET << ").\n";
}

Logger logger;

class MainManager {
private:
  int mngCount = 0;
  const std::string_view blockPath = "/sys/block/";
  const std::string_view cpuPath = "/sys/devices/system/cpu/";
  const std::string srvPath = "/etc/systemd/system/lpbooster-cpu.service";
  static constexpr double Gigabyte = 1024 * 1024 * 1024;

  bool isDriveRotational(const fs::path &devicePath) {
    fs::path rotPath = devicePath / "queue" / "rotational";
    if (!fs::exists(rotPath))
      return false;

    std::ifstream reader(rotPath);
    char isRotational = '0';
    if (reader.is_open()) {
      reader >> isRotational;
      return isRotational == '1';
    }
    return false;
  }

public:
  double getFreeRAM() {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long long freePages = sysconf(_SC_AVPHYS_PAGES);
    const long long freeBytes = freePages * pageSize;
    return static_cast<double>(freeBytes) / (Gigabyte);
  }

  double getTotalRAM() {
    const long pageSize = sysconf(_SC_PAGESIZE);
    const long long totalPages = sysconf(_SC_PHYS_PAGES);
    const long long totalBytes = totalPages * pageSize;
    return static_cast<double>(totalBytes) / (Gigabyte);
  }

  void detectDrives(bool &outHDD, bool &outSSD) {

    outHDD = outSSD = false;

    if (!fs::exists(blockPath))
      return;

    for (auto const &entry : fs::directory_iterator(blockPath)) {
      std::string dName = entry.path().filename().string();
      auto vt = std::any_of(
          vtDisks.begin(), vtDisks.end(),
          [&](const std::string_view vtd) { return dName.starts_with(vtd); });
      if (vt)
        continue;

      fs::path rotPath = entry.path() / "queue" / "rotational";
      if (!fs::exists(rotPath))
        continue;
      if (isDriveRotational(entry.path()))
        outHDD = true;
      else
        outSSD = true;

      if (outHDD && outSSD)
        return;
    }
  }

  bool disableServices(const char skip) {
    std::vector<std::string> toDisable;
    for (auto const &srv : services) {
      if (srv == "bluetooth.service" && (skip == 'y' || skip == 'Y')) {
        continue;
      }
      toDisable.push_back(srv);
    }

    bool hdd, ssd;
    detectDrives(hdd, ssd);

    if (hdd) {
      toDisable.push_back("tracker-miner-fs-3.service");

      if (!ssd) {
        toDisable.push_back("fstrim.timer");
      }
    }

    if (toDisable.empty()) {
      logger.LOG(1, "[!] NO SERVICES TO DISABLE");
      return false;
    }

    std::string cmd = "systemctl disable --now";
    for (const auto &srv : toDisable) {
      cmd += " " + srv;
    }
    cmd += " > /dev/null 2>&1";

    if (system(cmd.c_str()) == 0) {
      logger.LOG(0, "[+] UNNECESSARY SERVICES DISABLED SUCCESSFULLY.");
      return true;
    } else {
      logger.LOG(2, "[-] FAILED TO DISABLE SERVICES.");
      return false;
    }
  }

  void cleanSystem() {
    mngCount = 0;

    auto runCmd = [this](const std::string &path, const std::string &cmd,
                         const std::string &msg) {
      if (fs::exists(path)) {
        logger.LOG(0, msg);
        if (system(cmd.c_str()) != 0) {
          logger.LOG(2, "[-] FAILED TO RUN COMMAND");
        }
        mngCount++;
      }
    };

    runCmd("/usr/bin/pacman",
           "pacman -Rsu $(pacman -Qdtq) --noconfirm > /dev/null 2>&1",
           "[+] PACMAN DETECTED. CLEANING ORPHAN PACKAGES...");
    runCmd("/usr/bin/apt-get",
           "apt-get autoremove -y > /dev/null 2>&1 && apt-get clean > "
           "/dev/null 2>&1",
           "[+] APT DETECTED. REMOVING UNUSED DEPENDENCIES...");
    runCmd("/usr/bin/dnf", "dnf autoremove -y > /dev/null 2>&1",
           "[+] DNF DETECTED. CLEANING UP...");
    runCmd("/usr/bin/flatpak", "flatpak uninstall --unused -y > /dev/null 2>&1",
           "[+] FLATPAK DETECTED. REMOVING UNUSED RUNTIMES...");
    runCmd("/usr/bin/fc-cache", "fc-cache -r > /dev/null 2>&1",
           "[+] REBUILDING FONT CACHE...");
    runCmd("/var/lib/systemd/coredump",
           "rm -rf /var/lib/systemd/coredump/* > /dev/null 2>&1",
           "[+] REMOVING OLD COREDUMPS...");

    const std::string cache[] = {"thumbnails", "fontconfig", "pip"};
    const char *user = getenv("SUDO_USER");

    if (user != nullptr) {
      fs::path cacheDir = "/home" / fs::path(user) / ".cache";

      for (const auto &c : cache) {
        fs::path pCache = cacheDir / c;

        if (!fs::exists(pCache)) {
          logger.LOG(1, std::format("[!] PATH {} IS NOT EXIST, SKIPPING",
                                    pCache.string()));
          continue;
        }

        fs::remove_all(pCache);
        if (!fs::exists(pCache)) {
          logger.LOG(
              0, std::format("[+] PATH: {} CLEANED SUCCESS!", pCache.string()));
        } else {
          logger.LOG(1,
                     std::format("[-] FAIL TO CLEAN UP: {}", pCache.string()));
        }
      }

      if (system("journalctl --vacuum-size=50M > /dev/null 2>&1") == 0) {
        logger.LOG(0, "[+] SYSTEM JOURNAL CLEANED UP!");
      }
    }
  }

  void gamingMode() {
    if (!fs::exists(cpuPath)) {
      logger.LOG(2, std::format("[-] PATH NOT FOUND: {}", cpuPath));
      return;
    }

    for (const auto &entry : fs::directory_iterator(cpuPath)) {
      if (entry.is_directory() &&
          entry.path().filename().string().starts_with("cpu")) {

        fs::path govPath = entry.path() / "cpufreq" / "scaling_governor";

        if (!fs::exists(govPath)) {
          logger.LOG(0, std::format("[!] GOVERNOR NOT FOUND FOR: {}",
                                    entry.path().filename().string()));
          continue;
        }

        std::ifstream reader(govPath);
        std::string currGov;
        if (reader.is_open()) {
          std::getline(reader, currGov);
          reader.close();
          if (currGov == "powersave") {
            std::ofstream writer(govPath);
            if (writer.is_open()) {
              writer << "performance";
              writer.close();
              logger.LOG(0, "[+] POWERSAVE MODE CHANGED TO PERFORMANCE");
            }
          } else if (currGov == "performance") {
            logger.LOG(0, "[!] CPU GOVERNOR IS ALREADY IN PERFORMANCE MODE");
          } else {
            logger.LOG(0, "[!] UNKNOWN CPU GOVERNOR");
          }
        }
      }
    }

    const std::vector<std::string> vmArr = {
        "vm.swappiness=10", "vm.dirty_background_ratio=5", "vm.dirty_ratio=10",
        "vm.vfs_cache_pressure=50"};
    const std::vector<std::string> kernArr = {
        "kernel.sched_latency_ns=4000000",
        "kernel.sched_min_granularity_ns=1000000",
        "kernel.sched_wakeup_granularity_ns=1000000"};

    auto runCmd = [](int i, const std::vector<std::string> &arr) {
      std::string cmd =
          "echo " + arr[i] +
          " | tee -a /etc/sysctl.d/99-sysctl.conf > /dev/null 2>&1";
      return (system(cmd.c_str()) == 0);
    };

    if (runCmd(0, vmArr)) {
      logger.LOG(0, "[!] SWAPPINESS SET TO OPTIMAL VALUE");
    } else {
      logger.LOG(2, "[-] SOMETHING WENT WRONG WHILE CONFIGURING SWAPPINESS");
    }

    bool k1 = runCmd(0, kernArr);
    bool k2 = runCmd(1, kernArr);
    bool k3 = runCmd(2, kernArr);

    std::vector<bool> kernel = {k1, k2, k3};
    for (size_t i = 0; i < 3; ++i) {
      if (!kernel[i]) {
        logger.LOG(
            1, std::format("[-] SCHEDULER SETUP FOR {} FAILED", kernArr[i]));
      } else {
        logger.LOG(
            0, std::format("[+] SCHEDULER SETUP FOR {} SUCCESS", kernArr[i]));
      }
    }

    if (fs::exists(srvPath)) {
      logger.LOG(0, "[!] SYSTEMD SERVICE ALREADY EXISTS, SKIPPING");
    } else {
      logger.LOG(0, "[*] CREATING PERSISTENT SYSTEMD SERVICE...");

      std::ofstream file(srvPath);
      if (file.is_open()) {
        file << "[Unit]\n"
             << "Description=Linux Performance Booster\n"
             << "After=multi-user.target\n\n"
             << "[Service]\n"
             << "ExecStart=/bin/sh -c 'echo performance | tee "
                "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'\n"
             << "Type=oneshot\n"
             << "RemainAfterExit=yes\n\n"
             << "[Install]\n"
             << "WantedBy=multi-user.target";
        file.close();

        if (system("systemctl daemon-reload && systemctl enable "
                   "lpbooster-cpu.service > /dev/null 2>&1") == 0) {
          logger.LOG(0, "[+] SERVICE SETUP SUCCESSFUL!");
        } else {
          logger.LOG(2, "[-] FAILED TO SETUP SERVICE.");
        }
      } else {
        logger.LOG(2, std::format("[-] FAILED TO OPEN FILE: {}", srvPath));
      }
    }

    logger.LOG(0, "[!] PREPARING VIRTUAL STORAGE TUNING...");
    bool hdd, ssd;
    detectDrives(hdd, ssd);
    if (ssd || getTotalRAM() >= 8.0) {
      bool s1 = runCmd(1, vmArr);
      bool s2 = runCmd(2, vmArr);
      if (s1 && s2) {
        logger.LOG(0, "[+] VIRTUAL STORAGE TUNING COMPLETE!");
      } else {
        logger.LOG(2, "[-] FAILED TO COMPLETE VIRTUAL STORAGE TUNING");
      }
    }

    bool s1 = runCmd(3, vmArr);
    if (!s1) {
      logger.LOG(1, "[!] FAILED TO SETUP OPTIMAL CACHE PRESSURE VALUE");
    } else {
      logger.LOG(0, "[+] CACHE PRESSURE VALUE SET TO OPTIMAL!");
    }

    std::string gmSrv[] = {"ananicy.service", "gamemoded.service"};

    for (auto const &gm : gmSrv) {

      std::string cmd = "systemctl status " + gm + " > /dev/null 2>&1";
      std::string onCmd = "systemctl enable --now " + gm + " > /dev/null 2>&1";

      if (system(cmd.c_str()) == 0) {
        if (system(onCmd.c_str()) == 0) {
          logger.LOG(0, std::format("[+] SERVICE: {} ENABLED!", gm));
        } else {
          logger.LOG(1, std::format("[-] FAIL TO ENABLE {}", gm));
        }
      } else {
        logger.LOG(1, std::format("[!] SERVICE: {} DOESN'T EXIST", gm));
      }
    }
    fs::path splPath = "/proc/sys/kernel/split_lock_mitigate";

    if (fs::exists(splPath)) {
      std::ifstream reader(splPath);
      char currMode = '0';
      bool isOptimized = false;

      if (reader.is_open()) {
        if (reader >> currMode && currMode == '0') {
          logger.LOG(0, "[!] SPLIT LOCK ALREADY OPTIMIZED!");
          isOptimized = true;
        }
        reader.close();
      }

      if (!isOptimized) {
        std::ofstream writer(splPath);
        if (writer.is_open()) {
          writer << "0";
          writer.close();
          logger.LOG(0, "[+] SPLIT LOCK SUCCESS OPTIMIZED!");
        }
      }

    } else {
      logger.LOG(2, std::format("[-] PATH {} IS NOT EXIST", splPath.string()));
    }

    const fs::path pciePath = "/sys/module/pcie_aspm/parameters/policy";
    std::string currMode;
    const std::string defModes[] = {"default", "powersave", "powersupersave"};

    if (fs::exists(pciePath)) {

      std::ifstream reader(pciePath);
      bool isFind = false;

      if (reader.is_open()) {
        std::getline(reader, currMode);
        for (auto const def : defModes) {
          if (currMode.find("[" + def + "]") != std::string::npos) {
            isFind = true;
          }
        }
        reader.close();
      } else {
        logger.LOG(2, "[-] FAILED TO OPEN FILE FOR READING");
      }

      if (isFind) {
        std::ofstream writer(pciePath);
        if (writer.is_open()) {
          writer << "performance";
          writer.close();
          logger.LOG(0, "[+] PCIE SUCCESS CHANGED TO PERFORMANCE");
        } else {
          logger.LOG(2, "[-] FAILED TO OPEN FILE FOR WRITE");
        }
      }
    } else {
      logger.LOG(1,
                 std::format("[-] FILE {} DOES NOT EXIST", pciePath.string()));
    }

  } // void gamingMode()

  void schedulerSetup() {
    std::string currMode;

    if (!fs::exists(blockPath)) {
      logger.LOG(2, std::format("[-] PATH DOES NOT EXIST: {}", blockPath));
      return;
    }

    logger.LOG(0, "[+] LOOKING FOR STORAGE DEVICES...");
    for (auto const &entry : fs::directory_iterator(blockPath)) {
      const std::string device_name = entry.path().filename().string();
      bool isVirtual = std::any_of(
          vtDisks.begin(), vtDisks.end(),
          [&](std::string_view vtd) { return device_name.starts_with(vtd); });

      if (isVirtual) {
        logger.LOG(0, "[!] SKIPPING VIRTUAL DEVICE");
        continue;
      }

      const fs::path schedPath = entry.path() / "queue" / "scheduler";

      if (!fs::exists(schedPath)) {
        continue;
      }

      std::string mode = isDriveRotational(entry.path()) ? "bfq" : "none";

      std::ifstream reader(schedPath);

      if (reader.is_open()) {
        std::getline(reader, currMode);
        reader.close();
        if (currMode.find("[" + mode + "]") != std::string::npos) {
          logger.LOG(0, std::format("[!] SCHEDULER FOR {} ALREADY OPTIMIZED!",
                                    device_name));
          continue;
        }
      } else {
        logger.LOG(1, std::format("[-] ERROR TO OPEN FILE {} FOR READING",
                                  device_name));
        continue;
      }

      std::ofstream writer(schedPath);

      if (writer.is_open()) {
        writer << mode;
        writer.close();
        logger.LOG(0, std::format("[+] I/O SCHEDULER OPTIMIZED FOR {} TO '{}'",
                                  device_name, mode));
      } else {
        logger.LOG(1, std::format("[-] ERROR TO OPEN FILE: {} FOR WRITING",
                                  schedPath.string()));
      }
    }
  }

  inline int getMngCount() const { return mngCount; }
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    helpMsg();
    std::cout << "\n";
    return 0;
  }

  std::string_view firstArg = argv[1];
  if (firstArg == "--help" || firstArg == "-h") {
    helpMsg();
    std::cout << "\n";
    return 0;
  }

  if (getuid() != 0) {
    std::cout << "\033[31m[Error: Root privileges required]\033[0m\n";
    return 1;
  }

  MainManager manager;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h") {
      helpMsg();
      return 0;
    } else if (arg == "--clean-system") {
      logger.LOG(0, "[+] SYSTEM - CLEANING PACKAGES...");
      manager.cleanSystem();
      logger.LOG(
          0, std::format("[+] CLEANED MANAGERS: {}", manager.getMngCount()));
    } else if (arg == "--disable-services") {
      std::cout
          << "[!] WARNING: BLUETOOTH AND SYSTEM SERVICES WILL BE DISABLED.\n"
          << "[!] DO YOU WANT TO PROCEED? (y/n): ";
      char tmp = 'n';
      std::cin >> tmp;
      if (std::cin.fail()) {
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Invalid input. Exiting.\n";
        return 0;
      }
      logger.LOG(0, "[+] DISABLING UNNECESSARY SERVICES...");
      if (!manager.disableServices(tmp)) {
        logger.LOG(0, "[!] NO SERVICES WERE DISABLED");
      }
    } else if (arg == "--game-mode") {
      logger.LOG(0, "[!] ENABLING GAME MODE...");
      manager.gamingMode();
      logger.LOG(0, "[+] GAMING MODE ACTIVATED SUCCESSFULLY!");
    } else if (arg == "--disk-optimization") {
      manager.schedulerSetup();
    } else {
      helpMsg();
      return 1;
    }
  }
  return 0;
}
