#pragma once

#include "../logger/logger.hpp"
#include "macros.hpp"
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <string.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

inline std::string textTrimmer(std::string &str) {
  const std::string whiteList = " \n\t\r";

  size_t end = str.find_last_not_of(whiteList);
  if (end != std::string::npos) {
    str.erase(end + 1);
  } else {
    str.clear();
    return "";
  }

  size_t begin = str.find_first_not_of(whiteList);
  if (begin != std::string::npos) {
    str.erase(0, begin);
  }

  return str;
}

inline std::string extractActiveScheduler(std::string str) {
  str = textTrimmer(str);
  size_t start = str.find('[');
  size_t end = str.find(']');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    return str.substr(start + 1, end - start - 1);
  }
  return str;
}

inline std::vector<std::string> getCPUPaths() {
  std::vector<std::string> governorPaths;

  if (!fs::exists(Config::CPU_PATH) || !fs::is_directory(Config::CPU_PATH)) {
    return governorPaths;
  }

  for (const auto &entry : fs::directory_iterator(Config::CPU_PATH)) {
    std::string dName = entry.path().filename().string();

    if (dName.starts_with("cpu") && dName.size() > 3 &&
        std::isdigit(static_cast<unsigned char>(dName[3]))) {

      std::string govPath =
          Config::CPU_PATH + dName + "/cpufreq/scaling_governor";

      if (fs::exists(govPath)) {
        governorPaths.push_back(govPath);
      }
    }
  }

  return governorPaths;
}

inline std::string getDRM() {

  Logger logger;

  if (!fs::exists(Config::DRM_PATH)) {
    logger.LOG(2, "[-] DRM PATH IS NOT FOUND!");
    return "";
  }

  int fd = open(Config::DRM_PATH.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

  DIR *dir = fdopendir(fd);
  if (!dir)
    return "";

  dirent *entry;

  while ((entry = readdir(dir)) != nullptr) {
    std::string d_name = entry->d_name;
    if (d_name.find("card") != std::string::npos && d_name.size() > 4 &&
        isdigit(d_name[4]) && d_name.find('-') == std::string::npos) {
      return d_name;
    } else {
      continue;
    }
  }

  return "";
}

class FileStatus {
private:
  int fd = -1;
  bool ok = false;

public:
  FileStatus(int _fd, bool _ok) : fd(_fd), ok(_ok) {}

  ~FileStatus() {
    if (fd >= 0) {
      close(fd);
    }
  }

  FileStatus &seek(off_t offset, int whence = SEEK_SET) {
    if (!ok || fd < 0)
      return *this;

    if (lseek(fd, offset, whence) < 0) {
      ok = false;
    }

    return *this;
  }

  bool is_ok() { return ok; }
};

class FileAssist {
private:
  Logger &logger;

public:
  [[nodiscard]] inline FileStatus readFile(const std::string &path,
                                           std::string &output) {

    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

    if (fd < 0) {
      if (errno == ENOENT) {
        logger.LOG(1, format("[!] READER: FILE: {} DOES NOT EXIST!", path));
      } else {
        logger.LOG(
            1, format("[!] READER: FAILED TO OPEN FILE: {}\n[READER] ERROR: {}",
                      path, strerror(errno)));
      }
      return FileStatus(-1, false);
    }

    char buff[4096];
    ssize_t bytes_read;

    lseek(fd, 0, SEEK_SET);

    while ((bytes_read = read(fd, buff, sizeof(buff))) > 0) {
      output.append(buff, bytes_read);
    }

    if (bytes_read < 0) {
      logger.LOG(1, format("[!] READER: FAIL TO READ FILE: {}", path));
      return FileStatus(-1, false);
    }

    return FileStatus(fd, true);
  }

  [[nodiscard]] inline FileStatus writeFile(const std::string &path,
                                            std::string input,
                                            int writeMode = O_APPEND) {
    int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | writeMode);
    if (fd < 0) {
      if (errno == ENOENT) {
        logger.LOG(2, format("[-] WRITER: FAILED TO OPEN FILE: {}", path));
      }
      return FileStatus(errno, false);
    }

    if (input.find('\n') == std::string::npos)
      input += '\n';

    ssize_t write_bytes = write(fd, input.c_str(), input.length());
    close(fd);

    if (write_bytes < 0) {
      logger.LOG(
          2,
          format("[-] WRITER: FAILED TO WRITE TO FILE: {}\n[WRITER] ERROR: {}",
                 path, strerror(errno)));
      return FileStatus(errno, false);
    }

    if ((size_t)write_bytes != input.length()) {
      logger.LOG(
          2,
          std::format("[-] INCOMPLETE WRITE TO: {} EXPECTED {} BYTES, WROTE {}",
                      path, input.length(), write_bytes));
      return FileStatus(EIO, false);
    }

    return FileStatus(0, true);
  }

  inline FileStatus createFile(const std::string &path) {

    int fd = open(
        path.c_str(),
        O_WRONLY | O_NOFOLLOW | O_CLOEXEC | O_CREAT | O_TRUNC | O_EXCL, 0666);

    if (fd < 0) {
      if (errno == EEXIST) {
        return FileStatus(0, true);
      }
      return FileStatus(-1, false);
    }

    close(fd);
    return FileStatus(fd, true);
  }

  inline bool removeFile(const std::string &path) {

    if (!fs::exists(path)) {
      return false;
    }

    if (!fs::remove(path)) {
      return false;
    }

    return true;
  }

  FileAssist(Logger &_logger) : logger(_logger) {}
};
