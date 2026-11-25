# CPU Metrics

Pitot identifies processors dynamically through CPUID vendor, family, and model. It dispatches to the AMD Zen or Intel collector in `src/sensors/cpu.*`. An unrecognized vendor has no vendor sensor readings.

- PDH `\Processor Information(*)\% Processor Utility` (English name) provides total and per-logical-processor load on each [basic round](sampling.md).
- While the flyout is open, the CPU process list uses time deltas from `NtQuerySystemInformation(SystemProcessInformation)`. Active clock uses deltas from hardware counters on the same basic cadence.
- Temperatures and package power run on sensor rounds. These and active clock use [PawnIO](pawnio.md) and require administrator rights. Per-logical-processor reads temporarily set and then restore the sampling thread's affinity.

See the [flyout panel](flyout.md#panels) for presentation and [hardware reference](../hardware.md#writes-pitot-performs) for the hardware write inventory.

## AMD Zen

Source: LHM `Amd17Cpu.cs`. Unit tests: `tests/sensors_decode_test.cpp`.

| Name | Address | Purpose | Status |
| --- | --- | --- | --- |
| `MPERF` / `APERF` | MSR `0xC00000E7` / `0xC00000E8` | C0 base / actual clock counters | Verified |
| `PStateDef[0]` | MSR `0xC0010064` | Base (P0) frequency (`PstateEn` `[63]`) | Verified |
| `PWR_UNIT` / `PKG_ENERGY_STAT` | MSR `0xC0010299` / `0xC001029B` | Energy unit `ESU [12:8]`, pkg energy `[31:0]` | Verified |
| `THM_TCON_CUR_TMP` (`Tctl`) | SMN `0x59800` | Control temperature | Verified |
| `CCD` temp (Zen 2/3, TR3000) | SMN `0x59954 + 4*i` (`i < 8`) | Family `17h/71h`, `19h/21h`, `17h/31h` | Verified |
| `CCD` temp (Zen 4/5) | SMN `0x59B08 + 4*i` | Family `19h/61h`, `1Ah/44h` | Verified |

**Formulas:**
- **CPUID identification:** CPUID `1` EAX (`family = base + ext`, `model = (ext << 4) | base`).
- **Base clock:** 17h/19h = `(CpuFid[7:0] / CpuDfsId[13:8]) * 200 MHz`; 1Ah = `CpuFid[11:0] * 5 MHz`.
- **Active clock:** `(ΣΔAPERF / ΣΔMPERF) * base` across logical processors (excludes idle C-states, so higher than HWiNFO Effective Clock `ΔAPERF/Δt`; discard round on wrap/backwards delta).
- **Package power:** `ΔPKG_ENERGY_STAT * 0.5^ESU / Δt` (unsigned 32-bit delta).
- **Tctl:** `CUR_TEMP[31:21] * 0.125 °C`, minus `49 °C` if `RANGE_SEL[19] == 1` or `TJ_SEL[17:16] == 3`.
- **CCD:** `raw[11:0] * 0.125 - 305 °C` (`0` = absent, `≥ 125 °C` = invalid). Hold `Global\Access_PCI` around SMN reads only.

**Measured (Ryzen 5 5600 / Ryzen 7 5700X idle):** CPUID `19h/21h`, P0 `3500 MHz`, `ESU=16` (`1.53e-5 J`), Tctl `43–44.4 °C`, CCD1 `39.0 °C`, Package `27.3–34.0 W` (matches HWiNFO `CPU Package Power`; Adrenalin reports core-only ~`5.9 W`), Active clock `~3.8 GHz` (matches HWiNFO Core Clock; Effective Clock is ~`149 MHz`).

## Intel

Sources include LHM `IntelCpu.cs` and Intel SDM Vol. 4. See [D-084](../decisions/D-084.md) for the choice of `IntelMSR`. Status is **Unverified** on hardware (AMD test machine).

| Name | Address | Purpose |
| --- | --- | --- |
| `IA32_MPERF` / `IA32_APERF` | MSR `0xE7` / `0xE8` | C0 base / actual clock counters |
| `MSR_PLATFORM_INFO` | MSR `0xCE` | Max non-turbo ratio `[15:8]` (× `100 MHz` on Sandy Bridge+) |
| `IA32_TEMPERATURE_TARGET` | MSR `0x1A2` | `TjMax [23:16]` (read once; if 0 or failed, hide temp) |
| `IA32_PACKAGE_THERM_STATUS` | MSR `0x1B1` | Valid `[31]`, distance to TjMax `[22:16]` (needs CPUID 6 EAX `[6]`) |
| `MSR_RAPL_POWER_UNIT` / `MSR_PKG_ENERGY_STATUS` | MSR `0x606` / `0x611` | `ESU [12:8]` (`0.5^ESU J`; `2^ESU µJ` on Silvermont/Airmont Atom `37h/4Ah/4Dh/5Ah/5Dh/4Ch`) |

Package temp = `TjMax - [22:16]` (no TCC offset `[29:24]`, matching LHM/`coretemp`). No shared mutex required.

## Verification

Validate decoding with `tests/sensors_decode_test.cpp`. On matching hardware, compare package power and temperature with HWiNFO and load with Task Manager. Compare active clock using its C0-only definition above. Report physical verification through the [hardware report form](https://github.com/cheng-zongkai/Pitot/issues/new?template=hardware_report.yml).
