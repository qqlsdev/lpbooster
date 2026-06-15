# lpbooster v1.6.8

Консольная утилита оптимизации Linux: очистка системы, отключение лишних сервисов, game mode (CPU/GPU/sysctl) и настройка I/O-планировщиков. Главное нововведение релиза — **бэкап и откат** изменений.

---

## Что нового

- **`--restore`** — восстановление настроек из резервной копии
- **Автобэкап** перед `--game-mode` и `--disk-optimization` → `/var/lib/lpbooster/backup.ini`
- **GPU boost** — AMD DRM: `power_dpm_force_performance_level=high`
- **Disclaimer и подтверждения** перед опасными операциями
- **Модульная архитектура** — `logger/`, `utils/`, отдельный `BackupManager`

---

## Улучшения

- Цветной лог с метками времени (INFO / WARNING / ERROR)
- Безопасная работа с sysfs через `O_CLOEXEC` / `O_NOFOLLOW`
- Детект пакетных менеджеров через `command -v` (не жёсткие пути `/usr/bin/*`)
- Исправлена очистка pacman: `pacman -Qdtq | pacman -Rns -`
- Sysctl без дубликатов + применение через `sysctl -p`
- Отключение сервисов по одному с отдельным логом на каждый
- Корректный парсинг I/O-планировщика (`[bfq]` → `bfq`)
- В список виртуальных дисков добавлен `zram`

---

## Изменения поведения

- Убрана настройка **PCIe ASPM** (была в v1.1.0)
- Перед `--clean-system` теперь запрашивается подтверждение
- При старте утилита показывает предупреждение о рисках

---

## Использование

```bash
cmake -B build && cmake --build build
sudo cp build/lpboost /usr/local/bin/

sudo lpboost --game-mode
sudo lpboost --disk-optimization
sudo lpboost --clean-system
sudo lpboost --disable-services
sudo lpboost --restore
```

---

## Требования

- Linux (systemd)
- C++20, CMake 3.14+
- Root-права для большинства операций

---

**Full Changelog:** v1.1.0...v1.6.8
