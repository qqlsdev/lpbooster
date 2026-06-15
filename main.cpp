#include "logger/logger.hpp"
#include "utils/macros.hpp"
#include "utils/restore.hpp"
#include "utils/tools.hpp"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

class SystemOptimizer {
private:
  Logger &logger;
  FileAssist &file_assist;

  std::string trim(const std::string &str) {
    const size_t first = str.find_first_not_of(" \t");
    if (std::string::npos == first)
      return "";
    const size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
  }

  void appendSysctl(const std::string &setting) {
    std::string currContent;

    if (!fs::exists(Config::SYSCTL_CONF_PATH)) {
      if (!file_assist.createFile(Config::SYSCTL_CONF_PATH).is_ok()) {
        logger.LOG(1, std::format("[!] FAILED TO CREATE: {}",
                                  Config::SYSCTL_CONF_PATH));
      }
      logger.LOG(0, std::format("[+] FILE: {} CREATED SUCCESS!",
                                Config::SYSCTL_CONF_PATH));
    }

    if (!file_assist.readFile(Config::SYSCTL_CONF_PATH, currContent).is_ok()) {
      logger.LOG(2, std::format("[-] FAILED TO READ FILE: {}",
                                Config::SYSCTL_CONF_PATH));
      return;
    }

    auto equalPos = setting.find('=');
    if (equalPos == std::string::npos)
      return;
    std::string targetKey = trim(setting.substr(0, equalPos));

    std::istringstream stream(currContent);
    std::string line;
    bool exists = false;

    while (std::getline(stream, line)) {
      line = trim(line);

      if (line.empty() || line[0] == '#' || line[0] == ';') {
        continue;
      }

      auto lineEqualPos = line.find('=');
      if (lineEqualPos != std::string::npos) {
        std::string currentKey = trim(line.substr(0, lineEqualPos));

        if (currentKey == targetKey) {
          exists = true;
          break;
        }
      }
    }

    if (exists) {
      return;
    }

    if (!file_assist.writeFile(Config::SYSCTL_CONF_PATH, setting).is_ok()) {
      return;
    }
  }

public:
  explicit SystemOptimizer(Logger &log, FileAssist &fileassist)
      : logger(log), file_assist(fileassist) {}

  void cleanSystem() {
    int cleanedManagers = 0;
    auto tryClean = [&](const std::string &binary, const std::string &cmd,
                        const std::string &msg) {
      std::string binaryPath =
          SystemUtils::getCmdOutput("command -v " + binary);

      if (!binaryPath.empty()) {
        logger.LOG(0, msg);
        if (SystemUtils::runCommand(cmd))
          cleanedManagers++;
        else
          logger.LOG(1, "[!] FAILED TO RUN CLEANING COMMAND");
      }
    };

    tryClean("pacman", "pacman -Qdtq | pacman -Rns - --noconfirm",
             "[+] PACMAN: CLEANING ORPHANS...");
    tryClean("apt-get",
             "apt-get autoremove -y > /dev/null 2>&1 && apt-get clean",
             "[+] APT: CLEANING DEPENDENCIES...");

    tryClean("dnf", "dnf autoremove -y", "[+] DNF: CLEANING UP...");
    tryClean("flatpak", "flatpak uninstall --unused -y",
             "[+] FLATPAK: REMOVING UNUSED RUNTIMES...");
    tryClean("fc-cache", "fc-cache -r", "[+] REBUILDING FONT CACHE...");

    if (const char *user = std::getenv("SUDO_USER")) {
      fs::path cacheDir = fs::path("/home") / user / ".cache";
      for (const auto &c : {"thumbnails", "fontconfig", "pip"}) {
        fs::path pCache = cacheDir / c;
        if (fs::exists(pCache)) {
          std::string cmd =
              std::format("sudo -u {} rm -rf {}", user, pCache.c_str());
          SystemUtils::runCommand(cmd);
          logger.LOG(0, std::format("[+] CLEANED: {}", pCache.string()));
        }
      }
    }

    if (SystemUtils::runCommand("journalctl --vacuum-size=50M")) {
      logger.LOG(0, "[+] SYSTEM JOURNAL CLEANED UP!");
    }
    logger.LOG(0,
               std::format("[+] TOTAL CLEANED MODULES: {}", cleanedManagers));
  }

