# Motherboard Sensors

This subsystem uses [PawnIO](pawnio.md) `LpcIO` for motherboard temperatures and fan speeds. It runs on [sensor rounds](sampling.md#cadence) and requires administrator rights. On mutex contention it skips the round and retains the previous readings.

## Super I/O

Sources: LHM `LpcIO.cs`, `LpcPort.cs`, `Nct677X.cs`, `IT87XX.cs`, `F718XX.cs`. Code: `src/sensors/board.*`, `board_chips.*`; tests: `tests/board_chips_test.cpp`. Hold `Global\Access_ISABUS.HTP.Method` (10 ms wait). Probe both `0x2E` and `0x4E` (up to 2 chips), each with its own `LpcIO` handle.

### Detection & Supported Families

1. **Nuvoton / Fintek:** Write `0x87` twice, read chip ID `0x20` / rev `0x21`. Fintek also checks vendor `0x1934` at `0x23`.
2. **ITE:** On `0x4E`, read 16-bit ID before entering config mode (avoids IT8792E ID bug); else enter with `0x87 0x01 0x55 0x55` (`0xAA` on `0x4E`). Exit via `0x02 → 0x02` (not on `0x4E`).
3. Call `ioctl_find_bars`, select LDN (`0x0B` Nuvoton, `0x04` ITE/Fintek, `0x02` F71858), read `0x60` 16-bit base twice 1 ms apart (`base >= 0x100`, `(base & 0xF007) == 0`; clear Fintek low `101` quirk).

| Family | Chips (`ID:rev`) | Access / Status |
| --- | --- | --- |
| Nuvoton EC | `NCT6683D` (`C7:32`), `NCT6686D` (`D4:40/41`), `NCT6687D` (`D5:92`) | EC page/index/data (`base+4/+5/+6`). `NCT6687D` **Measured**; others **Unverified** |
| Nuvoton Bank | `NCT6779D` (`C5:6x`), `6791D` (`C8:03`), `6792D` (`C9:11/13`), `6793D` (`D1:21`), `6795D` (`D3:52`), `6796D` (`D4:23`), `6796D-R` (`D4:2A`), `6797D` (`D4:51`), `6798D` (`D4:2B`), `6799D` (`D8:02`) | `base+5/+6`, bank via `0x4E`. **Unverified** |
| ITE | `IT8613E–IT8696E`, `IT8705F–IT8728F`, `IT8771E/8772E`, `IT8790E`, `IT8792E` (`0x8733`), `IT87952E` (`0x8695`) | `base+5/+6`, index readback verify (except `IT8688E`). **Unverified** |
| Fintek | `F71808E`, `F71858`, `F71862`, `F71869`, `F71869A/F71811`, `F71878AD`, `F71882`, `F71889AD/ED/F` | `base+5/+6`. **Unverified** |

Not supported: `NCT6701D`, legacy `NCT6771F/6776F/610xD`/Winbond `W836xx`, SMSC, ASUS EC (`0x25C`). Channels that never produce a valid non-0 / `< 127` reading (or spun fan) stay hidden.

### Register Decoding

- **Nuvoton EC (`NCT668x`):** Wait page reg `== 0xFF` (32 polls; skip round if busy, never force), write page/index, read data, write `0xFF`. Temps: `0x100 + 2*i` (`i < 11`, signed hi + `lo[7]*0.5 °C`, `0` or `>= 127` invalid). Fans: `0x140 + 2*i` (`i < 16`, 16-bit BE RPM).
  - **Measured (`NCT6687D`, MSI B550M-P, slot 1 `0x4E`, base `0x0A20`, ctrl `0x80`):** `0x100` CPU (`43–44 °C`), `0x102` System (`45 °C`), `0x104` VRM (`43.5 °C`), `0x106` Chipset (`47.5 °C`), `0x108` CPU Socket (`37 °C`), `0x10A` PCIe Slot 1 (`39.5 °C`), `0x140` CPU Fan (`820–914 RPM`), `0x142` Pump (`0`), `0x144` System Fan 1 (`~1010 RPM`).
- **Nuvoton Bank (`NCT67xx`):** Check vendor `(0x804F << 8) | 0x004F == 0x5CA3`; if locked, enter config LDN `0x0B` and clear `0x28 [4]` (`6791D+`, max once/min). Temps: read source-mapped channels (`0x073..0x07D`, `0x4A0`, `0x027`, `0x150`, `0x670..0x67A`, `0x4A2`) first (`-55..125 °C`), then per-source integer fallbacks (`SYSTIN 0x490`, `CPUTIN 0x491`, `AUXTIN0-3 0x492-0x495`, `TSENSOR 0x496`, `PCH 0x401`). Exposed sources: `1, 2, 3–7, 8, 10, 16, 19`. Fans: 13-bit counts at `0x4B0, 4B2, 4B4, 4B6, 4B8, 4BA, 4CC` (`>= 0x1FFF` → `0 RPM`, `< 0x15` invalid, `1.35e6 / count`).
- **ITE:** Verify vendor `0x58 == 0x90/0x7F`, cfg `0x00 [4]` (except `8655E/8665E`). Clear `0x06 [6:5]` on `8655E/8665E` before reading (sleep bug). Temps: `0x29 + i` (signed, `1..126 °C`). Fans: 16-bit (`0x0D/0x18`, etc.; enable bits `0x0C [4/5/2]`, `0x0B [3]`; `<= 0x3F` invalid, `0xFFFF` → `0`, `1.35e6 / (2 * count)`) or 8-bit on old `8705F`/`8712F` (`1.35e6 / (count * divisor)`).
- **Fintek:** Temps: `0x70 + 2*(i+1)` (`1..126 °C`; `F71858` uses 12-bit table at `0x70+2i` / `0x69 [1:0]`). Fans: 16-bit BE at `0xA0, 0xB0, 0xC0, 0xD0` (`0` invalid, `>= 0x0FFF` → `0`, `1.5e6 / count`).

## Verification

Use `tests/board_chips_test.cpp` for the detection table and decoding. On matching hardware, compare each temperature and fan reading with HWiNFO. Include the board, chip, Windows build, and relevant log lines in the [hardware report form](https://github.com/cheng-zongkai/Pitot/issues/new?template=hardware_report.yml). See the separate [write inventory](../hardware.md#writes-pitot-performs).
