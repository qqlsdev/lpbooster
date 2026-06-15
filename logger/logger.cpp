#include "logger.hpp"
#include "../utils/tools.hpp"
#include <chrono>
#include <filesystem>
#include <format>
#include <iostream>

using namespace std::chrono;
namespace fs = std::filesystem;

std::string Logger::now() {
  auto t = floor<seconds>(system_clock::now());
  return std::format("[{0:%F | %T}]", t);
}

void Logger::LOG(int lvl, std::string_view msg) {

  if (lvl > 2 || lvl < 0) {
    lvl = 0;
  }

  statusColors colStat = static_cast<statusColors>(lvl);

  switch (colStat) {
  case SUCCESS:
    color = Colors::GREEN;
    break;
  case WARNING:
    color = Colors::YELLOW;
    break;
  case ERROR:
    color = Colors::RED;
    break;
  default:
    color = Colors::RESET;
    break;
  }

  std::string str = std::format("{} : {} : {}\n", now(), this->level[lvl], msg);
  std::cout << color << str << Colors::RESET;
  Logs.push_back(str);
}

void Logger::SaveLogs(const std::string &path) {

  Logger logger;
  FileAssist file_assist(logger);

  if (!fs::exists(path)) {
    file_assist.createFile(path);
  }

  for (auto const &logs : Logs) {
    file_assist.writeFile(path, logs).is_ok();
  }

  logger.LOG(0, std::format("[+] LOGS SAVED TO PATH: {}", path));
}
