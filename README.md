# ProcessLens

Real-time native Windows system monitor written in modern C++.

ProcessLens is a small dark desktop widget for Windows 10 and Windows 11. It reads live operating-system counters on a background thread and renders them with Direct2D and DirectWrite—there is no browser, web server, Electron runtime, or managed UI layer.

## Features

- Compact widget with CPU, memory, and GPU graphs, network throughput, and top-five processes.
- Expanded process monitor with sortable CPU, working-set, disk-read, and disk-write columns.
- Live search by executable name or PID and a process details panel.
- Confirmed, user-initiated process termination with access-denied handling.
- Frameless resizable dark window, Windows 11 rounded corners, per-monitor DPI V2, and multi-monitor position recovery.
- Always-on-top and click-through modes. The tray icon or `Ctrl+Alt+O` always restores interaction.
- ProcessLens starts hidden at user sign-in by default, keeping `Ctrl+Alt+O` available without showing a window. The tray menu can disable **Start with Windows**.
- A regular Start Menu launcher is installed for manual starts; subsequent launches activate the existing single instance.
- Refresh intervals of 250, 500, 1000, and 2000 ms; 1000 ms is the default.
- Local JSON settings in `%LOCALAPPDATA%\ProcessLens\config.json`.

Double-click the widget, or use the `+`/`-` title button, to switch between compact and expanded modes. Click **by CPU / by RAM** in compact mode to change the top-process ranking.

## Screenshots

The application deliberately uses live counters, so the exact graphs and process rows vary per machine. Build and launch `ProcessLens.exe` to see the compact widget; double-click it for the expanded process table.

## Architecture

```text
Windows APIs
     |
     v
Collectors (CPU / memory / network / processes / GPU engines)
     |
     v
Metrics worker (std::jthread, 250-2000 ms)
     |
     v
immutable shared snapshot (atomic publication)
     |
     +-------------------+
     v                   v
Compact Direct2D UI  Expanded Direct2D UI
```

The UI thread never enumerates processes or network adapters. The worker creates a complete `MetricsSnapshot`, atomically publishes `shared_ptr<const MetricsSnapshot>`, and posts a lightweight repaint message. Histories use bounded circular buffers containing the latest 120 samples.

## Metrics Collection

| Metric | Windows source | Calculation |
|---|---|---|
| Total CPU | `GetSystemTimes` | `(totalDelta - idleDelta) / totalDelta` |
| Process CPU | `OpenProcess`, `GetProcessTimes` | process-time delta divided by wall-time delta and logical processor count |
| Physical memory | `GlobalMemoryStatusEx` | total, available, used, and load percentage |
| Process memory | `GetProcessMemoryInfo` | current working set |
| Network | `GetIfTable2` | summed connected, up, non-loopback adapter octet deltas per second |
| Process I/O | `GetProcessIoCounters` | transfer-byte deltas per second |
| Process inventory | Tool Help snapshot API | PID, executable name, and thread count |
| GPU | PDH `GPU Engine(*)\\Utilization Percentage` | sum process instances per physical engine, then select the busiest engine |

GPU collection uses the vendor-neutral Windows GPU engine counters introduced with WDDM 2.0. It mirrors Task Manager's overall-utilization semantics by grouping process instances by physical engine and displaying the busiest engine rather than summing unrelated engines. On systems without compatible counters, the widget safely reports `N/A` and the other collectors continue normally.

Protected and short-lived processes are expected. When Windows denies access or a process exits during collection, ProcessLens retains the Tool Help information it can read and uses zero/`N/A` for unavailable fields.

## Building

Requirements:

- Windows 10 or Windows 11 x64
- Visual Studio with the **Desktop development with C++** workload
- CMake 3.24 or newer
- Windows 10/11 SDK

From a Developer PowerShell for Visual Studio:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is written to `build\Release\ProcessLens.exe` for a Visual Studio generator.

If `cmake` or `cl` is not visible in an ordinary PowerShell, use **Developer PowerShell for VS** or invoke the CMake installed under the Visual Studio installation.

## Usage

- Drag the title area to move the widget; drag any edge to resize it.
- Double-click to switch modes.
- In expanded mode, click a column header to sort and click it again to reverse direction.
- Click the search field and type a process name or PID. `Esc` closes details first, then clears search.
- Click a row for details. **End Process** always displays a confirmation prompt and never elevates the application.
- Right-click the tray icon for mode, topmost, click-through, refresh-rate, and exit controls.
- Press `Ctrl+Alt+O` from anywhere to reveal ProcessLens. On first run it creates per-user Start Menu and Startup shortcuts; the Startup entry launches it hidden at sign-in so the system-wide hotkey remains registered. Disable this behavior with **Start with Windows** in the tray menu.
- Closing the custom title bar hides the window to the tray. Choose **Exit** from the tray to stop the application.

## Performance

Process enumeration, system sampling, and rendering occur once per selected refresh interval. Window interactions can trigger additional repaints, but there is no busy animation timer while the snapshot is unchanged. The 250 ms interval performs four times as many process scans as the default and may noticeably increase ProcessLens CPU usage on systems with many processes.

## Project Structure

```text
include/processlens/   public types, calculations, worker, renderer, and window APIs
src/                   Win32 implementation and Windows collectors
resources/             DPI-aware as-invoker application manifest
tests/                 dependency-free unit tests for deterministic logic
assets/                reserved for packaged icon assets
```

## Tests

The test executable covers system CPU delta calculation, process CPU normalization, GPU-engine aggregation, byte formatting, graph wraparound, process sorting/filtering, settings JSON round-tripping, and network rate calculation.

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Roadmap

1. Add signed application icons and an installer.
2. Add opacity and threshold notifications.
3. Add process executable paths and icons where permissions allow.
