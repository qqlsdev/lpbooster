#pragma once
#include <string>
#include <string_view>
#include <vector>

class FileAssist;

enum statusColors { SUCCESS = 0, WARNING = 1, ERROR = 2 };

class Logger {
private:
  const char *level[3] = {"INFO", "WARNING", "ERROR"};
  std::vector<std::string> Logs;
  std::string_view color;
  std::string now();

public:
  void LOG(int lvl, std::string_view msg);
  void SaveLogs(const std::string &path);
};
