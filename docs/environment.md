# Development Environment

Toolchain, build commands, and local testing notes.

## Toolchain

| Component | Version / Path |
| --- | --- |
| OS | Windows 11 x64 (26H2 / build 26300 verified) |
| Compiler | Visual Studio 2022 Build Tools 17.14 (`cl` x64, `/std:c++20 /W4 /WX /permissive- /utf-8`, `/MT`) |
| Windows SDK | 10.0.26100.0 |
| Build system | CMake 4+ + Ninja |
| PawnIO driver | Installed service (`\\?\GLOBALROOT\Device\PawnIO`) for runtime sensor tests |
| Installer compiler | Inno Setup 6.7.3, per user, from `scripts/get-innosetup.ps1`; only needed to package a release |

Install prerequisites via winget:

```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id Ninja-build.Ninja -e
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--quiet --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Third-party binaries and SDKs in `third_party/`:
- `third_party/pawnio/` (committed): signed `AMDFamily17.bin`, `IntelMSR.bin`, and `LpcIO.bin` from PawnIO.Modules 0.2.11 plus upstream `.p` sources (`third_party/pawnio/src/`).
- `third_party/adlx/` (git-ignored, AMD proprietary license, [D-033](decisions/D-033.md)): ADLX SDK v1.5 headers. Run once to build `PitotAdlx.dll`:

  ```powershell
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\get-adlx.ps1 -AcceptAmdLicense
  ```

  Without it, CMake skips `PitotAdlx.dll` and `Pitot.exe` falls back to D3DKMT. `PitotNvidia.dll` needs no extra download ([D-081](decisions/D-081.md)).

## Building and Testing

From any PowerShell prompt (`scripts/build.ps1` refreshes `PATH` and loads the MSVC developer shell automatically):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1                    # Debug
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config release
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config all -Test  # Debug + Release + unit tests
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check-docs.ps1               # Documentation checks
```

Inside an active VS developer shell you can run presets directly:

```powershell
cmake --preset x64-debug
cmake --build --preset x64-debug
ctest --preset x64-debug
```

Outputs are written to `build\x64-debug\` and `build\x64-release\` (`Pitot.exe`, `PitotNvidia.dll`, optional `PitotAdlx.dll`, `pitot_tests.exe`). Filter unit tests with `pitot_tests.exe <substring>`. Note: the first run of a newly linked binary may pause 1–3 s due to Windows Defender scanning.

### Signing release binaries

After the final build and before packaging, sign the executable and its shipped DLLs. From a Visual Studio developer PowerShell, use the Windows SDK's SignTool with a publicly trusted code-signing certificate in the current user's Personal store. Set `$thumbprint` to that certificate's thumbprint and `$timestampUrl` to the provider's RFC 3161 endpoint.

```powershell
$files = 'Pitot.exe', 'PitotNvidia.dll', 'PitotAdlx.dll' |
    ForEach-Object { Join-Path 'build/x64-release' $_ } |
    Where-Object { Test-Path -LiteralPath $_ }
signtool sign /sha1 $thumbprint /fd SHA256 /tr $timestampUrl /td SHA256 $files
if ($LASTEXITCODE -ne 0) { throw 'Release signing failed' }
signtool verify /pa /tw $files
if ($LASTEXITCODE -ne 0) { throw 'Release signature verification failed' }
```

The release workflow and ordinary local builds produce unsigned binaries; sign them before running `scripts/package.ps1` when a certificate is available. The installer `Pitot-<version>-setup.exe` is unsigned as well; signing it needs Inno Setup's `SignTool` setting, which is not configured yet. See [SignTool](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool) and [SmartScreen reputation for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

To regenerate `src/app/pitot.ico` from `src/app/pitot*.svg` (requires Edge or Chrome):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\make-icon.ps1
```

## Releasing

A release is a version tag on `main`. Pushing a tag that starts with `MAJOR.MINOR.PATCH` (for example `0.1.0` or `0.1.0-dev`, no `v` prefix) runs `.github/workflows/release.yml`. It builds Release, runs the unit tests, installs Inno Setup with `scripts/get-innosetup.ps1`, runs `scripts/package.ps1`, and creates the GitHub release with three assets ([D-091](decisions/D-091.md)):

| Asset | Content |
| --- | --- |
| `Pitot-<version>-setup.exe` | Installer compiled from `installer/Pitot.iss` |
| `Pitot-<version>-win-x64.zip` | Portable copy of the same files |
| `SHA256SUMS.txt` | SHA-256 of the two files above |

Both hold `Pitot.exe`, `PitotNvidia.dll`, `LICENSE`, `THIRD_PARTY_NOTICES.md` and `third_party/pawnio/COPYING`, and after the repack step below also `PitotAdlx.dll` and `PitotAdlx-EULA.txt`. A loose `Pitot.exe` or DLL is never a release asset.

Installer behavior (`installer/Pitot.iss`):

