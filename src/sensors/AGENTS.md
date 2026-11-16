# src/sensors · Hardware Access Rules

Before working in this directory, read `docs/hardware.md` (registers, protocols, decoding formulas and their sources, measurements on this machine). The rules in the root `AGENTS.md` apply as well.

## Coexistence with FanControl

| Access | Global mutex to hold first |
| --- | --- |
| SMN | `Global\Access_PCI` |
| Super I/O / EC | `Global\Access_ISABUS.HTP.Method` |

- Wait at most 10 ms; if it cannot be acquired, skip the round and keep the previous reading. Do not retry and do not block the sampling thread.
- Hold the lock as briefly as possible and release it as soon as the read is done.
- When the EC is busy, do not take it over.

## Other

- Identify the CPU by CPUID family/model, never by a hard-coded model.
- New register, MSR or SMN addresses: define named constants in `decode.h` and add an entry with its source to `docs/hardware.md`; check decoding formulas against LibreHardwareMonitor first, and do not write them from memory.
- LibreHardwareMonitor (MPL-2.0) is consulted for facts only; write your own code.
- `adlx_abi.h` is the only interface between `Pitot.exe` and `PitotAdlx.dll`; it holds custom C types only and includes no ADLX header.
- NVIDIA extended metrics go through `PitotNvidia.dll` (`src/nvidia/`), which calls `nvml.dll` and, only for a missing fan or memory temperature, the public `nvapi64.dll` entries. It must not call `nvmlDeviceSet*` or any unpublished NVAPI id. `Pitot.exe` does not link either DLL (D-081).
- Every port or register write Pitot performs is listed in `docs/hardware.md`, "Writes Pitot performs"; add any new one there (D-085).
- When a metric is unsupported or unreadable, return an "unavailable" state, not 0.
- New or changed decoding logic needs unit tests (`tests/`), because that part can be tested without administrator rights.
