# Project Documentation

Contributor and maintainer guides for Pitot. For project overview and installation, see [README.md](../README.md); for contribution rules and PR workflow, see [CONTRIBUTING.md](../CONTRIBUTING.md); for agent instructions, see [AGENTS.md](../AGENTS.md).

## Document Map

| Document | Scope | Update when |
| --- | --- | --- |
| [architecture.md](architecture.md) | Product spec: surfaces, data sources, threads, modules, risks, milestones | Requirements or module structure change |
| [configuration.md](configuration.md) | Every user setting, defaults, and `config.ini` format | A setting or INI key is added or changed |
| [hardware.md](hardware.md) | Registers, MSRs, SMN/Super I/O protocols, decoding formulas, measurements | Hardware paths or verified measurements change |
| [design.md](design.md) | UI and copy rules plus the Chinese–English glossary | UI rules or terminology change |
| [environment.md](environment.md) | Toolchain, build/test commands, release packaging, demo screenshots, resource measurement | Build setup, release process, or developer workflow changes |
| [gotchas.md](gotchas.md) | Known Win32, DWM, PDH, ETW, and tooling pitfalls | A new non-obvious trap is hit or resolved |
| [decisions/](decisions/README.md) | Architecture Decision Records (`D-NNN.md`) for lasting technical trade-offs | An architectural, licensing, or protocol decision is made |
| [progress.md](progress.md) | Current status, unverified hardware paths, and next steps | Milestones, blockers, or verification status change |

## Conventions

- **Single source of truth:** Keep each fact in one file and link to it elsewhere.
- **Language:** Write documentation in English and code comments in Simplified Chinese. Quote Chinese UI strings literally when referenced.
- **Validation:** Run `scripts/check-docs.ps1` after editing documentation to verify line budgets, relative links, ADR index consistency, and language rules.
