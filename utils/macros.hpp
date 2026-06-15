#pragma once

#include <algorithm>
#include <array>
#include <dirent.h>
#include <fcntl.h>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>

namespace Colors {
constexpr std::string_view RESET = "\033[0m";
constexpr std::string_view BOLD = "\033[1m";
constexpr std::string_view GREEN = "\033[32m";
constexpr std::string_view CYAN = "\033[36m";
constexpr std::string_view YELLOW = "\033[33m";
constexpr std::string_view RED = "\033[31m";
} // namespace Colors

namespace Config {
constexpr double GIGABYTE = 1024.0 * 1024.0 * 1024.0;

const std::string BLOCK_PATH = "/sys/block/";
const std::string CPU_PATH = "/sys/devices/system/cpu/";
const std::string DRM_PATH = "/sys/class/drm/";
const std::string BACKUP_PATH = "/var/lib/lpbooster/backup.ini";

const std::string PERFORMANCE = "performance\n";
const std::string POWERSAVE = "powersave";
const std::string BFQ = "bfq";
const std::string NONE = "none";
const std::string HIGH = "high";
const std::string AUTO = "auto";
const std::string LOW = "low";

const std::string CPU_SERVICE_PATH =
    "/etc/systemd/system/lpbooster-cpu.service";
const std::string SYSCTL_CONF_PATH = "/etc/sysctl.d/99-sysctl.conf";
const std::string SYSCTL_CONF_PATH_BAK = "/etc/sysctl.d/99-sysctl.conf.bak";
const std::string SPLIT_LOCK_PATH = "/proc/sys/kernel/split_lock_mitigate";

const std::array<std::string_view, 6> VIRTUAL_DISKS = {"loop", "zram", "ram",
                                                       "dm",   "nbd",  "md"};
const std::string VM_TWEAKS[] = {
    "vm.swappiness=10",
    "vm.vfs_cache_pressure=50",
    "kernel.sched_latency_ns=4000000",
    "kernel.sched_min_granularity_ns=1000000",
    "kernel.sched_wakeup_granularity_ns=1000000",
};

constexpr std::string_view SERVICES_TO_DISABLE[] = {
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
    "hv-fcopy-daemon.service"};
} // namespace Config

namespace SystemUtils {

inline bool runCommand(const std::string &cmd) {
  return std::system((cmd + " > /dev/null 2>&1").c_str()) == 0;
}

inline double getTotalRAM() {
  long pageSize = sysconf(_SC_PAGESIZE);
  long long totalPages = sysconf(_SC_PHYS_PAGES);
  return static_cast<double>(totalPages * pageSize) / Config::GIGABYTE;
}

inline std::optional<bool> isDriveRotational(const std::string &devicePath) {
  const std::string rotPath = devicePath + "/queue/rotational";

  int fd = open(rotPath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0)
    return std::nullopt;

  char buff[4] = {0};
  ssize_t bytesRead = read(fd, buff, sizeof(buff) - 1);
  close(fd);

  if (bytesRead < 1) {
    return std::nullopt;
  }

  return buff[0] == '1';
}

inline void detectDrives(bool &outHDD, bool &outSSD) {

  outHDD = outSSD = false;

  int fd = open(Config::BLOCK_PATH.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) {
    return;
  }

  DIR *dir = fdopendir(fd);
  if (!dir) {
    close(fd);
    return;
  }

  dirent *entry;

  while ((entry = readdir(dir)) != nullptr) {
    const std::string dName = entry->d_name;
    if (dName == "." || dName == "..")
      continue;

    bool isVirtual = std::any_of(
        Config::VIRTUAL_DISKS.begin(), Config::VIRTUAL_DISKS.end(),
        [&](const std::string_view vtd) { return dName.starts_with(vtd); });

    if (isVirtual)
      continue;

    auto rot = isDriveRotational(Config::BLOCK_PATH + dName);
    if (!rot.has_value())
      continue;
    if (rot.value())
      outHDD = true;
    else
      outSSD = true;

    if (outHDD && outSSD) {
      closedir(dir);
      return;
    }
  }
}

inline std::string getCmdOutput(const std::string &cmd) {

  std::array<char, 128> buffer;
  std::string result;

  FILE *pipe = popen(cmd.c_str(), "r");

  if (!pipe) {
    return "";
  }

  while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
    result += buffer.data();
  }

  pclose(pipe);

  if (!result.empty() && result.back() == '\n') {
    result.pop_back();
  }

  return result;
}
} // namespace SystemUtils
