## Downloads

| File | What it is |
| --- | --- |
| `Pitot-<version>-setup.exe` | Recommended. Installs Pitot into `C:\Program Files\Pitot` with a Start menu shortcut. Run a newer installer to update; uninstall from **Settings > Apps**. |
| `Pitot-<version>-win-x64.zip` | Portable copy of the same files. Extract **all** files into a permanent folder. |
| `SHA256SUMS.txt` | SHA-256 hashes of the two downloads above. |

Both contain `Pitot.exe`, `PitotNvidia.dll`, `PitotAdlx.dll` and the license files. `PitotAdlx.dll` adds the extra AMD GPU metrics and is licensed under `PitotAdlx-EULA.txt`, which sits next to it; if you do not agree to that agreement, delete the DLL. Pitot runs without it.

## Before you run it

- Windows 11 x64. Pitot asks for administrator rights (UAC prompt) on every launch until you turn on **Start at logon**.
- The installer and binaries are not code-signed, so Windows SmartScreen may warn. Click **More info**, then **Run anyway**, if you trust the download.
- CPU and motherboard sensors need the [PawnIO](https://pawnio.eu/) driver. Pitot offers to download its installer on first launch.
- Pitot was tested on one machine (Ryzen 7 5700X, MSI B550, Radeon RX 9060 XT). The [known limitations](https://github.com/cheng-zongkai/Pitot#known-limitations) list the paths that have not run on real hardware yet. Hardware reports are welcome.
