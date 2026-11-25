# PawnIO Access

[`src/sensors/pawnio.cpp`](../../src/sensors/pawnio.cpp) owns the device handles for [CPU](cpu.md) and [motherboard](board-sensors.md) sensor modules. PawnIO access requires administrator rights.

## Device and Modules

Sources: `namazso/PawnIO` (`PawnIOLib.cpp`, `pawnio_um.h`) and `namazso/PawnIO.Modules` 0.2.11 (`AMDFamily17.p`, `IntelMSR.p`, `LpcIO.p` in `third_party/pawnio/src/`).

- **Device & IOCTLs:** `\\?\GLOBALROOT\Device\PawnIO` via `DeviceIoControl` directly ([D-004](../decisions/D-004.md)). Device type `41394`, `METHOD_BUFFERED`, `FILE_ANY_ACCESS`: load module `0x821` (raw `.bin` payload), invoke function `0x841` (32-byte null-terminated function name + `uint64_t[]` args → `uint64_t[]` results). **Verified**.
- **Modules (each opens its own handle):**
  - `AMDFamily17` (AMD family 17h–1Ah): `ioctl_read_msr`, `ioctl_read_smn` (PCI `00:00.0` `0x60`/`0x64`; requires `Global\Access_PCI`).
  - `IntelMSR` (Intel x64): `ioctl_read_msr` (`ioctl_write_msr` is never called).
  - `LpcIO`: `ioctl_select_slot` (`0` = `0x2E/0x2F`, `1` = `0x4E/0x4F`), `ioctl_find_bars`, `ioctl_superio_inb`/`inw`/`outb`, `ioctl_pio_inb`/`outb`.
- **Anti-cheat note:** Some kernel anti-cheat drivers (e.g. FACEIT) block PawnIO; fail gracefully and surface status in the flyout.

## Missing Driver

On startup outside demo mode, a short-lived helper can offer the official installer when PawnIO is missing. It downloads over HTTPS and validates Authenticode before launching it. See [`src/app/pawnio_install.cpp`](../../src/app/pawnio_install.cpp) and [verification status](../progress.md#unverified-hardware-paths).
