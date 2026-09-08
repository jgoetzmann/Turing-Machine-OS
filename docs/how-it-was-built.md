# How it was built

TuringOS was built with heavy AI assistance, from a written specification, against tests. This page says what that meant in practice, so nobody has to guess.

## The idea

The project started as a proof of concept: take the parts of a Turing machine (tape, head, finite control, transition function) and make each one a real, inspectable part of an operating system instead of a metaphor. A true Turing machine has an infinite tape and one transition per step; this one has a finite tape (32K-64K), an Intel 8080 as its head, a six-state kernel as its control, and an environment (console, keys, disks) behind a single host boundary. Every place where it departs from the formal model is listed in `architecture.md` §1 and justified in `decisions.md`.

## Version 1

The first version was scaffolded by an AI coding agent working from a spec, a progress file and a "remember" file kept under `.cursor/`. It produced an 8080 emulator, a BIOS behind `OUT 01H`, a CP/M-style filesystem, a tiny-C compiler, and a shell written in that C subset and running on the emulated CPU. That shell was the strongest thing in the repository then, and still is. It also produced a separate Python viewer that polled snapshot files, and 29 tests.

An audit on 2026-09-07 (`v2-roadmap.md` §1.2) found the honest gaps: the "test programs" printed their answers with `puts`, `int` was 8 bits, the dirty-page map never set a bit, `KS_IDLE` was unreachable, half the memory map was fiction, the compiler kept a scratch byte inside the program's own address space, and the BIOS blocked on `getchar()`. The agent workflow files were the only architecture documentation, and they no longer described the code.

## Version 2: spec first, then many builders, then tests

v2 was built in one run with a fixed procedure:

1. The spec was frozen first. `docs/v2-roadmap.md` set the goals and acceptance criteria (`WSn-mm` ids), and the frozen headers under `src/` (`tos.h`, `hal.h`, `mem.h`, `cpu.h`, `kernel.h`, `bios.h`, `fs.h`, `api.h`, the language and compiler headers) fixed every constant, struct layout, function signature and error string before a line of implementation existed. `decisions.md` B1-B16 record the choices the spec made and why.
2. Builders ran in parallel. The work was split into vertical slices (emulator, kernel, BIOS + HAL, filesystem, compiler, each language, the shell, the demos, the web engine, the panels, the tools, the docs), and each slice was written in isolation by an AI agent that was forbidden to run the compiler, read other slices, leave stubs, or ask questions. Agents were told to duplicate helpers rather than coordinate, and to import what they wished existed.
3. The slices were then reconciled against tests. Tests were written from the spec and cited behavior ids (`TEST("WS1-06: transition table", …)`), and the first build found every mismatch at once. Where two slices had written the same thing, the one that passed more behaviors survived; the other was deleted.
4. The honesty rule: every claim on the site is backed by a test or links to a line of code. The architecture page's constants table is generated from `src/tos.h` and checked in CI; every demo has an expected-output or golden-frame test; the FSM diagram is drawn from the kernel's own transition table.

The repository you are reading is the result of that run plus the ordinary human work of reading, fixing and deciding.

### By the numbers

The v2 run, 2026-09-07: 14 builder slices and 5 spec-testers launched at once; ~22,000 lines written blind. First contact: every C file in `src/` compiled against the frozen headers with one comment nit as the only error, and the new compiler compiled the new shell on its first try (2,614 bytes, down from 5,115). First run of the suite: 40 of 57 tests passed; the 17 failures were legacy tests asserting v1 behaviour the spec had changed (DMA default, help text, exit line, 8-bit return values), two test bugs, and three real defects (an 8-bit input limit in the TM compiler, an unpinned "label state" rule, Brainfuck inheriting a previous program's cells). Final: 77 native tests and 54 web tests green, every one of the 60 behavior ids cited by a test, ~39 M instructions/s natively, a 117 KB wasm. Four independent auditors then re-checked all 99 roadmap items read-only: 42 done outright, 56 with a stated sub-clause unmet, 1 not done; the fixes that were cheap were made the same day and the rest are written next to the items in `v2-roadmap.md`. Three of the twelve subagent runs died mid-flight to API limits and were redone by hand. The spec made that possible.

### The audit that followed, 2026-09-07

Ten read-only auditors then went subsystem by subsystem, looking for behaviour that contradicted the 8080,
the spec or the documentation. Every finding was handed to a second agent whose job was to refute it by
reproduction. Of 77 findings, 13 did not survive that and 64 did. The ones that mattered most were in code
that had tests: the auxiliary carry was inverted on every subtraction, `tos_seek` could rebuild a machine out
of the wrong program image, the compiler crashed on deeply nested expressions, a deleted file never reached
the disk, and "Step over syscall" in the browser could never finish, because the state it waited for is not
observable between steps. Several tests turned out not to test what their names claimed: the flag assertions
encoded the wrong 8080 rule, the "never sleeps" test measured processor time, and nothing anywhere made a
breakpoint fire. Cypress now drives the site in a headless browser so the panels are covered by something
other than a screenshot. The fixes and the reasoning are in `decisions.md` B24-B28.

## What the machine's determinism bought

Because the core never blocks and never reads a clock (`decisions.md` B2), the whole machine is a pure function of its program, its input log, its seed and its levers. That property is what makes time travel (`tos_seek`), the two-machines-agree test (WS1-16), the browser build (B1) and the byte-identical demo tests possible. It was the first thing the v2 spec fixed, and everything else followed from it.

## Tools used

- C99 with `-Wall -Wextra -Werror -pedantic`, `make`, a data-driven test runner of about 40 lines (`tests/run_tests.sh`).
- Emscripten 6.0.9 for the WebAssembly build; Vite and TypeScript for the site; `node --test` for the web tests; no runtime JavaScript dependencies.
- GitHub Actions for CI (ubuntu, macos, sanitizers) and for deploying the site to GitHub Pages.
- Python 3 (standard library only) for `tools/tm_ref.py`, the reference interpreter the TM compiler is checked against.
- AI coding agents for drafting, guided by the spec and the tests, with the conventions in `CLAUDE.md`. Commit messages carry no tool attribution; the log records what changed and why.

## Where to go next

`status.md` says what works today and what does not. `v2-roadmap.md` is the plan this was built from and is kept for the record; anything aspirational lives there, not in the other docs.
