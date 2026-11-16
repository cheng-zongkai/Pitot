# Third-Party Notices

Pitot's own source code is under the MIT license (`LICENSE` in the repository root). The third-party works below are not covered by the MIT license and keep their original licenses:

- The PawnIO modules embedded in `Pitot.exe` are LGPL-2.1-or-later.
- The optional `PitotAdlx.dll` uses the AMD ADLX SDK (AMD proprietary license). The SDK is not in this repository; the built `PitotAdlx.dll` is distributed under the end-user agreement that the AMD license requires (`PitotAdlx-EULA.txt`), not under MIT.
- The optional `PitotNvidia.dll` is Pitot's own code (MIT). It calls `nvml.dll` and `nvapi64.dll` from the installed NVIDIA driver at run time. Neither DLL is distributed with Pitot, and no NVIDIA header is in this repository (D-081).
- The Windows installer is built with Inno Setup, which embeds its own setup program under the Inno Setup License (D-091).

The third-party works that Pitot uses or consults, with their licenses and how each is handled, are listed below (D-033). Whenever a third-party file, piece of code or reference source is added, check its license first and then update this file.

## Distributed with Pitot.exe

### PawnIO modules `AMDFamily17`, `IntelMSR` and `LpcIO`

- Version: PawnIO.Modules 0.2.11 (officially signed), embedded as resources in `Pitot.exe`.
- Copyright: Copyright © 2023–2026 namazso &lt;admin@namazso.eu&gt;.
- License: GNU Lesser General Public License v2.1 or later; full text in [`third_party/pawnio/COPYING`](third_party/pawnio/COPYING).
- Corresponding source: [`third_party/pawnio/src/`](third_party/pawnio/src/), unmodified; upstream <https://github.com/namazso/PawnIO.Modules/tree/0.2.11>.
- Note: the modules are standalone programs handed to the PawnIO driver for execution; Pitot does not modify them and passes them to the driver as they are.

### phnt

- Use: the structure layouts of `SYSTEM_PROCESS_INFORMATION` and `SYSTEM_MEMORY_LIST_INFORMATION` in `src/sampler/nt_system.h` are taken from phnt's `ntexapi.h`.
- Source: <https://github.com/winsiderss/phnt>
- License: MIT License

```text
MIT License

Copyright (c) 2022 Winsider Seminars & Solutions, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Distributed with PitotAdlx.dll (optional component)

### AMD Device Library eXtra (ADLX) SDK

- Use: only in the optional `PitotAdlx.dll`, to read the hotspot temperature, VRAM temperature, board power and fan speed of AMD graphics cards. `Pitot.exe` contains and links no part of ADLX, and runs normally without this DLL.
- License: **AMD ADLX SDK License Agreement** (an AMD proprietary license, not an open-source license).
- Handling:
  - The SDK's header files and sample code are **not in this repository** and have not been copied into the source. Whoever needs them downloads them with `scripts/get-adlx.ps1 -AcceptAmdLicense` into the git-ignored `third_party/adlx/`; downloading means accepting AMD's agreement.
  - `PitotAdlx.dll` is distributed only in compiled form, together with the end-user agreement required by section 3 of the agreement: [`PitotAdlx-EULA.txt`](PitotAdlx-EULA.txt), written from Schedule B of the AMD agreement, which the release installer and zip place next to the DLL. The agreement covers only that DLL. The DLL is built on the maintainer's machine and packed into the installer and zip with `scripts/repack-adlx.ps1`; the release workflow does not download the SDK.
  - The `amdadlx64.dll` called at runtime is installed with the AMD graphics driver; Pitot does not distribute it.

## Distributed with the installer

### Inno Setup

- Use: `Pitot-<version>-setup.exe` is built with Inno Setup 6.7.3 (<https://jrsoftware.org/isinfo.php>), which embeds its setup and uninstall program in the installer (D-091). The Simplified Chinese wizard text is Inno Setup's unofficial `ChineseSimplified.isl` translation, maintained by Zhenghan Yang, from the same Inno Setup source tag.
- Copyright: Copyright (C) 1997-2026 Jordan Russell. All rights reserved. Portions Copyright (C) 2000-2026 Martijn Laan. All rights reserved.
- License: [Inno Setup License](https://jrsoftware.org/files/is/license.txt). It allows use for any purpose, including commercial applications, provided the copyright notices and web site addresses in the binaries are kept. Pitot does not modify Inno Setup; its files are not in this repository and are downloaded with pinned hashes by `scripts/get-innosetup.ps1`.

## Runtime dependencies, not distributed with Pitot

### NVIDIA Management Library (`nvml.dll`)

- Use: only from the optional `PitotNvidia.dll`, to read hotspot temperature, memory temperature, board power and fan speed of NVIDIA graphics cards (D-081). `Pitot.exe` does not call it and runs normally without `PitotNvidia.dll` or without an NVIDIA driver.
- The library is installed by the NVIDIA graphics driver (`System32\nvml.dll`, or `%ProgramW6432%\NVIDIA Corporation\NVSMI\nvml.dll`). The same DLL also calls `System32\nvapi64.dll` for the public tachometer and thermal-settings calls when NVML does not supply fan RPM or memory temperature (D-081). Pitot does not distribute either DLL.
- The public NVAPI headers `nvapi.h` / `nvapi_interface.h` are MIT (<https://github.com/NVIDIA/nvapi>). They were read for six function ids (initialize, unload, enumerate GPUs, PCI identifiers, tachometer, thermal settings) and the thermal-settings layout. The headers are not copied into this repository; the declarations in `src/nvidia/pitot_nvidia.cpp` and `src/sensors/nvidia_decode.h` are Pitot's own. Their notice is reproduced here all the same:

```text
SPDX-FileCopyrightText: Copyright (c) 2019-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

