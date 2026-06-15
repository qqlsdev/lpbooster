#pragma once

#include "../logger/logger.hpp"
#include "macros.hpp"
#include "tools.hpp"
#include <INIReader.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <functional>
#include <unistd.h>
#include <vector>

using namespace Config;
namespace fs = std::filesystem;

class BackupManager {
private:
  Logger &logger;
  FileStatus &fStatus;

  const fs::path PARENT_DIR = fs::path(BACKUP_PATH).parent_path();

  static std::string trimTrailingNewline(std::string s) {
    if (!s.empty() && s.back() == '\n')
      s.pop_back();
    return s;
  }

  std::vector<std::string> listMatchingEntries(
      const std::string &dirPath,
      const std::function<bool(const std::string &)> &predicate) const {
    std::vector<std::string> result;
    DIR *dir = opendir(dirPath.c_str());
    if (!dir)
      return result;

    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string dName = entry->d_name;
      if (dName == "." || dName == "..")
        continue;
      if (predicate(dName))
        result.push_back(dName);
    }
    closedir(dir);
    return result;
  }

  static bool isCpuDir(const std::string &name) {
    return name.starts_with("cpu") && name.size() > 3 &&
           std::isdigit(static_cast<unsigned char>(name[3]));
  }

  static bool isCardDir(const std::string &name) {
    return name.find("card") != std::string::npos && name.size() > 4 &&
           std::isdigit(static_cast<unsigned char>(name[4])) &&
           name.find('-') == std::string::npos;
  }

  bool isVirtualDisk(const std::string &name) const {
    return std::any_of(
        VIRTUAL_DISKS.begin(), VIRTUAL_DISKS.end(),
        [&](std::string_view vtd) { return name.starts_with(vtd); });
  }

  std::vector<std::string> collectCpuGovernorPaths() const {
    std::vector<std::string> paths;
    for (auto &d : listMatchingEntries(CPU_PATH, isCpuDir))
      paths.push_back(CPU_PATH + d + "/cpufreq/scaling_governor");
    return paths;
  }

  std::vector<std::string> collectDrmGovernorPaths() const {
    std::vector<std::string> paths;
    for (auto &d : listMatchingEntries(DRM_PATH, isCardDir))
      paths.push_back(DRM_PATH + d +
                      "/device/power_dpm_force_performance_level");
    return paths;
  }

  std::vector<std::string> collectBlockSchedulerPaths() const {
    std::vector<std::string> paths;
    auto entries = listMatchingEntries(
        BLOCK_PATH, [](const std::string &) { return true; });
    for (auto &d : entries) {
      if (isVirtualDisk(d))
        continue;
      paths.push_back(BLOCK_PATH + d + "/queue/scheduler");
    }
    return paths;
  }

  std::vector<std::string> collectAllPaths() const {
    std::vector<std::string> paths;

    if (fs::exists(SYSCTL_CONF_PATH))
      paths.push_back(SYSCTL_CONF_PATH);

    if (fs::exists(SPLIT_LOCK_PATH))
      paths.push_back(SPLIT_LOCK_PATH);

    auto append = [&](std::vector<std::string> &&extra) {
      paths.insert(paths.end(), extra.begin(), extra.end());
    };

    append(collectCpuGovernorPaths());
    append(collectDrmGovernorPaths());
    append(collectBlockSchedulerPaths());

    return paths;
  }

  void backupSysctlIfNeeded(FileAssist &file_assist) const {
    if (fs::exists(SYSCTL_CONF_PATH_BAK))
      return;

    if (!file_assist.createFile(SYSCTL_CONF_PATH_BAK).is_ok())
      return;

    std::string tmpStr;
    if (file_assist.readFile(SYSCTL_CONF_PATH, tmpStr).is_ok()) {
      file_assist.writeFile(SYSCTL_CONF_PATH_BAK, tmpStr).is_ok();
    }
  }

  std::string buildBackupContent(const std::vector<std::string> &paths,
                                 FileAssist &file_assist) {
    std::string reader;

    for (auto const &path : paths) {
      if (path == SYSCTL_CONF_PATH) {
        backupSysctlIfNeeded(file_assist);
        continue;
      }

      std::string value;
      if (!file_assist.readFile(path, value).seek(0).is_ok()) {
        logger.LOG(1, std::format("[!] BACKUP: FAIL TO READ FILE: {}", path));
        continue;
      }

      reader += path + "=" + value;
    }

    return reader;
  }

  bool writeBackupFile(const std::string &content) {
    int fd =
        open(BACKUP_PATH.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_TRUNC);

    if (fd < 0) {
      logger.LOG(
          2, std::format("[-] BACKUP: FAIL TO OPEN BACKUP FILE: {}, ERROR: {}",
                         BACKUP_PATH, strerror(errno)));
      return false;
    }

    size_t totalWritten = 0;
    const char *ptr = content.data();
    size_t len = content.length();

    while (totalWritten < len) {
      ssize_t written = write(fd, ptr + totalWritten, len - totalWritten);

      if (written < 0) {
        if (errno == EINTR)
          continue;
        logger.LOG(2, std::format("[-] BACKUP: FAIL TO WRITE DATA, ERROR: {}",
                                  strerror(errno)));
        close(fd);
        return false;
      }

      totalWritten += written;
    }

    if (fsync(fd) < 0) {
      logger.LOG(2,
                 std::format("[-] BACKUP: FAIL TO SYNC DATA TO DISK, ERROR: {}",
                             strerror(errno)));
      close(fd);
      return false;
    }

    if (close(fd) < 0) {
      logger.LOG(
          2, std::format("[-] BACKUP: FAIL TO CLOSE FILE DESCRIPTOR, ERROR: {}",
                         strerror(errno)));
      return false;
    }

    return true;
  }

