# Security Policy

## Supported Versions

Security fixes are applied to the latest state of the `main` branch and included in subsequent releases.

## Security Model

Pitot requires administrator privileges (elevated launch or the `\Pitot\Autostart` scheduled task) to access hardware sensors via PawnIO, kernel ETW sessions, and memory list diagnostics. Because of this privilege boundary:

- Third-party driver DLLs (`nvml.dll`, `nvapi64.dll`, `amdadlx64.dll`) are loaded only from `System32` or their verified driver directory, never via the default DLL search path.
- The optional PawnIO installer download verifies HTTPS transport and Authenticode signatures before launching the official installer.
- Settings edits in `Pitot.exe --settings` and IPC via `Local\Pitot.Sensors` avoid executing external commands from untrusted inputs.

## Reporting a Vulnerability

**Please do not report security vulnerabilities through public GitHub issues.**

Instead, report them privately using GitHub Security Advisories (**Security -> Advisories -> Report a vulnerability** on the repository page).

Include as much of the following as possible:

- Description of the issue and affected component (`Pitot.exe`, `PitotAdlx.dll`, `PitotNvidia.dll`, PawnIO integration, or autostart task)
- Steps to reproduce or proof of concept
- Windows 11 build and privilege context

## Out of Scope

- Requirements inherent to elevated local monitoring (for example, requiring administrator rights to read MSRs or kernel ETW)
- Kernel anti-cheat software blocking the third-party PawnIO driver