- Installs per machine into `Program Files\Pitot` with administrator rights, on x64 Windows 11 only, with a Start menu shortcut and an optional desktop icon. The wizard is English or Simplified Chinese, following the Windows display language.
- Before installing or uninstalling, it posts `WM_CLOSE` to the `Pitot.Main` and `Pitot.Settings` windows and waits up to 10 s for the `Local\Pitot.*` mutexes to be released, so Pitot saves its traffic counters. If Pitot is still running, it stops with a message.
- Every install first deletes `PitotAdlx.dll` and `PitotAdlx-EULA.txt` from the install folder, so an installer without the AMD DLL leaves no stale copy.
- Uninstall deletes `\Pitot\Autostart` only when the task runs the installed `Pitot.exe`, and keeps `%APPDATA%\Pitot` and `%LOCALAPPDATA%\Pitot`.
- The `AppId` GUID must never change; upgrades and uninstall find the installed copy through it.

Rules:

- The numeric part of the tag must equal `project(Pitot VERSION ...)` in `CMakeLists.txt`, and `src/app/Pitot.rc` carries the same number; bump both before tagging. `package.ps1` fails on a mismatch with `CMakeLists.txt`.
- A tag with a `-` suffix (`0.1.0-dev`) is published as a pre-release.
- The release body is `.github/release-notes.md` followed by GitHub's generated notes.
- `PitotAdlx.dll` is not built by CI ([D-033](decisions/D-033.md)). After the workflow publishes the release, build the DLL locally from the tagged commit and repack the installer and zip with it:

  ```powershell
  git checkout 0.1.0
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Config release -Target PitotAdlx
  powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\repack-adlx.ps1 -Tag 0.1.0
  git checkout main
  ```

  The script refuses to run when `HEAD` is not at the tag or the tree is dirty. It downloads the release's zip, takes `Pitot.exe` and `PitotNvidia.dll` from it, rebuilds the installer and zip with the local `PitotAdlx.dll` and `PitotAdlx-EULA.txt` inside, and replaces those two assets and `SHA256SUMS.txt`. It needs `third_party/adlx/` from `get-adlx.ps1 -AcceptAmdLicense` and Inno Setup from `get-innosetup.ps1`. `-Target PitotAdlx` builds only the DLL, so a running `build\x64-release\Pitot.exe` does not block the build.
- Binaries are unsigned unless you sign them first (see above).

```powershell
git tag -a 0.1.0 -m 'Pitot 0.1.0'
git push origin 0.1.0
```

To dry-run the packaging locally, run `scripts/get-innosetup.ps1` once, `scripts/build.ps1 -Config release`, then `scripts/package.ps1 -Version 0.1.0-dev` (add `-AdlxDll build\x64-release\PitotAdlx.dll` to include the AMD DLL); files appear in the git-ignored `dist/`. If the workflow fails after the release exists, delete it with `gh release delete <tag> --cleanup-tag` before tagging again.

## Measuring Memory and CPU

- Task Manager "Memory" corresponds to **private working set** (`PROCESS_MEMORY_COUNTERS_EX2.PrivateWorkingSetSize`); `Get-Process` `PrivateMemorySize64` is **private commit**. Check both.
- Wait at least 10 seconds after closing the flyout before measuring (heap/D2D memory release is lazy).
- `memory:` lines in `%LOCALAPPDATA%\Pitot\pitot.log` log private commit across startup and flyout open/close.
- The previous reference footprint on the primary machine was about 8 MB with the strip only and 12 MB after opening the flyout.
- CPU usage: `(TotalProcessorTime delta) / (wall delta * logical cores)`.

## UI Automation & Screenshots

- Always call `SetProcessDpiAwarenessContext(-4)` (Per-Monitor V2) in UI test/screenshot scripts so coordinates match physical pixels.
- In PowerShell `Add-Type` P/Invoke signatures taking `StringBuilder`, specify `CharSet = CharSet.Unicode` or `GetClassNameW` returns garbled strings.
- Do not pass PowerShell arrays with `powershell -File script.ps1 -Points @(1,2)` (it coerces to string); dot-source or invoke `&` inside a session.
- **Demo screenshots:** Run `build\x64-release\Pitot.exe --demo` (no admin rights required; generates synthetic metrics so no local hardware, processes, or network data appear). Hide desktop icons, capture both light and dark modes (via the flyout sun button with empty wallpaper paths) at a calm moment outside the synthetic burst, save to `docs/images/`, and exit demo mode before restarting Pitot.

## Privileges and Reference Tools

- PawnIO, kernel ETW sessions, and `SystemMemoryListInformation` require an administrator process.
- Do not leave `\Pitot\Autostart` enabled during development unless specifically testing logon autostart.
- Reference tools for comparing readings: Task Manager, Resource Monitor, RAMMap, HWiNFO, CrystalDiskInfo, AMD Adrenalin overlay, and `nvidia-smi`.