public:
  explicit BackupManager(Logger &_logger, FileStatus &_fStatus)
      : logger(_logger), fStatus(_fStatus) {
    FileAssist file_assist(logger);

    if (!fs::exists(PARENT_DIR)) {
      try {
        fs::create_directories(PARENT_DIR);
      } catch (const fs::filesystem_error &e) {
        logger.LOG(
            2, std::format("[-] FAIL TO CREATE FILE: {}", PARENT_DIR.string()));
        return;
      }
    }

    if (!file_assist.createFile(BACKUP_PATH).is_ok()) {
      logger.LOG(2, std::format("[-] FAILED TO CREATE BACKUP DIR: {}",
                                strerror(errno)));
      return;
    }
  }

  inline bool createBackup() {
    FileAssist file_assist(logger);

    const auto paths = collectAllPaths();
    const std::string content = buildBackupContent(paths, file_assist);

    if (content.empty()) {
      logger.LOG(1, "[!] BACKUP: NO DATA COLLECTED TO WRITE");
      return false;
    }

    return writeBackupFile(content);
  }

  inline bool restoreSysctl() {
    FileAssist file_assist(logger);
    std::string savedBak;

    if (!fs::exists(SYSCTL_CONF_PATH_BAK) &&
        !file_assist.readFile(SYSCTL_CONF_PATH_BAK, savedBak).is_ok()) {
      return false;
    }

    if (!file_assist.writeFile(SYSCTL_CONF_PATH, savedBak, 0).is_ok()) {
      logger.LOG(2, "[-] FAILED TO RESTORE TWEAKS");
      return false;
    }

    logger.LOG(0, "[+] SYSCTL TWEAKS RESTORED SUCCESS!");
    if (file_assist.removeFile(SYSCTL_CONF_PATH_BAK)) {
      logger.LOG(0, "[+] BAK FILE REMOVED SUCCESS!");
    }

    return true;
  }

  inline bool restoreCpuGovernor() {
    if (!fs::exists(CPU_PATH)) {
      logger.LOG(2, std::format("[-] NO SUCH FILE OR DIRECTORY: {}", CPU_PATH));
      return false;
    }

    FileAssist file_assist(logger);
    std::vector<std::string> cpuPaths;

    for (auto &d : listMatchingEntries(CPU_PATH, isCpuDir))
      cpuPaths.push_back(CPU_PATH + d + "/cpufreq/scaling_governor");

    if (cpuPaths.empty()) {
      logger.LOG(1, "[!] NOT FOUND ANY CPU! FAILED TO RESTORE");
      return false;
    }

    INIReader reader(BACKUP_PATH);
    bool isRestored = false;
    bool isCpuExist = false;

    for (auto const &cpu : cpuPaths) {
      if (!reader.HasValue("", cpu))
        continue;

      auto mode = reader.Get("", cpu, "none");
      if (mode == "none")
        continue;

      if (file_assist.writeFile(cpu, mode).is_ok()) {
        std::string cpuMode;
        if (file_assist.readFile(cpu, cpuMode).is_ok()) {
          cpuMode = trimTrailingNewline(cpuMode);
          if (cpuMode == mode) {
            isCpuExist = true;
            continue;
          }
          if (isCpuExist) {
            logger.LOG(0, std::format("[.] NOTHING TO RESTORE FOR: {}", cpu));
          }
        }
        logger.LOG(0, std::format("[+] CPU: {} RESTORED!", cpu));
        isRestored = true;
      } else if (errno == EACCES) {
        logger.LOG(2, "[-] ACCESS DENIED, PLEASE RUN WITH ROOT.");
        return false;
      } else {
        logger.LOG(1, std::format("[-] FAILED TO RESTORE CPU: {} : {}", cpu,
                                  strerror(errno)));
      }
    }

    if (!isRestored) {
      if (errno == EACCES) {
        logger.LOG(2, "[-] ACCESS DENIED, PLEASE RUN WITH ROOT.");
      } else {
        logger.LOG(0, "[.] NOTHING TO RESTORE FOR: "
                      "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor");
      }
      return false;
    }

    return true;
  }