  void optimizeServices(char skipBluetooth) {
    std::vector<std::string_view> toDisable;
    for (const auto &srv : Config::SERVICES_TO_DISABLE) {
      if (srv == "bluetooth.service" &&
          (skipBluetooth == 'y' || skipBluetooth == 'Y')) {
        continue;
      }
      toDisable.push_back(srv);
    }

    bool hdd, ssd;
    SystemUtils::detectDrives(hdd, ssd);
    if (hdd) {
      toDisable.push_back("tracker-miner-fs-3.service");
      if (!ssd)
        toDisable.push_back("fstrim.timer");
    }

    if (toDisable.empty()) {
      logger.LOG(1, "[!] NO SERVICES TO DISABLE");
      return;
    }

    std::string cmd = "systemctl disable --now";
    bool isDisable = false;

    for (const auto &srv : toDisable) {
      std::string fullCmd = cmd + std::format(" {}", srv);
      if (!SystemUtils::runCommand(fullCmd)) {
        logger.LOG(1, std::format("[!] FAILED TO DISABLE SERVICE: {}", srv));
      } else {
        logger.LOG(0, std::format("[+] SERVICE: {} DISABLED!", srv));
        isDisable = true;
      }
    }
    if (isDisable)
      logger.LOG(0, "[+] UNNECESSARY SERVICES DISABLED!");
    else
      logger.LOG(1, "[!] NO SERVICES TO DISABLE!");
  }

  bool applyTweaks() {

    for (auto const &tweak : Config::VM_TWEAKS)
      appendSysctl(tweak);

    bool hdd, ssd;
    SystemUtils::detectDrives(hdd, ssd);
    if (ssd || SystemUtils::getTotalRAM() >= 8.0) {
      appendSysctl("vm.dirty_background_ratio=5");
      appendSysctl("vm.dirty_ratio=10");
    }

    if (system("sysctl -p /etc/sysctl.d/99-sysctl.conf > /dev/null 2>&1")) {
      return true;
    } else {
      logger.LOG(1,
                 "[-] FAILED TO APPLY KERNEL CONFIG, PLEASE REBOOT MANUALY.");
    }

    return false;
  }

