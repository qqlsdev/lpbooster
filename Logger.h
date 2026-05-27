#pragma once
#include <chrono>
#include <format>
#include <fstream>
#include <iostream>
#include <vector>

using namespace std::chrono;

class Logger {
private:
  const char *level[3] = {"INFO", "WARNING", "ERROR"};
  std::vector<std::string> Logs;

  inline std::string now() {
    auto t = floor<seconds>(system_clock::now());
    return std::format("[{0:%F | %T}]", t);
  }

public:
  inline void LOG(int lvl, std::string_view msg) {
    if (lvl > 2 || lvl < 0) {
      lvl = 0;
    }
    std::string str =
        std::format("{} : {} : {}\n", now(), this->level[lvl], msg);
    std::cout << str;
    Logs.push_back(str);
  }

  void SaveLogs(const std::string &Path) {
    std::ofstream file(Path, std::ios::app);
    if (file.is_open()) {
      for (auto const &logs : Logs) {
        if (logs.empty()) {
          continue;
        }
        file << logs;
      }
      std::cout << "Logs saved success!" << "\n";
    }
  }
};
