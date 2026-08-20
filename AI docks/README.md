# AI docks

Design and research documentation for **XYORAS Access**, split by concern.
`../CLAUDE.md` is the entry point and points here.

Each file is meant to be readable on its own. Anything marked `UNVERIFIED` has
not been confirmed on real hardware or against a real game build — treat it as
a lead, not a fact.

| # | File | Covers |
| --- | --- | --- |
| 01 | [Project overview](01-project-overview.md) | Goals, scope, non-goals, who this is for |
| 02 | [Accessibility design](02-accessibility-design.md) | What the player hears, controls, speech UX |
| 03 | [Target games](03-target-games.md) | Title IDs, versions, regions, XY vs ORAS differences |
| 04 | [Gen 6 reverse engineering](04-gen6-reverse-engineering.md) | RomFS, GARC, PK6, RAM layout, tooling |
| 05 | [Plugin architecture](05-plugin-architecture.md) | 3GX/CTRPF, hooks, threads, module design |
| 06 | [TTS + audio pipeline](06-tts-audio-pipeline.md) | eSpeak NG, BCWAV, CSND, latency budget |
| 07 | [Build environment](07-build-environment.md) | Toolchain, dependencies, build scripts |
| 08 | [Repo layout](08-repo-layout.md) | Where every kind of file belongs |
| 09 | [Coding standards](09-coding-standards.md) | Language rules, naming, error handling |
| 10 | [Testing and QA](10-testing-and-qa.md) | Emulator, hardware, and user testing |
| 11 | [Roadmap](11-roadmap.md) | Phased plan and current status |
| 12 | [Research log](12-research-log.md) | Sources, findings, dead ends |
| 13 | [Glossary](13-glossary.md) | Terms and acronyms |
| 14 | [Legal and licensing](14-legal-and-licensing.md) | GPL obligations, what must never be committed |