  bool cpuPerformance() {
    int fd_dir = open(Config::CPU_PATH.c_str(),
                      O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
    if (fd_dir < 0) {
      if (errno == ENOENT) {
        logger.LOG(
            2, std::format("[-] FILE: {} DOES NOT EXIST!", Config::CPU_PATH));
        return false;
      } else {
        logger.LOG(2, std::format("[-] FAIL TO OPEN DIRECTORY {}: {}",
                                  Config::CPU_PATH, strerror(errno)));
        return false;
      }
      return true;
    }

    DIR *dir = fdopendir(fd_dir);

    if (!dir) {
      logger.LOG(2, std::format("[-] CAN'T OPEN DIRECTORY STREAM FOR {}: {}",
                                Config::CPU_PATH, strerror(errno)));
      close(fd_dir);
    } else {
      dirent *entry;
      while ((entry = readdir(dir)) != nullptr) {
        std::string dName = entry->d_name;

        if (dName.starts_with("cpu") && dName.size() > 3 &&
            std::isdigit(dName[3])) {
          const std::string govPath =
              Config::CPU_PATH + dName + "/cpufreq/scaling_governor";

          if (!fs::exists(govPath)) {
            logger.LOG(
                3, std::format("[.] GOVERNOR FILE NOT FOUND FOR: {}", govPath));
            continue;
          }

          int fd_gov = open(govPath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
          if (fd_gov < 0) {
            logger.LOG(2, std::format("[-] FAIL TO OPEN {} FOR READING: {}",
                                      govPath, strerror(errno)));
            continue;
          }

          std::vector<char> buffer(64);
          ssize_t bytesRead = read(fd_gov, buffer.data(), buffer.size() - 1);
          if (bytesRead < 0) {
            logger.LOG(2, std::format("[-] FAIL TO READ FROM {}: {}", govPath,
                                      strerror(errno)));
            close(fd_gov);
            continue;
          }

          buffer[bytesRead] = '\0';
          std::string currGov = buffer.data();
          size_t lastCharPos = currGov.find_last_not_of(" \n\r\t");
          if (lastCharPos != std::string::npos)
            currGov.erase(lastCharPos + 1);
          else
            currGov.clear();
          close(fd_gov);

          if (currGov == Config::POWERSAVE) {
            if (!file_assist.writeFile(govPath, Config::PERFORMANCE).is_ok()) {
              logger.LOG(2, std::format("[-] FAILED TO WRITE TO: {}", govPath));
            }
          } else {
            logger.LOG(
                0, std::format("[.] CPU: {} IS ALREADY {}", govPath, currGov));
          }
        }
      }
      closedir(dir);
    }

    return true;
  }

  bool splitLock() {
    fs::path splPath = Config::SPLIT_LOCK_PATH;
    if (!fs::exists(splPath)) {
      logger.LOG(3, std::format("[.] SPLIT LOCK MITIGATION FILE NOT FOUND: {}",
                                splPath.string()));
    } else {
      int fd_spl = open(splPath.c_str(), O_RDONLY | O_CLOEXEC);
      if (fd_spl < 0) {
        logger.LOG(2, std::format("[-] FAIL TO OPEN {} FOR READING: {}",
                                  splPath.string(), strerror(errno)));
      } else {
        std::vector<char> splBuffer(8);
        ssize_t splBytesRead =
            read(fd_spl, splBuffer.data(), splBuffer.size() - 1);
        close(fd_spl);

        if (splBytesRead < 0) {
          logger.LOG(2, std::format("[-] FAIL TO READ FROM {}: {}",
                                    splPath.string(), strerror(errno)));
        } else if (splBytesRead > 0) {

          splBuffer[splBytesRead] = '\0';

          std::string currModeStr = splBuffer.data();
          textTrimmer(currModeStr);

          if (currModeStr != "0") {
            int fd_spl_write = open(splPath.c_str(), O_WRONLY | O_CLOEXEC);
            if (fd_spl_write < 0) {
              logger.LOG(2, std::format("[-] FAIL TO OPEN {} FOR WRITING: {}",
                                        splPath.string(), strerror(errno)));
            } else {
              const std::string disableMode = "0\n";
              ssize_t splBytesWritten = write(fd_spl_write, disableMode.c_str(),
                                              disableMode.length());
              if (splBytesWritten < 0 ||
                  (size_t)splBytesWritten != disableMode.length()) {
                logger.LOG(2, std::format("[-] FAIL TO WRITE TO {}: {}",
                                          splPath.string(), strerror(errno)));
              } else {
                logger.LOG(0, "[+] SPLIT LOCK MITIGATION DISABLED.");
              }
              close(fd_spl_write);
            }
          } else {
            logger.LOG(3, std::format("[.] SPLIT LOCK: {} IS ALREADY DISABLED.",
                                      splPath.string()));
          }
        }
      }
    }
    return true;
  }

  std::optional<bool> gpuBoost() {

    auto drmName = getDRM();

    if (!drmName.empty()) {
      const std::string fullPath = Config::DRM_PATH + drmName +
                                   "/device/power_dpm_force_performance_level";

      std::string readMode;

      if (!file_assist.readFile(fullPath, readMode).is_ok()) {
        logger.LOG(2, std::format("[-] FAIL TO READ FROM {}: {}", fullPath,
                                  strerror(errno)));
        return false;
      }

      if (!readMode.empty() && readMode.back() == '\n')
        readMode.pop_back();

      if (readMode == Config::HIGH) {
        logger.LOG(
            3, std::format("[.] GPU {} IS ALREADY HIGH PERFORMANCE", fullPath));
        return std::nullopt;
      }

      if (readMode == Config::AUTO || readMode == Config::LOW) {
        const std::string highMode = "high";
        if (!file_assist.writeFile(fullPath, highMode).is_ok()) {
          logger.LOG(2, std::format("[-] FAIL TO WRITE TO {}: {}", fullPath,
                                    strerror(errno)));
        }
        logger.LOG(0,
                   std::format("[+] GPU {} SET TO HIGH PERFORMANCE!", drmName));
      } else {
        logger.LOG(1, std::format("[!] GPU {} UNKNOWN MODE: {}, SKIPPING",
                                  drmName, readMode));
      }
    }

    return true;
  }

  bool gamingServices() {
    for (const std::string &srv : {"ananicy.service", "gamemoded.service"}) {
      if (SystemUtils::runCommand("systemctl list-unit-files " + srv +
                                  " | grep -q " + srv)) {
        SystemUtils::runCommand("systemctl enable --now " + srv);
        logger.LOG(0, "[+] GAME SERVICE ENABLED: " + srv);
      } else {
        logger.LOG(
            1, std::format(
                   "[!] GAME SERVICE: {} NOT ACTIVE OR NOT FOUND, SKIPPING.",
                   srv));
      }
    }
    return true;
  }

  bool createCpuService() {
    if (!fs::exists(Config::CPU_SERVICE_PATH)) {
      int fd_service =
          open(Config::CPU_SERVICE_PATH.c_str(),
               O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
      if (fd_service < 0) {
        logger.LOG(2, std::format("[-] FAILED TO CREATE SERVICE FILE {}: {}",
                                  Config::CPU_SERVICE_PATH, strerror(errno)));
      } else {
        const std::string service_content =
            "[Unit]\nDescription=Linux Performance "
            "Booster\nAfter=multi-user.target\n\n"
            "[Service]\nType=oneshot\nRemainAfterExit=yes\n"
            "ExecStart=/bin/sh -c 'echo performance | tee "
            "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'\n\n"
            "[Install]\nWantedBy=multi-user.target\n";

        ssize_t bytesWritten = write(fd_service, service_content.c_str(),
                                     service_content.length());
        if (bytesWritten < 0) {
          logger.LOG(2,
                     std::format("[-] FAILED TO WRITE TO SERVICE FILE {}: {}",
                                 Config::CPU_SERVICE_PATH, strerror(errno)));
        } else if ((size_t)bytesWritten != service_content.length()) {
          logger.LOG(2, std::format("[-] INCOMPLETE WRITE TO SERVICE FILE {}: "
                                    "Expected {} bytes, wrote {}",
                                    Config::CPU_SERVICE_PATH,
                                    service_content.length(), bytesWritten));
        } else {
          logger.LOG(0, std::format("[+] SERVICE FILE CREATED: {}",
                                    Config::CPU_SERVICE_PATH));
          SystemUtils::runCommand("systemctl daemon-reload && systemctl enable "
                                  "lpbooster-cpu.service");
          logger.LOG(0, "[+] SERVICE ENABLED: lpbooster-cpu.service");
        }
        close(fd_service);
      }
    } else {
      logger.LOG(3, std::format("[.] SERVICE FILE: {} ALREADY EXIST",
                                Config::CPU_SERVICE_PATH));
    }
    return true;
  }

  void enableGamingMode() {

    logger.LOG(0, "[+] PREPAIRING GAME MODE...");

    if (!cpuPerformance()) {
      logger.LOG(1, "[-] FAILED TO ACTIVATE CPU PERFORMANCE!");
    }

    if (!applyTweaks()) {
      logger.LOG(1, "[-] FAILED TO APPLY TWEAKS!");
    }

    if (gpuBoost() != std::nullopt && gpuBoost()) {
      logger.LOG(1, "[-] FAILED TO BOOST GPU!");
    }

    if (!createCpuService() || !gamingServices() || !splitLock()) {
      logger.LOG(1, "[-] FAILED TO INITIALIZE CORE GAMING SERVICES!");
    }

    logger.LOG(0, "[+] GAME MODE TWEAKS APPLIED SUCCESSFULLY!");
  }

  void optimizeDiskScheduler() {
    int fd_block =
        open(Config::BLOCK_PATH.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (fd_block < 0) {
      if (errno == ENOENT) {
        logger.LOG(
            2, std::format("[-] FILE: {} DOES NOT EXIST!", Config::BLOCK_PATH));
      }
      return;
    }

    DIR *dir = fdopendir(fd_block);

    if (!dir) {
      close(fd_block);
      return;
    }

    dirent *entry;

    while ((entry = readdir(dir)) != nullptr) {

      std::string dName = entry->d_name;

      if (dName.starts_with(".") || dName.starts_with(".."))
        continue;

      bool isVirtual = std::any_of(
          Config::VIRTUAL_DISKS.begin(), Config::VIRTUAL_DISKS.end(),
          [&](std::string_view vtd) { return dName.starts_with(vtd); });

      if (isVirtual)
        continue;

      const std::string schedPath =
          Config::BLOCK_PATH + dName + "/queue/scheduler";

      auto rotational =
          SystemUtils::isDriveRotational(Config::BLOCK_PATH + dName);

      if (!rotational.has_value()) {
        logger.LOG(1, "[-] CAN'T DETERMINE DRIVE TYPE, SKIPPING");
        continue;
      }

      std::string currMode;
      std::string targetMode = rotational.value() ? Config::BFQ : Config::NONE;

      if (!file_assist.readFile(schedPath, currMode).is_ok()) {
        continue;
      }

      currMode = extractActiveScheduler(currMode);

      if (currMode.empty()) {
        logger.LOG(
            2, std::format("[-] CURRENT MODE FOR: {} IS EMPTY!", schedPath));
        continue;
      }

      if (currMode == targetMode) {
        logger.LOG(
            0, std::format("[!] MODE IS ALREADY: {} SKIPPING.", targetMode));
        continue;
      }

      if (file_assist.writeFile(schedPath, targetMode, 0).is_ok()) {
        logger.LOG(0, std::format("[+] I/O SCHEDULER FOR: {} SET TO: {}",
                                  schedPath, targetMode));
      } else {
        continue;
      }
    }
    closedir(dir);
    return;
  }
};

namespace {

void printHelp() {
  std::cout
      << Colors::BOLD << "Usage:" << Colors::RESET << " lpboost "
      << Colors::CYAN << "[OPTIONS]" << Colors::RESET << "\n\n"
      << "Options:\n"
      << "  " << Colors::GREEN << "-h, --help" << Colors::RESET
      << "\t\tShow this message\n"
      << "  " << Colors::GREEN << "--clean-system" << Colors::RESET
      << "\tClean garbage, caches, and logs\n"
      << "  " << Colors::GREEN << "--disable-services" << Colors::RESET
      << "\tDisable heavy systemd services\n"
      << "  " << Colors::GREEN << "--game-mode" << Colors::RESET
      << "\t\tApply gaming tweaks (CPU/Sysctl)\n"
      << "  " << Colors::GREEN << "--disk-optimization" << Colors::RESET
      << "\tOptimize I/O scheduler\n"
      << "  " << Colors::GREEN << "--restore" << Colors::RESET
      << "\t\tRestore all settings from backup\n\n"
      << Colors::YELLOW << Colors::BOLD << "Warning:" << Colors::RESET
      << " this tool modifies system-level settings and requires root.\n"
      << "Use at your own risk. Always keep backups before applying changes.\n";
}

bool requireRoot() {
  if (getuid() != 0) {
    std::cerr << Colors::RED << "Error: Root privileges required (sudo)"
              << Colors::RESET << "\n";
    return false;
  }
  return true;
}

void printDisclaimer() {
  std::cout << Colors::YELLOW << Colors::BOLD
            << "==================== DISCLAIMER ====================\n"
            << Colors::RESET << "This program runs with " << Colors::RED
            << "root privileges" << Colors::RESET
            << " and modifies low-level system settings:\n"
            << "  - CPU governors and frequency scaling\n"
            << "  - GPU power management (DPM)\n"
            << "  - I/O schedulers for block devices\n"
            << "  - Kernel sysctl parameters\n"
            << "  - Systemd services\n\n"
            << "Possible consequences include:\n"
            << "  - Reduced system stability or unexpected shutdowns\n"
            << "  - Increased power consumption / heat / noise\n"
            << "  - Disabled services may break functionality you rely on\n"
            << "  - Backups are best-effort and may not cover every setting\n\n"
            << Colors::BOLD
            << "Use this tool at your own risk. The authors are not\n"
            << "responsible for data loss, hardware damage, or system\n"
            << "instability resulting from its use.\n"
            << Colors::RESET
            << "=====================================================\n\n";
}

void logBackupResult(Logger &logger, BackupManager &backup) {
  if (backup.createBackup()) {
    logger.LOG(0, "[+] BACKUP CREATED!");
  } else {
    logger.LOG(1, "[!] BACKUP FAILED - PROCEEDING WITHOUT BACKUP. "
                  "RESTORE MAY NOT BE POSSIBLE!");
  }
}

bool promptYesNo(const std::string &question) {
  std::cout << question << " (y/n): ";
  char choice = 'n';
  std::cin >> choice;
  return choice == 'y' || choice == 'Y';
}

bool handleArg(std::string_view arg, Logger &logger, FileAssist &file_assist,
               SystemOptimizer &optimizer, BackupManager &backup,
               int &exitCode) {
  if (arg == "--help" || arg == "-h") {
    printHelp();
    exitCode = 0;
    return false;
  }

  if (arg == "--clean-system") {
    if (!promptYesNo(
            "[!] This will permanently delete cache/log files. Continue?")) {
      logger.LOG(0, "[.] CLEAN SYSTEM CANCELLED BY USER");
      return true;
    }
    optimizer.cleanSystem();
    return true;
  }

  if (arg == "--disable-services") {
    std::cout << Colors::YELLOW
              << "[!] Disabling services may break functionality "
                 "(printing, bluetooth, etc.) depending on your setup.\n"
              << Colors::RESET;
    char choice = promptYesNo("[!] Disable system services?") ? 'y' : 'n';
    optimizer.optimizeServices(choice);
    return true;
  }

  if (arg == "--game-mode") {
    std::cout << Colors::YELLOW
              << "[!] Game mode will change CPU governor, sysctl values, "
                 "and GPU power profile.\n"
              << "    A backup of current settings will be created so "
                 "you can revert with --restore.\n"
              << Colors::RESET;
    logBackupResult(logger, backup);
    optimizer.enableGamingMode();
    return true;
  }

  if (arg == "--disk-optimization") {
    std::cout << Colors::YELLOW
              << "[!] This will change the I/O scheduler for detected "
                 "block devices.\n"
              << "    Virtual disks are skipped automatically.\n"
              << Colors::RESET;
    logBackupResult(logger, backup);
    optimizer.optimizeDiskScheduler();
    return true;
  }

  if (arg == "--restore") {
    if (backup.restoreAll()) {
      logger.LOG(0, "[+] SETTINGS RESTORED DONE!");
    } else {
      logger.LOG(1, "[!] RESTORE COMPLETED WITH WARNINGS - CHECK LOG ABOVE");
    }
    return true;
  }

  std::cerr << "Unknown option: " << arg << "\n";
  printHelp();
  exitCode = 1;
  return false;
}

} // namespace

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printHelp();
    return 0;
  }

  if (!requireRoot()) {
    return 1;
  }

  printDisclaimer();

  if (!promptYesNo("Do you understand the risks and want to continue?")) {
    std::cout << "[.] ABORTED BY USER\n";
    return 0;
  }

  Logger logger;
  FileAssist file_assist(logger);
  FileStatus file_status(0, false);
  SystemOptimizer optimizer(logger, file_assist);
  BackupManager backup(logger, file_status);

  for (int i = 1; i < argc; ++i) {
    int exitCode = 0;
    if (!handleArg(argv[i], logger, file_assist, optimizer, backup, exitCode)) {
      return exitCode;
    }
  }

  return 0;
}