- The NVML header `nvml.h` 13.4.92 (Copyright 1993-2026 NVIDIA Corporation, all rights reserved) was read for function names, numeric ids and struct layouts. Its notice grants a nonexclusive, royalty-free license to use the code in individual and commercial software, and requires that any such use include NVIDIA's disclaimer and U.S. Government End Users notice in the user documentation and in the code comments. The header was not copied into this repository or into `PitotNvidia.dll`, and the declarations in `src/nvidia/pitot_nvidia.cpp` and `src/sensors/nvidia_decode.h` are Pitot's own; because they follow that header's interface, the two notices are included here and in the comments of `src/nvidia/pitot_nvidia.cpp`:

```text
NVIDIA MAKES NO REPRESENTATION ABOUT THE SUITABILITY OF THIS SOURCE
CODE FOR ANY PURPOSE.  IT IS PROVIDED "AS IS" WITHOUT EXPRESS OR
IMPLIED WARRANTY OF ANY KIND.  NVIDIA DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOURCE CODE, INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL,
OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
OF USE, DATA OR PROFITS,  WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
OR OTHER TORTIOUS ACTION,  ARISING OUT OF OR IN CONNECTION WITH THE USE
OR PERFORMANCE OF THIS SOURCE CODE.

U.S. Government End Users.   This source code is a "commercial item" as
that term is defined at  48 C.F.R. 2.101 (OCT 1995), consisting  of
"commercial computer  software"  and "commercial computer software
documentation" as such terms are  used in 48 C.F.R. 12.212 (SEPT 1995)
and is provided to the U.S. Government only as a commercial end item.
Consistent with 48 C.F.R.12.212 and 48 C.F.R. 227.7202-1 through
227.7202-4 (JUNE 1995), all U.S. Government End Users acquire the
source code with only those rights set forth herein.
```

### PawnIO driver

- Pitot can offer to download the official installer from <https://github.com/namazso/PawnIO.Setup/releases/latest/download/PawnIO_setup.exe>, the link published by <https://pawnio.eu/>. The signed official distribution is provided by namazso under its own terms; the setup repository does not publish an installer source license. Pitot does not bundle or redistribute the installer, copy its code, or bypass its interactive installation UI.

- Installed by the user (<https://pawnio.eu>). License: GNU GPL v2.0 or later, with a special exception: standalone programs that communicate with PawnIO only through the device I/O control interface are not bound by the GPL. Pitot communicates with it only through `DeviceIoControl`.
- Pitot's calling protocol (IOCTL numbers, buffer layouts) was implemented independently with reference to the PawnIOLib source; PawnIOLib is neither used nor copied.

## References (no code copied)

### LibreHardwareMonitor

- Source: <https://github.com/LibreHardwareMonitor/LibreHardwareMonitor>, license MPL-2.0.
- Use: checking register addresses, decoding formulas and the EC access sequence (each source is recorded in `docs/hardware.md`). Pitot's code is independently written C++ and does not copy or translate LHM's source line by line.

### Anthropic `frontend-design` skill

- Source: <https://github.com/anthropics/skills/tree/main/skills/frontend-design>; license: [Apache License 2.0](https://github.com/anthropics/skills/blob/main/skills/frontend-design/LICENSE.txt).
- Use: reference for UI layout and copy principles. The skill was downloaded only into the local agent skills directory, was not placed in the project repository or distributed with the product; the project's UI code is written independently.

### Microsoft Windows control design documentation

- Source: <https://github.com/MicrosoftDocs/windows-dev-docs>; documentation license CC BY 4.0, sample code license MIT.
- Use: reference for the look and interaction principles of controls such as the color palette and sliders; no sample code or documentation content was copied.

### Microsoft WinUI theme resources

- Source: [microsoft/microsoft-ui-xaml](https://github.com/microsoft/microsoft-ui-xaml), [MIT License](https://github.com/microsoft/microsoft-ui-xaml/blob/main/LICENSE).
- Use: reference for the color values of the Mica content and card layers; no WinUI code is introduced, linked or distributed.

### Others

- Microsoft Windows API documentation: [MicrosoftDocs/sdk-api](https://github.com/MicrosoftDocs/sdk-api), documentation license [CC BY 4.0](https://github.com/MicrosoftDocs/sdk-api/blob/docs/LICENSE). Used to check MIB_IF_ROW2 byte counters and ETW buffer delivery semantics; only interface facts were consulted, and no code or documentation passages were copied.

- The Windows SDK headers and import libraries are used under Microsoft's SDK license.
- The UI fonts (Segoe UI Variable, Segoe Fluent Icons) and process icons are taken at runtime from the system and from the respective programs, and are not distributed with Pitot.
