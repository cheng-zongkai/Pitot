# Hardware Reference

Hardware sources, register definitions, decoding formulas, and measured readings live in the individual [subsystem documents](README.md#subsystems). This page records the shared measurement baseline and the complete hardware write inventory.

## Measurement Baseline

| Component | Primary verification hardware |
| --- | --- |
| Windows | Windows 11 26H2, build 26300 |
| CPU | AMD Ryzen 7 5700X. Earlier measurements also used a Ryzen 5 5600 |
| Motherboard | MSI PRO B550M-P WIFI6E (`NCT6687D`) |
| GPU | AMD Radeon RX 9060 XT 8 GB |
| Storage | NV7400 512 GB and aigo P3500 512 GB (NVMe) |
| Network | MediaTek MT7902 Wi-Fi and onboard Ethernet |

**Verified** means checked against an upstream reference, such as LibreHardwareMonitor (LHM, MPL-2.0) or vendor documentation. **Measured** identifies a physical measurement on the primary test PC unless the entry specifies another machine. **Unverified** identifies a path awaiting testing on matching hardware. See [progress](progress.md#unverified-hardware-paths) for open verification work.

## Writes Pitot Performs

This inventory covers writes made while reading hardware. Pitot never writes MSRs or fan, voltage, or clock controls. The [CPU](subsystems/cpu.md) and [motherboard sensors](subsystems/board-sensors.md) pages describe the associated reads and locking rules.

| Target | Write | Purpose |
| --- | --- | --- |
| Super I/O `0x2E`/`0x4E` | `0x87 0x87` / `0xAA`; ITE `0x87 0x01 0x55 0x55` (`0xAA`), exit `0x02 → 0x02` | Enter/exit configuration mode |
| Super I/O reg `0x07` | LDN (`0x0B`, `0x04`, `0x02`) | Select hardware-monitor LDN |
| Nuvoton `0x28` (LDN `0x0B`) | Clear `[4]` | Unlock I/O space (`NCT6791D+`, only when locked) |
| NCT668x EC ports | Page, index, `0xFF` | EC read protocol |
| Bank / ITE / Fintek ports | `0x4E` bank select, register index, ITE `0x06 [6:5]` clear | Bank/register addressing |
| PCI `0x60`/`0x64` (`AMDFamily17`) | SMN address | SMN read protocol |