private:
  bool restoreGpuGovernor(FileAssist &file_assist) {
    auto getNameDrm = getDRM();
    std::string pathToGovernor = Config::DRM_PATH + getNameDrm +
                                 "/device/power_dpm_force_performance_level";

    INIReader reader(Config::BACKUP_PATH);

    if (!reader.HasValue("", pathToGovernor)) {
      logger.LOG(1, std::format("[!] NO VALUE FOR: {}", pathToGovernor));
      return false;
    }

    auto getMode = reader.Get("", pathToGovernor, "none");

    if (getMode == "none") {
      logger.LOG(
          2, std::format("[-] FAILED TO GET VALUE FROM: {}", pathToGovernor));
      return false;
    }

    std::string currMode;
    if (file_assist.readFile(pathToGovernor, currMode).is_ok()) {
      currMode = trimTrailingNewline(currMode);
      if (currMode == getMode) {
        logger.LOG(
            0, std::format("[.] NOTHING TO RESTORE FOR: {}", pathToGovernor));
        return true;
      }
    }

    if (file_assist.writeFile(pathToGovernor, getMode).is_ok()) {
      logger.LOG(0,
                 std::format("[+] GPU: {} RESTORED SUCCESS!", pathToGovernor));
      return true;
    }

    logger.LOG(2, std::format("[-] FAILED TO RESTORE: {}", pathToGovernor));
    return false;
  }

  bool restoreDiskScheduler(FileAssist &file_assist) {

    if (!fs::exists(Config::BLOCK_PATH))
      return false;

    std::vector<std::string> schedPaths = collectBlockSchedulerPaths();
    if (schedPaths.empty())
      return false;

    std::string currMode;

    INIReader reader(Config::BACKUP_PATH);
    for (auto const &sp : schedPaths) {

      if (!file_assist.readFile(sp, currMode).is_ok())
        continue;

      currMode = extractActiveScheduler(currMode);

      if (!reader.HasValue("", sp))
        continue;

      std::string r = reader.Get("", sp, "unknown");

      if (r == "unknown")
        continue;

      r = extractActiveScheduler(r);

      if (r == currMode) {
        logger.LOG(0, std::format("[.] NOTHING TO RESTORE FOR: {}", sp));
        continue;
      }

      if (file_assist.writeFile(sp, r, 0).is_ok()) {
        logger.LOG(0, std::format("[+] RESTORING SUCCESS FOR: {}", sp));
      }
    }

    return true;
  }

  bool restoreSplitLock(FileAssist &file_assist) {
    if (!fs::exists(Config::BACKUP_PATH)) {
      logger.LOG(2,
                 std::format("[-] FILE DOES NOT EXIST: {} FAILED TO RESTORE!",
                             Config::SPLIT_LOCK_PATH));
      return false;
    }

    INIReader reader(Config::BACKUP_PATH);
    if (!reader.HasValue("", Config::SPLIT_LOCK_PATH)) {
      logger.LOG(1,
                 std::format("[!] NO VALUE FOR: {}", Config::SPLIT_LOCK_PATH));
      return false;
    }
    auto getValue = reader.Get("", Config::SPLIT_LOCK_PATH, "none");
    if (getValue == "none") {
      logger.LOG(2, std::format("[-] FAILED TO GET VALUE FROM: {}",
                                Config::SPLIT_LOCK_PATH));
      return false;
    }

    std::string currValue;
    if (file_assist.readFile(Config::SPLIT_LOCK_PATH, currValue).is_ok()) {
      currValue = trimTrailingNewline(currValue);
      if (getValue == currValue) {
        logger.LOG(0, std::format("[.] NOTHING TO RESTORE FOR: {}",
                                  Config::SPLIT_LOCK_PATH));
        return true;
      }
    }

    if (file_assist.writeFile(Config::SPLIT_LOCK_PATH, getValue).is_ok()) {
      logger.LOG(0, std::format("[-] FILE: {} RESTORED SUCCESS!",
                                Config::SPLIT_LOCK_PATH));
    }

    return true;
  }

public:
  bool MainRestoring() {
    FileAssist file_assist(logger);

    restoreGpuGovernor(file_assist);
    restoreSplitLock(file_assist);
    restoreDiskScheduler(file_assist);

    return true;
  }

  bool restoreAll() {
    if (restoreSysctl())
      logger.LOG(0, std::format("[+] {} RESTORE DONE!", SYSCTL_CONF_PATH));

    if (restoreCpuGovernor())
      logger.LOG(0, "[+] CPU GOVERNOR RESTORED SUCCESS!");

    if (MainRestoring())
      logger.LOG(0, "[+] MAIN RESTORING FINISHED!");

    return true;
  }
};
