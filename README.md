# lpboost

A command-line system optimization utility for Linux. Cleans junk, disables unnecessary services, tweaks kernel parameters for gaming, and optimizes I/O schedulers for your storage devices.

---

## Requirements

- Linux (systemd-based distro: Arch, Ubuntu/Debian, Fedora, etc.)
- C++23-compatible compiler (GCC 13+ or Clang 17+)
- Root privileges for most operations

## Building

```bash
git clone https://github.com/qqlsdev/lpbooster.git
cd lpboost
g++ -std=c++23 -O2 -o lpboost main.cpp
```

Or with CMake:

```bash
cmake -B build && cmake --build build
sudo cp build/lpboost /usr/local/bin/
```

---

## Usage

```
lpboost [OPTIONS]
```

Most options require root:

```bash
sudo lpboost --clean-system
sudo lpboost --disable-services
sudo lpboost --game-mode
sudo lpboost --disk-optimization
```

---

## Options

### `--clean-system`

Removes orphaned packages, unused Flatpak runtimes, stale caches, and old coredumps. Detects your package manager automatically:

- **pacman** — removes orphan packages
- **apt** — runs `autoremove` + `clean`
- **dnf** — runs `autoremove`
- **flatpak** — removes unused runtimes

Also cleans `~/.cache/{thumbnails,fontconfig,pip}` and vacuums the systemd journal down to 50 MB.

---

### `--disable-services`

Disables and stops systemd services and timers that are unnecessary for most desktop setups. You'll be asked whether to also disable Bluetooth.

Disabled by default: `bluetooth`, `cups`, `avahi-daemon`, `geoclue`, `irqbalance`, `unattended-upgrades`, NFS/RPC stack, Hyper-V daemons, evolution data server, and more.

If an HDD is detected without an SSD present, `fstrim.timer` is preserved (fstrim is only meaningful on SSDs).

---

### `--game-mode`

Applies a set of kernel and CPU tweaks aimed at reducing latency for gaming:

- Sets CPU frequency governor to `performance` for all cores
- Creates and enables a persistent systemd service (`lpbooster-cpu.service`) to reapply the governor on boot
- Tunes VM parameters: `swappiness=10`, lower dirty ratios
- Tunes CFS scheduler: reduced latency and wakeup granularity
- Enables `ananicy` and `gamemode` daemons if installed
- Disables `split_lock_mitigate` for better compatibility with some games

> **Note:** These tweaks increase power consumption. Not recommended for laptops on battery.

---

### `--disk-optimization`

Sets the optimal I/O scheduler for each physical storage device:

- **HDD** → `bfq` (Budget Fair Queueing — better latency under mixed load)
- **SSD/NVMe** → `none` (pass-through, lets the drive's own queue handle everything)

Virtual and loop devices are skipped automatically.

---

## Logging

lpboost uses a built-in `Logger` class. Log levels:

| Level | Meaning |
|-------|---------|
| `0`   | Info / success |
| `1`   | Warning |
| `2`   | Error |

---

## License

MIT
