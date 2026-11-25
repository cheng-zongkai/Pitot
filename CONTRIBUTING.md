# Contributing to Pitot

Thank you for your interest in contributing to Pitot. Contributions are welcome, especially bug reports, hardware compatibility reports, and pull requests for hardware that could not be tested on the primary development machine.

## Scope

Pitot is a lightweight Windows 11 taskbar and flyout hardware monitor written in native C++20 and Win32/Direct2D.

**In scope:** hardware monitoring, UI responsiveness, sensor decoding accuracy, localization (Simplified Chinese and English), and bug fixes.

**Out of scope:** fan control, overclocking, voltage/clock writes, ARM64 support, and new runtime frameworks (.NET, WinUI, Qt, Electron, or bundled JSON parsers).

## Prerequisites

- Windows 11 x64
- Visual Studio 2022 (or Build Tools) with the **Desktop development with C++** workload
- CMake 3.25+ and Ninja
- Optional: [PawnIO](https://pawnio.eu/) driver for runtime CPU and motherboard sensor testing
- Optional: AMD ADLX SDK v1.5 headers (downloaded via `scripts/get-adlx.ps1 -AcceptAmdLicense`) to build `PitotAdlx.dll`

## Building and Testing

From a PowerShell prompt:

```powershell
# Build Debug + Release and run all unit tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config all -Test

# Validate documentation links, budgets, and ADR index
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-docs.ps1
```

Both `x64-debug` and `x64-release` must compile with zero warnings (`/W4 /WX`) and pass all unit tests in `pitot_tests.exe`.

## Coding Conventions

- **Language & Compiler:** C++20, Unicode (`W` Win32 APIs), `/W4 /WX /permissive- /utf-8`, UTF-8 with LF line endings.
- **Comments & Docs:** Identifiers, commit messages, and documentation are written in English. Code comments are written in Simplified Chinese and explain *why*, not *what*.
- **ASCII-only files:** Keep `scripts/*.ps1`, `.rc` files, and `src/app/resource.h` ASCII-only so Windows PowerShell 5.1 and `rc.exe` do not misparse them.
- **UI thread discipline:** The taskbar strip is a child window of `Shell_TrayWnd`, so its input queue is joined with Explorer's. Never block or call hardware/disk/network APIs on the UI thread, and keep the flyout idle when closed.
- **UI strings:** Add all user-visible text to both Chinese and English tables in `src/ui/strings.*`, keeping `%s`/`%d` placeholder order identical. Follow [docs/design.md](docs/design.md).
- **Hardware coexistence:** Hold `Global\Access_PCI` for SMN reads and `Global\Access_ISABUS.HTP.Method` for Super I/O / EC access (10 ms timeout, skip round on contention). Document any new register or port write in [docs/hardware.md](docs/hardware.md).

## Licensing and Third-Party Boundaries

Pitot's own source code is licensed under the [MIT License](LICENSE). Because the repository also interacts with third-party drivers and vendor SDKs, strict boundaries apply:

- **PawnIO modules (LGPL-2.1-or-later):** Embedded in `Pitot.exe`; corresponding sources live in `third_party/pawnio/src/`.
- **AMD ADLX SDK (AMD proprietary):** Never commit ADLX headers or sample code. They stay in the git-ignored `third_party/adlx/` directory and are compiled only into the optional `PitotAdlx.dll` across the pure-C interface in `src/sensors/adlx_abi.h`.
- **NVIDIA NVML / NVAPI:** `PitotNvidia.dll` dynamically loads `nvml.dll` and `nvapi64.dll` from the installed driver without bundling vendor headers. Do not use unpublished NVAPI query IDs or `nvmlDeviceSet*` calls.
- **LibreHardwareMonitor (MPL-2.0):** Consulted only for hardware register facts and formulas; write original C++ code rather than copying or translating line by line.
- Update [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before introducing any third-party reference or asset.

## Submitting Pull Requests

1. Keep changes focused. Link the related Issue when there is one. Use the [subsystem map](docs/README.md#subsystems) to find the code and update the document that owns any changed behavior. Use the [project references](docs/README.md#project-references) for shared rules, settings, and decision rationale.
2. Run `scripts/build.ps1 -Config all -Test` and `scripts/check-docs.ps1`.
3. In the PR, state which hardware and Windows build you tested on, what you observed, and which paths remain unverified. Use the [hardware report form](https://github.com/cheng-zongkai/Pitot/issues/new?template=hardware_report.yml) to share standalone hardware results.
4. See [AGENTS.md](AGENTS.md) if you are using an AI coding agent.
