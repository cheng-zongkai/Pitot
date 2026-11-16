# PawnIO Modules

Officially signed PawnIO modules, embedded as resources in `Pitot.exe` (D-004, D-028). The driver verifies the signature when loading, so the modules cannot be modified.

| File | SHA-256 |
| --- | --- |
| `AMDFamily17.bin` | `DAE74615761B78BDF064DFB3E136252DDCC6FC727D88F14738D0E5800D427A91` |
| `IntelMSR.bin` | `D6ED85D65AB17A22F813EF98207D6D537155EE2DED5976A21CB48413C9B92E5F` |
| `LpcIO.bin` | `B3896A1CAB0D808FCA31FE2EBCAE045D59DAC690DA87B17C858BB8DA357EB45E` |

- Source: <https://github.com/namazso/PawnIO.Modules/releases/tag/0.2.11> (`release_0_2_11.zip`, SHA-256 `43608CB89BC84247FEF1368A139013F7D043E17DB6D6C8DFC9B46BF0905A81F4`).
- Byte-for-byte identical to the files of the same name in the LibreHardwareMonitor repository under `LibreHardwareMonitorLib/Resources/PawnIo/` (checked 2026-11-04).
- `IntelMSR.bin` was taken from the same release zip on 2026-11-11 (zip hash checked against the one above; `AMDFamily17.bin` and `LpcIO.bin` in it are byte-for-byte the files already here).
- License: LGPL-2.1-or-later; the full text is in `COPYING` (same text as upstream, only the line endings differ). Copyright © 2023–2026 namazso.

## Corresponding source (LGPL-2.1 sections 4 and 6)

`src/` holds the complete source of these three modules at the upstream 0.2.11 tag, unmodified (downloaded 2026-11-04, D-033; `IntelMSR.p` added 2026-11-11, D-084):

- `AMDFamily17.p`, `IntelMSR.p`, `LpcIO.p`: the modules themselves;
- `include/*.inc`: the headers they reference;
- `.github/workflows/ci.yml`: how upstream builds them (`pawncc <module>.p -iinclude -C64 -;+ -(+ -p`, with the compiler being Pawn 4.1.7152 from upstream `_pawn/`);
- `README.md`: the upstream description.

Upstream signs the compiled `.amx` and publishes it as `.bin`. The signing private key is not part of the source, and a module you build yourself will not be accepted by the official driver; this is a limitation of the PawnIO driver, not something Pitot adds.
