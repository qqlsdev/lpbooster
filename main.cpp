#include "Logger.h"
#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

Logger logger;

namespace fs = std::filesystem;
const std::vector<std::string_view> vtDisks = {"loop", "ram", "dm", "nbd",
                                               "md"};
const std::string services[] = {
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

void showHelp() {
  std::cout << "usage: lpboost -h | --help\n"
            << "--clean-pkg             cleaning unused packages\n"
            << "--disable-services       disable unneccessary services\n"
            << "--monitoring             monitoring system resources";
}

class MainManager {
private:
  int srv_count = 0;
  int mng_count = 0;
  const char *blockPath = "/sys/block/";

public:
  double getFreeRAM() {
    int pageSize = sysconf(_SC_PAGESIZE);
    long long freePages = sysconf(_SC_AVPHYS_PAGES);
    long long freeBytes = freePages * pageSize;
    return static_cast<double>(freeBytes) / (1024 * 1024 * 1024);
  }

  double getTotalRAM() {
    int pageSize = sysconf(_SC_PAGESIZE);
    long long totalPages = sysconf(_SC_PHYS_PAGES);
    long long totalBytes = totalPages * pageSize;
    return static_cast<double>(totalBytes) / (1024 * 1024 * 1024);
  }

  bool hasRotationalDisk() {
    char isRotational;
    bool hasSSD = false;
    bool hasHDD = false;

    if (!fs::exists(blockPath)) {
      logger.LOG(2, std::format("File: {} doesn't exist", blockPath));
      logger.LOG(0, "Application is closed");
      return false;
    }
    logger.LOG(0, "Looking for devices..");
    for (auto const &entry : fs::directory_iterator(blockPath)) {
      std::string device_name = entry.path().filename().string();
      bool isVirtual = std::any_of(
          vtDisks.begin(), vtDisks.end(),
          [&](std::string_view vtd) { return device_name.starts_with(vtd); });
      if (isVirtual) {
        logger.LOG(0, "Skipping virtual device.");
        continue;
      }
      fs::path rotationalPath = entry.path() / "queue" / "rotational";
      std::ifstream file(rotationalPath);
      if (file.is_open()) {
        if (file >> isRotational) {
          if (isRotational == '0') {
            hasSSD = true;
          } else if (isRotational == '1') {
            hasHDD = true;
          }
        }
      } else {
        logger.LOG(
            1, std::format("File: {} didn't open!", rotationalPath.string()));
      }
    }

    if (hasSSD) {
      return false;
    }

    if (hasHDD) {
      return true;
    }

    return false;
  }

  bool disableServices() {
    srv_count = 0;
    std::vector<std::string> exist_services;

    for (auto const &service : services) {
      std::stringstream cmdCheck;
      cmdCheck << "systemctl list-unit-files " << service
               << " > /dev/null 2>&1";
      std::string fullCmd = cmdCheck.str();
      if (std::system(fullCmd.c_str()) == 0) {
        exist_services.push_back(service);
      } else {
        logger.LOG(
            1, std::format("Service {} does not exist, skipping.", service));
      }
    }
    std::stringstream ss;
    if (!exist_services.empty()) {
      ss << "systemctl disable --now ";
      for (size_t j = 0; j < exist_services.size(); ++j) {
        ss << exist_services[j];
        if (j != exist_services.size() - 1) {
          ss << " ";
        }
      }
      std::string full_cmd = ss.str();
      system(full_cmd.c_str());
      srv_count = exist_services.size();
      if (hasRotationalDisk()) {
        if (std::system(
                "systemctl list-unit-files fstrim.timer > /dev/null 2>&1") ==
            0) {
          std::system("systemctl disable --now fstrim.timer > /dev/null 2>&1");
          ++srv_count;
        }
        if (std::system("systemctl list-unit-files tracker-miner-fs-3.service "
                        "> /dev/null 2>&1") == 0) {
          std::system("systemctl disable --now tracker-miner-fs-3.service > "
                      "/dev/null 2>&1");
          ++srv_count;
        }
      }
    } else {
      logger.LOG(2, "No matching services.");
      return false;
    }
    return true;
  }
  void removePackages() {
    mng_count = 0;
    logger.LOG(0, "Starting package system cleanup...");

    if (fs::exists("/usr/bin/pacman")) {
      logger.LOG(0, "Pacman detected. Cleaning orphans...");
      if (std::system("pacman -Qdtq > /dev/null 2>&1") == 0) {
        std::system("pacman -Rsu $(pacman -Qdtq) --noconfirm > /dev/null 2>&1");
      }
      mng_count++;
    } else {
      logger.LOG(1, "Pacman is not found, skipping.");
    }

    if (fs::exists("/usr/bin/apt-get")) {
      logger.LOG(0, "APT detected. Removing unused dependencies...");
      std::system("apt-get autoremove -y > /dev/null 2>&1");
      std::system("apt-get clean > /dev/null 2>&1");
      mng_count++;
    } else {
      logger.LOG(1, "APT is not found, skipping.");
    }

    if (fs::exists("/usr/bin/dnf")) {
      logger.LOG(0, "DNF detected. Cleaning up...");
      std::system("dnf autoremove -y > /dev/null 2>&1");
      mng_count++;
    } else {
      logger.LOG(1, "DNF is not found, skipping.");
    }

    if (fs::exists("/usr/bin/flatpak")) {
      logger.LOG(0, "Flatpak detected. Removing unused runtimes...");
      std::system("flatpak uninstall --unused -y > /dev/null 2>&1");
      mng_count++;
    } else {
      logger.LOG(1, "Flatpak is not found, skipping.");
    }

    if (mng_count > 0) {
      logger.LOG(0, "Package cleanup completed successfully!");
    } else {
      logger.LOG(1, "No supported package managers found.");
    }
  }

  int getServCount() const { return srv_count; }
  int getMngCount() const { return mng_count; }
};

int main(int argc, char *argv[]) {
  if (getuid() != 0) {
    std::cout << "[!] Please run as root";
    return 1;
  }

  char a;
  std::cout << "Do you want to save logs? (y/n)\n";
  std::cin >> a;
  if (a == 'y' || a == 'Y') {
    std::cout << "[Logs] Path: ";
    std::string path;
    std::cin >> path;
    if (!fs::exists(path)) {
      logger.LOG(1, "Path not exist, skipping.");
    } else {
      logger.SaveLogs(path);
    }
  }

  MainManager manager;

  if (argc < 2) {
    showHelp();
    return 0;
  }

  for (int i = 1; i < argc; ++i) {
    std::string_view arg = argv[i];

    if (arg == "--clean-pkg") {
      std::cout << "[System] - Cleaning packages.." << "\n";
      manager.removePackages();
      logger.LOG(0, std::format("Removed packages: {}", manager.getMngCount()));
    } else if (arg == "--disable-services") {
      logger.LOG(0, "Disabling unnecessary services..");
      if (!manager.disableServices()) {
        logger.LOG(0, "No services for disable");
      } else {
        logger.LOG(0, "Done! Good luck <3");
      }
      logger.LOG(0,
                 std::format("Disabled services: {}", manager.getServCount()));
    } else if (arg == "--monitoring") {
      while (true) {
        std::string fRam =
            std::format("[FREE: {:.2f}GB]", manager.getFreeRAM());
        std::cout << "\r" << fRam << std::flush;
        std::this_thread::sleep_for(milliseconds(300));
      }
    }
  }
  return 0;
}
