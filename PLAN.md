# JavaScriptCore PPC64LE (POWER8+) JIT & WASM Port — Plan

## Goal

Bring JavaScriptCore's JIT tiers and WebAssembly engine up on **PPC64 little-endian**. **Baseline ISA: Power ISA v2.07** (POWER8). POWER9/POWER10-only opcodes (Power ISA v3.0/v3.1 additions: `darn`, VSX3, `lxvll`/`stxvll`, byte-reverse VSX, etc.) are **optional fast paths** — every such use must be guarded by a runtime capability check or compile-time feature flag and must have a v2.07-only fallback. "Working" means `jsc` shell runs the JSTests stress suite at JIT and FTL tiers, and the WASM BBQ/OMG tiers pass wasm conformance, on a POWER8 machine with no POWER9+ instructions executed.

Non-goals for v1: 32-bit PPC, big-endian, AIX, ARM64E-style pointer authentication.

## Target environment

| Purpose | Host | Role |
|---|---|---|
| Primary dev/test | `tle@192.168.1.247` | PPC64LE, build + run the new port |
| Reference oracle | `tle@192.168.64.3` | ARM64, cross-check JIT behavior on identical inputs |
| Workstation | local macOS | edit, agent loop, git |

Workflow: edit locally, push to a branch, pull on the PPC64 box, build + run. Use the ARM64 box to produce golden outputs (bytecode dumps, DFG IR, test results) when something behaves oddly on PPC64 — `jsc --dumpBytecode=1`, `--dumpDFGGraph=1`, `JSC_dumpAirGraph=1` are the primary observability levers. Always diff PPC64 output against ARM64 output for the **same jsc binary flags on the same input**.

## ISA references (keep open)

- **Power ISA v2.07B** — POWER8 spec, **the baseline we target and the primary source of truth** for every instruction we emit unconditionally. Every opcode added to `PPC64Assembler` must be present in v2.07B unless gated behind a POWER9+ feature check.
- **Power ISA v3.0B** — POWER9 additions. Only consult when implementing a **guarded** fast path. Any use must cite the v3.0B section in the source comment AND have a v2.07 fallback.
- **Power ISA v3.1** — POWER10 additions (prefixed instructions, MMA). Out of scope for v1; do not emit.
- **ELFv2 ABI (OpenPOWER)** — PPC64LE calling convention, register save/restore, TOC/PLT, red zone (288 bytes below r1).
- **64-bit ELF V2 ABI Specification: Power Architecture, Rev 1.5**.
- `/usr/include/linux/elf.h` + `<asm/ptrace.h>` on the test box for signal context / MachineContext.

Rule: do not write a single PPC instruction encoding from memory. Every opcode goes via a citation in PLAN notes or inline comment pointing to an ISA section — **and that citation must be Power ISA v2.07B** unless the opcode is behind a POWER9+ feature gate. Every calling-convention choice cites ELFv2.

## Architecture survey (what JSC needs per-arch)

1. **Detection** — `Source/WTF/wtf/PlatformCPU.h` already defines `WTF_CPU_PPC64` / `WTF_CPU_PPC64LE`; `Source/cmake/WebKitCommon.cmake` + `WebKitFeatures.cmake` need a PPC64LE branch (mirror the `WTF_CPU_RISCV64` pattern at `WebKitFeatures.cmake:113`).
2. **Assembler** — `Source/JavaScriptCore/assembler/`: new `PPC64Assembler.h` (ISA encoder) + `PPC64Registers.h` + `MacroAssemblerPPC64.h/.cpp` wired into `MacroAssembler.h` and `TargetAssemblerDefinitions.h`.
3. **LLInt offlineasm** — `Source/JavaScriptCore/offlineasm/ppc64le.rb` backend; register it in `backends.rb` and `Source/JavaScriptCore/CMakeLists.txt:278` (the `OFFLINE_ASM_BACKEND` switch).
4. **Baseline JIT / DFG** — mostly architecture-agnostic on top of MacroAssembler; the 64-bit paths in `dfg/DFGSpeculativeJIT64.cpp` light up once MacroAssemblerPPC64 is solid.
5. **B3 / Air** — `Source/JavaScriptCore/b3/air/`: per-arch register info, `AirCCallingConvention` specialization, `AirOpcode.opcodes` entries, and any arch-specific lowering. Once Air supports PPC64, both **FTL and WASM OMG** light up together.
6. **WASM** — BBQ (`WasmBBQJIT.cpp`) piggybacks on MacroAssembler; OMG piggybacks on B3/Air. `WasmCallingConvention.cpp` needs a PPC64 ABI block.
7. **Runtime glue** — `MachineContext.h` (signal unwinding; PPC already has an entry), `FastTLS`, probe trampolines, and JIT cage / executable-memory allocation on Linux PPC64.

**Primary reference: ARM64.** ARM64 is by far the most mature and exercised 64-bit backend in JSC — it covers every JIT tier, WASM tier, SIMD, concurrent JIT, the full Probe API, and is the platform Apple ships. When a design question comes up ("how does this interact with OSR exit?", "what does the calling convention look like for a slow-path call?", "how is the probe frame laid out?"), the ARM64 code is the authoritative answer. Every phase starts by reading the ARM64 equivalent file and understanding what it does before writing PPC64 code.

**Secondary reference: RISCV64.** It is the most recent clean new-arch introduction, so its **shape** (how new files are structured, what the minimal set of new CMake/offlineasm/MacroAssembler entries looks like, what can be stubbed vs must be real) is the best template for "what does a new port look like in 2025." Use RISCV64 to answer "what do I need to add?" — then use ARM64 to answer "what does the addition need to do?" If RISCV64 and ARM64 disagree on approach, follow ARM64 unless there's a specific reason the RISCV64 path fits PPC64 better (e.g. both are bare non-Apple Linux ports, so page size / JIT cage / signal trampoline bits may track RISCV64 more closely than ARM64).

Divergences to expect: PPC has condition registers, link register separate from GPRs, LR/CTR indirect-branch pair, TOC pointer in r2, and relaxed memory ordering requiring explicit `lwsync`. ARM64 shares none of these, RISCV64 shares none of these. These are the places where PPC64 will need genuinely original code, not a port.

## Lessons from the Firefox SpiderMonkey PPC64 port (hard-won)

A working PPC64 JIT was recently completed for SpiderMonkey (Firefox 151+, bug 1860412) and is documented in `~/Work/firefox/PLAN.md`. The bugs it hit are almost all **architecture-level** (not SpiderMonkey-specific), so they will show up in JSC too. Read this section before writing PPC64 code. Each item below is a concrete trap with a known workaround.

### Scratch-register discipline (the top source of pain in the SM port)

PPC64's scratch register story is more constrained than ARM64's. A careless borrow of a scratch mid-sequence is the single most common source of silent miscompiles.

- **r0 is not a general-purpose scratch.** In many encodings (D-form loads/stores, `addi`), r0 means "literal zero" instead of "read register 0." Document this in `PPC64Registers.h`: `r0` is non-allocatable as a base register in load/store; the JIT can use it freely only for non-base operands. Every MacroAssembler method that takes a base register must assert it is not r0 in debug builds.
- **Establish a fixed scratch pool early.** SM's convention: `r11` + `r12` = primary scratch pool (two regs always available), `r16` = secondary "saved scratch" (pool base register for constant pool loads). Adopt the same split and document it in `PPC64Registers.h`. When a macro needs a third scratch, the caller saves to the red zone or stack — don't borrow silently.
- **`computeConditionCode` on the POWER8 overflow path clobbers r0.** This is an actual SM bug that survived into production and is tracked but deferred there. Refactor the overflow sequence (`mfxer + rlwinm + mtcrf`) to route through the scratch pool, not r0. Worth fixing in JSC from day one.
- **IC machinery borrowing scratch regs needs audit.** SM's `EmitCallIC` uses `R2.scratchReg()` and the safety of that is "plausibly safe, needs deeper audit" — it's an open item. JSC's equivalent (`Repatch.cpp` IC patching) must explicitly document which registers are guaranteed live across an IC call and which are scratch.
- **SIMD splats clobber source GPRs.** `splatX{8,16}` clobbers its source GPR on POWER8. Any variable-shift Int8/Int16 caller must `mr` (move) into a scratch before splatting. This is a footgun for any SIMD op that reuses a source operand.
- **`replaceLaneInt32x4` POWER8 fallback needs an extra GPR scratch.** If the caller already holds both r11 and r12, the macro asserts. Either take the scratch as a parameter or document the callers that must free a register before invoking.
- **Dest-aliasing matrix for every destructive op.** For every SIMD or GPR destructive helper, write test cases covering all four aliasing configurations: no-alias, `dest == lhs`, `dest == rhs`, `lhs == rhs`. The SM port's argon2 miscompile came from handling only `dest == lhs`. Prefer native PPC forms that are naturally non-destructive (e.g. `vmul{e,o}{u,s}w`) so the register allocator has freedom.

### SIMD / VSX / VMX traps (Power ISA v2.07 baseline)

- **`stxvx` vs `stfd` LE byte order.** On PPC64 LE, `stxvx` places a scalar double at bytes 8-15; `stfd` at bytes 0-7. For scalar FP save/restore (including probe frames and bailout stacks), **always use `stfd`/`lfd`, never `stxvx`/`lxvx`**. Cross-kind reads of the same slot (e.g. the B3 spill slot interpretation) depend on this.
- **VMX vs VSX register-space confusion.** VMX instructions address VR0-31 (= VSR32-63); FPRs are VSR0-31. A Simd128 encoded in VSR0-31 will need a VR-staging round-trip for every VMX op. Decide the encoding early: SM moved Simd128 into the VR namespace (VSR32-63) in Phase 2 and saved ~80 dynamic instructions across extmul/dot/widen. **Do the same in JSC from the start.**
- **`ScratchSimd128Reg` must be distinct from `ScratchDoubleReg`.** SM's post-Phase-2 convention: `ScratchDoubleReg = f0`, `ScratchSimd128Reg = v0`. They are different physical registers. Do not alias them; scalar FP X/A-form ops (`fcfid`, `fctiwz`, `lfdx`, `fcmpu`) encode only a 5-bit FRT/FRB and will corrupt the opcode if given a Simd128-encoded register. Bridge scalar↔SIMD via `ScratchDoubleScope` + `xxlor` (XX3-form).
- **`xxsel XT,XA,XB,XC` is `(XA & ~XC) | (XB & XC)` — opposite of the intuitive reading.** Wrap this in a named helper the first time you use it. Wasteful to rediscover.
- **`xxpermdi DM`: DM bit 0 selects XA dword for result DW0; DM bit 1 selects XB dword for result DW1.** DM=2 swaps both halves. Document the table; do not reason from the spec under time pressure.
- **PPC64 LE inverts VMX "high"/"low".** `vupkhsb` unpacks the LOW wasm lanes on LE; `vupklsb` unpacks the HIGH. VMX naming is big-endian by spec — **all JSC code comments must use wasm-lane-direction naming, not VMX-spec naming.**
- **`splatX4` only fills word elements.** Byte/halfword shifts need `splatX16`/`splatX8`; substituting is a silent miscompile.
- **Red-zone stash pattern.** ELFv2 reserves 288 bytes below SP. Three SM SIMD helpers (`extAddPairwiseInt*`, `dotInt8x16Int7x16ThenAdd` 4-arg, `swizzleInt8x16`) use 16-32 bytes of red zone when a proper temp would be a bigger cross-file change. Acceptable workaround but must be documented at the call site.
- **Verify against ISA, not comments, not AI.** Never trust an AI agent's PPC64 semantic claim. Always cross-check against the Power ISA v2.07B manual AND a minimal empirical C program compiled with GCC that exercises the exact opcode.

### Constant pools and FP-constant routing

- **Do NOT retry `bcl + mflr + lfd/lfs` for hot FP scalar constants.** The SM port built it, tested it correct, then saw ~200× slowdown from Return Address Stack (RAS) thrash on POWER8. POWER8 stays inline (GCC-style `lis + ori + rldimi`) by design; there is no POWER8 equivalent to POWER9's `addpcis`.
- **POWER9 FP constants via `addpcis` + `lfd/lfs`** — no branch, no LR clobber, no RAS push. Gate behind `HasPOWER9()`.
- **Trap-site / pool-flush ordering.** Any `wasmLoad`/`wasmStore`/atomic helper that records a trap-site offset must `flushPool()` **before** the offset is recorded. Otherwise a constant-pool body inserted between `currentOffset()` and the actual load shifts the load past the recorded offset and the trap fires at the wrong PC. This is subtle and only triggers under specific wasm memory-access patterns.
- **`loadConstantSimd128` should always go via an inline pool entry**, not `movePtr × 2 + mtvsrdd`. ~60% smaller.
- **Pool base register.** SM uses `r16` (not `r12`) as the pool base to avoid scratch-pool conflict. JSC should do the same: reserve one non-volatile GPR for pool loads and keep it out of the scratch pool.

### Branch and call size constraints

These hard limits dictate every `enterNoPool` guard and `ma_b` size reservation. Get them wrong and a late pool flush shifts your branch past its target.

- Conditional branches (BC-form): 16-bit displacement, **±32 KB**.
- Unconditional branches (B-form): 26-bit displacement, **±32 MB**.
- Patchable near-call (`PatchWrite_NearCallSize`): **40 bytes = 10 instructions** (ELFv2 TOC-safe call stanza).
- `ma_b` forward / long branch worst case: reserve **14 instructions** of no-pool space (POWER8 overflow-condition adds 3 insns vs POWER9's `mcrxrx`).
- `toggledCall` / `call(ImmPtr)` / `nopPatchableToCall` / `callWithPatch`: reserve **10 instructions**.

**Stanza invariant**: every `label->bound()` code path needs both short-form AND long-form handling. If the label is bound and the offset fits in 16/26 bits, emit the short branch; otherwise emit a full `load64 + mtctr + bctr` (or `bctrl` for calls) stanza with a relocation entry. Unbound labels always emit the long form as a trap stanza for `bind()` to resolve. JSC's `LinkBuffer` is the place to enforce this invariant — do not let any backend path short-circuit to a single-form emit.

### Validation traps

- **POWER8 forced testing ≠ real POWER8 silicon.** The SM port has a `MOZ_PPC64_FORCE_POWER8=1` flag that flips runtime gating but the CPU still executes POWER9 instructions if something slipped past the gate. SM's original 4-instance bug (unguarded `xxinsertw` in SIMD replaceLane) passed the forced-POWER8 suite and would have crashed on real P8 silicon. **Design the JSC equivalent as `JSC_forcePOWER8=1` but treat results as provisional** — the definitive POWER8 validation requires actual POWER8 hardware (or possibly qemu-power8, untested at scale).
- **Always run forced-POWER8 alongside POWER9** as a matrix, not as an afterthought. The SM argon2 postmortem uncovered a *second independent bug* (ExtractLaneToGPR scratch clobber) that only forced-POWER8 would have caught.
- **Never trust single-tier self-verify tests.** Crypto or hash-based benchmarks that re-run their own hash inside one tier are blind to miscompiles — both the reference and the result go through the same buggy codegen. Hash against a native reference (Python, OpenSSL, node on another arch).
- **Add a differential baseline-vs-FTL harness early.** Diffing observable state between `--useFTLJIT=0` and `--useFTLJIT=1` on a handful of representative wasm/JS programs would have flagged SM's argon2 miscompile on day one. In JSC terms: baseline-vs-DFG-vs-FTL, three-way diff.
- **`wasm-reduce` + differential predicate** is the fastest minimal-repro tool for SIMD miscompiles. SM reduced a 30 KB / 40-function module to 486 B / 7 functions in ~20 minutes, unattended. Set this up before you need it.

### Concurrency / memory ordering

SM ran into a **compare-exchange-fail-path reservation leak** and three workarounds all pessimised the success path; the hazard is narrow per ISA and remains open. JSC's `Atomics*` implementations will need the same care. PPC's `lwarx`/`stwcx.` (and `ldarx`/`stdcx.`) reservation semantics require the fail path to explicitly clear the reservation via a dummy `stwcx.` or an intervening event — and doing it cleanly without slowing the success path is an unsolved problem in SM's port. Assume you will hit this and design the compare-exchange macro with a separate success-path and fail-path exit so the fail path can be enlarged without regressing success.

### Audit methodology (learn from the SM experience)

The SM author repeatedly found that subagent code audits produced **plausible-looking file:line claims that were pure extrapolation**. The pattern: an agent sees `if (HasPOWER9()) { fast } else { fallback }` and fabricates "the POWER8 fallback is a missed optimization" without opening the file. On 2026-04-20 this generated a bogus "P8 slowdowns" list that cost real investigation time.

Rules for any audit subagent in this port:

1. **Ask for evidence, not conclusions.** Every claim must quote the exact code at the cited line AND state what would falsify the claim. No quote = claim rejected.
2. **Embed known failure modes in the prompt.** A `HasPOWER9()` branch with a fast path present is not a missed optimization; it's a hardware gap. Instruction count ≠ cycle count on out-of-order POWER9.
3. **Two-pass verification.** First pass produces claims + citations; second pass (you or a narrow agent) opens every cited file:line. Claims that don't survive are dropped, not downgraded.
4. **No subagent output enters PLAN.md in the same turn it was produced.** Draft → verify per-item → commit.
5. **Measure on the dev box. Don't estimate.** Five minutes on real hardware beats arithmetic.

Bucket every audit finding into one of:

| Bucket | Pattern | Implication |
|---|---|---|
| (a) | `UNREACHABLE_FOR_PLATFORM()` reachable from a current caller | Real gap — fix |
| (b) | `UNREACHABLE_FOR_PLATFORM()` in a `default:` or behind a gate no caller flips | Defensive — leave |
| (c) | POWER9 fast path + POWER8 fallback, fallback is the tightest P8 encoding | Not a gap — hardware limit |
| (d) | POWER9 fast path + POWER8 fallback, fallback is suboptimal | Real gap — fix |

### Quick reference: register convention to adopt (mirrors SM, ELFv2-compliant)

| Reg | Role |
|---|---|
| r0 | Scratch only; not valid as load/store base |
| r1 | SP |
| r2 | TOC pointer |
| r3 | Return (also arg 0) |
| r3–r10 | Argument GPRs |
| r11, r12 | Scratch pool (MacroAssembler may borrow freely) |
| r13 | Linux TLS (**do not reuse**) |
| r14–r15 | Non-volatile, generally allocatable |
| r16 | Reserved for constant-pool base |
| r17–r30 | Non-volatile, allocatable |
| r31 | FP (frame pointer) |
| f0 | `ScratchDoubleReg` |
| f1–f13 | FP argument / volatile |
| f14–f31 | FP non-volatile, allocatable |
| v0 | `ScratchSimd128Reg` (VSR32) |
| v1–v19 | Volatile SIMD |
| v20–v31 | Non-volatile SIMD (in `NonVolatileMask`) |
| LR | Link register — saved to frame before nested calls |
| CTR | Indirect-call pointer; also used by loop `bdnz` (conflict!) |
| CR0 | Default condition-register field for MacroAssembler compares |
| XER | Carry / overflow; needs care on branchAdd32/branchSub32 POWER8 path |

---

## Phase 0 — Bring-up (build & test infra) — ✅ DONE 2026-04-23

Gate (met): `jsc` runs on the PPC64LE box via **C_LOOP interpreter only**, passes the smoke subset of `JSTests/stress/`.

### Verified working recipe

**Box**: `tle@192.168.1.247` — Fedora 44, ppc64le, kernel 6.19.10, POWER9, 32 cores, 63 GiB RAM, **64 KiB page size**, GCC 16.0.1, clang 22.1.1, CMake 4.3.0, Ninja 1.13.2, Ruby 4.0.1, Perl 5.42.2, Python 3.14.3, ICU 77.1. No extra packages needed beyond a stock Fedora 44 Workstation with standard devtools.

**Ship the source** (local → box):
```sh
# Exclude .git (12 GB of 18 GB total) — clone from GitHub on the box if you need git state.
# Default ssh cipher; rsync over ssh runs at ~10 MB/s on the local LAN so this takes ~10–15 min for ~6 GB.
rsync -a --progress \
  --exclude='.git/' --exclude='WebKitBuild/' \
  --exclude='.DS_Store' --exclude='**/.DS_Store' \
  /Users/tle/Work/WebKit/ \
  tle@192.168.1.247:/home/tle/Work/WebKit/
```

**Configure and build** (on the box):
```sh
cd /home/tle/Work/WebKit
CXXFLAGS="-Wno-error=sfinae-incomplete" \
  Tools/Scripts/build-jsc --jsc-only --release \
    --cmakeargs="-DENABLE_C_LOOP=ON \
                 -DENABLE_JIT=OFF \
                 -DENABLE_WEBASSEMBLY=OFF \
                 -DENABLE_STATIC_JSC=OFF \
                 -DUSE_SYSTEM_MALLOC=ON"
# ~15m45s on POWER9/32-core. Produces WebKitBuild/JSCOnly/Release/bin/jsc (~550 KB).
```

**Why each flag is needed:**
- `-DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_WEBASSEMBLY=OFF` — the Phase 0 scope: interpreter only.
- `-DUSE_SYSTEM_MALLOC=ON` — **required**. The CMake "unknown CPU" default branch in `Source/cmake/WebKitFeatures.cmake` leaves `USE_SYSTEM_MALLOC_DEFAULT=OFF` and `USE_MIMALLOC_DEFAULT=OFF`, so bmalloc falls through to `libpas`, which in turn hits `Source/bmalloc/bmalloc/BPlatform.h:437` → `#error "libpas, mimalloc, or system malloc needs to be specified"` because libpas has no PPC64 port. System malloc is the cheapest Phase 0 fix; we'll revisit with mimalloc later.
- `-DENABLE_STATIC_JSC=OFF` — matches the JSCOnly default; explicit to avoid surprise.
- `CXXFLAGS="-Wno-error=sfinae-incomplete"` — **required on GCC 16** (Fedora 44). GCC 16 added `-Wsfinae-incomplete` which trips on WTF's forward-declared `WTF::String` / `WTF::CString` used in SFINAE contexts within `<iterator_concepts.h>`. This is a compiler-version issue, not PPC64-specific — same fix would be needed for an x86_64 Fedora 44 build. Demote to warning. (On GCC ≤ 15 or clang, the flag is a no-op.)

### Known CMake quirk (latent, not yet blocking)

`Source/cmake/WebKitCommon.cmake:125-130` has a buggy elseif ordering:
```cmake
elseif (LOWERCASE_CMAKE_SYSTEM_PROCESSOR MATCHES "(ppc|powerpc)")  # matches "ppc" in "ppc64le"
    set(WTF_CPU_PPC 1)
elseif (LOWERCASE_CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64")          # unreachable
    set(WTF_CPU_PPC64 1)
elseif (LOWERCASE_CMAKE_SYSTEM_PROCESSOR MATCHES "ppc64le")        # unreachable
    set(WTF_CPU_PPC64LE 1)
```
On `ppc64le`, `WTF_CPU_PPC=1` gets set (wrong), which means `WebKitFeatures.cmake` falls through to the `else()` "unknown CPU" branch (C_LOOP=ON, JIT=OFF, MALLOC=OFF) — which is what we want for Phase 0. Since no `CMakeLists.txt` reads `WTF_CPU_PPC*`, this latent bug has no runtime effect in Phase 0. **Fix in Phase 1** when we start wiring JIT defaults — reorder to most-specific-first (`ppc64le` → `ppc64` → `ppc`) and add a PPC64LE branch to `WebKitFeatures.cmake` with proper JIT/malloc defaults.

### Smoke test (working)

```sh
# Small smoke subset:
cd /home/tle/Work/WebKit
mkdir -p /tmp/smoke
cp JSTests/stress/{Number-isNaN-basics,Number-isNumber-basic,16bit-code,32bit-code}.js /tmp/smoke/
Tools/Scripts/run-jsc-stress-tests \
    --jsc WebKitBuild/JSCOnly/Release/bin/jsc \
    --no-jit -c 8 \
    /tmp/smoke

# Results live in ./results/:
cat results/resultsByFamily   # per-source-file PASS/FAIL
cat results/results           # per-test-mode PASS/FAIL
cat results/passed            # list of passing runs
```

**Observed result**: 4 source tests × 4 interpreter-compatible modes (`default`, `bytecode-cache`, `mini-mode`, `lockdown`) = **16/16 PASS**.

**Critical flag**: `--no-jit` is required. Without it, the runner tries JIT-only modes (`no-llint`, `dfg-eager`, `ftl-*`) which fail with `INCOHERENT OPTIONS: at least one of useLLInt or useJIT must be true` or segfault, since our binary has no JIT.

**Minor cosmetic**: runner prints `Warning: did not find json or highline; some features will be disabled.` and exits with code 1 even on all-PASS runs. The ruby `json`/`highline` gems are optional — test execution and results are unaffected. Install with `sudo gem install json highline` if you want the summary output / clean exit.

### Quick smoke invocation

```sh
# One-liner sanity:
/home/tle/Work/WebKit/WebKitBuild/JSCOnly/Release/bin/jsc -e 'print(40+2)'   # → 42
```

### Full JSTests/stress/ run — 2026-04-24

Command: `run-jsc-stress-tests --jsc ... --no-jit -c 16 -o /tmp/stress-results JSTests/stress`
Duration: ~30 min. Results: **17894 PASS / 11 FAIL out of 17905 test runs** (4978 source files × 4 modes = ~19912 scheduled; ~2000 skipped by the runner for non-jit reasons).

All 11 failures are expected and explained:

| Test | Modes | Cause | Notes |
|---|---|---|---|
| `typed-array-oom-in-buffer-accessor.js` | default, bytecode-cache, mini-mode, lockdown | `//@ memoryHog` + `--useGC=0` → OOM killer fires on 63 GB box | Not PPC64-specific; would fail on any large-RAM machine |
| `map-forEach.js` | default, bytecode-cache, mini-mode, lockdown | `ReferenceError: Can't find variable: WebAssembly` | We built with `-DENABLE_WEBASSEMBLY=OFF`; remove this flag to fix |
| `many-substrings-of-rope-shouldnt-use-excessive-memory.js` | default | Expected ≤7 MB but used 25 MB | **PPC64LE 64K pages**: allocations rounded to 64 KB; threshold is tuned for 4K-page platforms |
| `codeblock-destructor-access-unlinkedcodeblock.js` | default | SIGTERM (timeout) | Test has `//@ skip if $cloop` — the runner doesn't auto-skip, C_LOOP is too slow for the 3s test window |
| `proxy-set-failure-inline-cache.js` | bytecode-cache only | `shouldBe(setCalls, 1e7)` → `setCalls=0` | **Upstream cross-platform JSC bytecode-cache race** — see dedicated section below. Happens on ARM64 too at sufficient concurrency. Not PPC64-specific. |

**Verdict**: all 11 failures are either config/environment artifacts or (in the case of `proxy-set-failure-inline-cache.js.bytecode-cache`) an upstream cross-platform JSC bug. Zero PPC64-specific regressions found.

### ARM64 reference oracle cross-check — ✅ DONE 2026-04-23

**Box**: `tle@192.168.64.3` — Fedora 43, aarch64, 8 cores, 7.8 GB RAM.

Built the same C_LOOP JSC with the same flag set (plus `-DENABLE_SAMPLING_PROFILER=OFF` which was required because ARM64 CMake defaults enable the sampling profiler, and that conflicts with `ENABLE_C_LOOP`). Ran:

```sh
JSCTEST_timeout=60 \
  Tools/Scripts/run-jsc-stress-tests \
    --jsc WebKitBuild/JSCOnly/Release/bin/jsc \
    --no-jit --memory-limited -c 8 \
    JSTests/stress
```

(`--memory-limited` skips 119 tests tagged `memoryHog!` to avoid OOM hangs on the 7.8 GB box.)

Result: **17604/17604 PASS, 0 failures** at the default runner concurrency level (`-c 8`).

Initial verdict of "PPC64-specific bug" for `proxy-set-failure-inline-cache.js.bytecode-cache` was **wrong** — see next section. It reproduces on ARM64 too once concurrency is high enough.

### Upstream cross-platform bug: bytecode-cache race under concurrent writers — investigated 2026-04-24

`proxy-set-failure-inline-cache.js.bytecode-cache` appeared as a single PPC64 failure in the original `-c 16` stress run. Further investigation showed it is **a cross-platform JSC bug in the bytecode cache serialization path, not PPC64-specific**.

**Reproducer** (works on both PPC64LE POWER9 and ARM64 Fedora 43):
```sh
# Copy the test 20× and run 20 parallel workers, each doing a pass-1 cache write + pass-2 forceDiskCache read
for i in $(seq 1 20); do mkdir -p w$i; done
for i in $(seq 1 20); do
  ( JSC_diskCachePath=$(pwd)/w$i ./jsc proxy-set-failure-inline-cache.js > /dev/null
    JSC_diskCachePath=$(pwd)/w$i JSC_forceDiskCache=true ./jsc proxy-set-failure-inline-cache.js > w-$i.out 2>&1
  ) &
done
wait
# Both platforms: 20/20 FAIL with `Exception: Error: Bad value: 0!`
```

**Root cause, narrowed** (not fully diagnosed):
Diffing the cache files written under sequential vs concurrent pass-1 runs (`jsc -d` on the cached bytecode):

| | Sequential (good) | Concurrent (bad) |
|---|---|---|
| PPC64LE cache size | 15024 B | 16992 B |
| ARM64 cache size | 15024 B | 15808 B |
| Proxy `set` handler bytecode | 7 insns / 134 bytes / 5 params | **3 insns / 5 bytes / 1 param** |

On both platforms, the Proxy handler's `set` method is serialized as an **empty stub function** (body ≈ `function() { return undefined; }`) when pass-1 runs alongside other pass-1 instances. On the forceDiskCache pass-2, loading that stub makes `proxy.foo = i` trigger a no-op `set` trap that returns `undefined` (falsy). The caller is non-strict, so the falsy return is silent — the loop completes with `setCalls === 0` and the assertion fails.

**What's been ruled out**:
- Not LLInt IC caching (`--useLLIntICs=false` still fails 20/20)
- Not concurrent JIT (`--useConcurrentJIT=false` still fails)
- Not concurrent GC / marker threads (`--useConcurrentGC=false --numberOfGCMarkers=1` still fails)
- Not CPU affinity (`taskset` per-core still fails 19/20)
- Not specific to identical sources — 20 copies with distinct-but-equivalent source content all fail
- Not specific to PPC64LE — ARM64 aarch64 fails identically

**What triggers it**: the specific 4-block structure of the real test (IIFE with Proxy + `shouldThrow` with Proxy, each with a 1e7 loop). Simpler proxy tests — even with identical concurrency load — do not reproduce it. Reduced 1-block or 2-block versions pass 20/20.

**Why we never saw this on the ARM64 oracle run**: the default stress runner concurrency is `-c 8` (one worker per core on the 8-core aarch64 box). At that level, the 17904-test batch hits the race rarely enough that this specific test happens to not land in a racing window. On the 32-core POWER9 box running `-c 16`, the race is likelier per-batch but still sporadic (we saw 1 of 17905 test instances fail). The reproducer amplifies it by running 20 identical racing pass-1 writers at once.

**Status**: not a blocker for the PPC64 port. All 11 PPC64 failures from the original stress run are now accounted for (10 config/environment + 1 upstream cross-platform race). Will be reported to WebKit upstream separately; does not need resolution before Phase 1 starts.

### What Phase 0 did NOT prove

- **mimalloc path not attempted** — using system malloc. Switching to mimalloc is a Phase 1 pre-req; test whether it handles 64K pages cleanly.

### Success criteria

- ✅ `jsc` binary built and executes JavaScript.
- ✅ 16/16 smoke-subset stress tests pass under C_LOOP.
- ✅ Full `JSTests/stress/` run: 17894/17905 pass; all 11 failures explained.
- ✅ CMake elseif-ordering bug fixed; PPC64LE branch added to `WebKitFeatures.cmake`.
- ✅ ARM64 reference oracle cross-check: 17604/17604 PASS at default concurrency.
- ✅ `proxy-set-failure-inline-cache.js.bytecode-cache` root-cause investigation: cross-platform JSC upstream race, not a PPC64 port issue. Zero PPC64-specific bugs identified in Phase 0.

## Phase 1 — Assembler skeleton + register map

Gate: `MacroAssemblerPPC64` compiles and links into a JSC built with `ENABLE_JIT=ON`, even if every macro is `UNREACHABLE_FOR_PLATFORM()`.

1. Write `PPC64Registers.h` defining the GPR set (r0–r31), FPR (f0–f31), VSR/VSX (vs0–vs63), condition register fields (cr0–cr7), LR, CTR, XER. Map ELFv2 roles: r1=SP, r2=TOC, r13=thread pointer, r14–r31 nonvolatile, r3–r10 arg regs, r3/r4 return, r12 function entry (for TOC). **Do not reuse r13** — Linux uses it for TLS.
2. Write `PPC64Assembler.h` with an opcode buffer and one instruction per ISA form (D-form, X-form, XO-form, I-form, B-form, DS-form, VX-form, XX3-form). Start with `addi`, `add`, `ori`, `ld`, `std`, `mfspr`, `mtspr`, `b`, `bc`, `bclr`, `bcctr`, `nop`. Encode per **Power ISA v2.07B Book I**. Any opcode you cannot find in v2.07B is either POWER9+ (guard it) or you are reading the wrong manual.
3. Write `MacroAssemblerPPC64.h/cpp` as a `AbstractMacroAssembler<PPC64Assembler>` subclass. All methods stub to `UNREACHABLE_FOR_PLATFORM()` except `nop`, `breakpoint` (use `trap` 0x7fe00008), `move`, `load64`, `store64`, `add64`, `sub64`, `jump`, `call`, `ret`, `push`, `pop`. Just enough to compile.
4. Wire into `assembler/MacroAssembler.h` and `TargetAssemblerDefinitions.h`.
5. Flip CMake default `ENABLE_JIT_DEFAULT=ON` for PPC64LE. Build must succeed; `jsc` still uses LLInt C_LOOP, but the binary contains a MacroAssembler.

**Verification**: unit-test each opcode's encoding against GNU `as` output. Script: `echo 'addi 3,4,5' | powerpc64le-linux-gnu-as - -o /tmp/x.o && objdump -d /tmp/x.o` vs `PPC64Assembler().addi(r3, r4, 5); dump()`. Check byte-by-byte. Do this for every single opcode as it is added.

## Phase 2 — LLInt offlineasm PPC64LE backend

Gate: LLInt runs native on PPC64LE; `--useJIT=0` runs the full stress suite at speeds comparable to native arm64 LLInt.

1. Copy `offlineasm/riscv64.rb` to `offlineasm/ppc64le.rb` as a structural starting point (RISCV64 is the smallest, most recent new-arch offlineasm backend — easier to diff against as you add PPC-specific rewrites). For the semantics of each pseudo-op lowering (what `cCall4` must actually do, how OSR-exit-adjacent ops must be preserved, how the Wasm tiering slow path interacts with `cloopDo`), **consult `offlineasm/arm64.rb`** — it is the reference implementation. Rewrite lowering tables for PPC64 ops. Use the PPC64 ABI for `cCall*` pseudo-ops. Honor red-zone semantics carefully — PPC64LE ELFv2 has a 288-byte red zone, bigger than ARM64's.
2. Add backend to `offlineasm/backends.rb` WORKING_BACKENDS and the `CMakeLists.txt:278` `OFFLINE_ASM_BACKEND` switch.
3. Expect initial breakage in `LowLevelInterpreter.asm` due to missing pseudo-op lowerings. Walk the bytecode opcodes in order, implement each as it's hit.
4. Probe infrastructure (`LLIntData::setEntrypointAndInitializeLLIntData`) needs to know about LR/CTR saving and TOC setup per ELFv2.

**Verification**: `run-jsc-stress-tests` parity against C_LOOP run from Phase 0. Any new failure is a bug in the PPC64 offlineasm backend. Cross-check specific failing tests against ARM64 `--dumpBytecode=1` to confirm bytecode is identical (if not, the bug is in bytecode generation, not LLInt).

## Phase 3 — Baseline JIT

Gate: `jsc --useDFGJIT=0 --useFTLJIT=0 --useWasmJIT=0` (Baseline only) passes the stress suite.

1. Expand MacroAssemblerPPC64 to cover the full Baseline surface: all integer arith, logical, shift, comparison, branches, loads/stores across sizes, float arith, truncation, conversion. Use ISA XO-form for arith, X-form for loads/stores, B-form for conditional branches via CR fields.
2. Implement `nearCall`/`farCall` — PPC64LE needs a local-call fast path (direct `bl`) and an external-call path that restores TOC via r2 reload (`ld r2, 24(r1)` per ELFv2 §2.3.2).
3. Implement probe trampolines for the Probe API (`Probe.cpp` hooks).
4. Flush icache correctly. On PPC the sequence is `dcbst; sync; icbi; isync` — the Linux kernel exposes `__builtin___clear_cache`; use it and verify it emits the right sequence via `objdump`.

**Verification**: Baseline vs LLInt parity. Any test that passes in LLInt and fails in Baseline is a pure codegen bug; isolate with `JSC_useBaselineJIT=true JSC_jitPolicyScale=0.001`.

## Phase 4 — DFG (Data Flow Graph) JIT

Gate: `jsc --useFTLJIT=0 --useWasmJIT=0` (Baseline + DFG) passes the stress suite.

1. DFG is mostly architecture-agnostic — `DFGSpeculativeJIT64.cpp` and `DFGSpeculativeJIT.cpp` operate on MacroAssembler abstractions. Fill in whatever MacroAssemblerPPC64 holes show up. Expect gaps in SIMD-adjacent paths and fast-math conversion.
2. OSR exit machinery needs MachineContext integration — verify PPC register save/restore in `MachineContext.h` covers all GPRs/FPRs and CR fields the DFG clobbers.
3. Inline caches: PPC64 function-call ABI (TOC pointer in r2) complicates IC patching. **Read the ARM64 IC implementation first** (`jit/Repatch.cpp`, `bytecode/StructureStubInfo.*`, and the ARM64 MacroAssembler patching primitives) — it is the reference for what IC patching must guarantee. Then cross-check RISCV64 for the bare-Linux-port-shape patterns (cache flushing, W^X transitions without Apple JIT permissions). The PPC answer will likely use trampolines that keep r2 stable across the patched call site.

**Verification**: diff `jsc --dumpDFGGraph=1` output between ARM64 and PPC64 on the same test — the DFG IR should be identical (it's arch-agnostic). Divergence means we're mis-reading the bytecode. Codegen divergence is expected at the Air level.

## Phase 5 — B3 / Air backend

Gate: FTL and WASM-OMG both functional. This is the largest phase.

1. Per-arch Air bits: `b3/air/AirOpcode.opcodes` additions guarded by `arch(ppc64le)`; `AirCCallingConvention.cpp` ELFv2 specialization; Air register info covering GPR/FPR volatility per ELFv2 Table 2.1.
2. Register allocator is arch-agnostic, but the register set sizes and constraints matter. PPC64 has 32 GPR / 32 FPR / 64 VSX, similar headroom to ARM64.
3. Air lowering rules per Air opcode. Walk `AirOpcode.opcodes` and add PPC64 encodings.
4. SIMD: POWER has VSX (128-bit vectors). **VSX on POWER8 is Power ISA v2.07B Book I Chapter 7** — the v2.07 VSX subset is what we target. POWER9 adds VSX3 (`xxspltib`, byte-reverse helpers, etc.); those are POWER9-gated fast paths, not baseline. Map B3's SIMD ops to v2.07 VSX first (`xvadddp`, `xxland`, `xxsel`, `xvcmpeqdp`, …); only reach for VSX3 opcodes behind a runtime feature check. This is the riskiest part of the port — punt to a later milestone if WASM SIMD tests blow up; gate `ENABLE_WEBASSEMBLY_SIMD` off initially.

**Verification**: `JSC_dumpAirGraph=1` on the same test on ARM64 and PPC64 — the Air IR should be **structurally** similar. Air passes FTL through the full B3 optimizer, so correctness bugs here are extremely subtle. Use `--useConcurrentJIT=0 --validateAir=1 --validateB3=1` as the default dev flags.

## Phase 6 — WASM BBQ + OMG

Gate: WASM conformance suite (via `Tools/Scripts/run-jsc-stress-tests JSTests/wasm`) passes.

1. BBQ (`WasmBBQJIT64.cpp`) is MacroAssembler-based — will mostly work once Phase 3 is solid. `WasmCallingConvention.cpp` needs a PPC64 entry assigning GPR/FPR argument slots per ELFv2.
2. OMG uses B3/Air — works once Phase 5 is solid.
3. WASM SIMD: gated off until VSX lowering is ready.
4. WASM traps: implement signal-based trap handler using PPC64 `siginfo_t` / `ucontext_t`; extract faulting PC from `uc_mcontext.gp_regs[PT_NIP]`.

**Verification**: `JSTests/wasm/` stress runs. `wasm-tools`-generated fuzz cases cross-checked between arm64 and ppc64.

## Phase 7 — Hardening

1. Address-sanitized + UBSAN builds pass.
2. Stress-test concurrent JIT (`--useConcurrentJIT=1`) — memory ordering is looser on PPC than ARM; audit every atomic for correct `sync`/`lwsync`/`isync` placement. PPC uses `lwsync` for acquire/release; full `sync` only for seq-cst.
3. JIT cage / `USE(JIT_CAGE)` currently relies on ARM64 PAC + specific mmap tricks. Decide: either disable JIT cage on PPC64LE (simplest) or port it (significant). Default off for v1.
4. Benchmark against LLInt and against ARM64 JIT on identical workload. Target: DFG ≥ 5× LLInt, FTL ≥ 2× DFG.

## Testing strategy (applies to every phase)

1. **Every phase must green the previous phase's gate before starting.** No "we'll come back to this."
2. **Dual-machine diff.** On every non-trivial failure: re-run the same test on ARM64 with the same `jsc` flags, diff outputs. If ARM64 fails the same way, the bug is arch-independent (probably in our offlineasm or MacroAssembler use). If ARM64 passes, the bug is in PPC64 codegen.
3. **Empirical opcode verification.** Every instruction encoding we write gets byte-compared against `powerpc64le-linux-gnu-as` output. No exceptions.
4. **Golden bytecode files.** For small JS test programs, capture `--dumpBytecode=1 --dumpDFGGraph=1 --dumpAirGraph=1` on ARM64 and store as golden. Phase 1–4 must produce equivalent bytecode and DFG graphs (Air diverges, but bytecode and DFG must match).
5. **Commit discipline.** One opcode per commit in early phases. One test-suite-passing green gate per merge.

## Known risks & open questions

See the "Lessons from the Firefox SpiderMonkey PPC64 port" section for concrete workarounds to most of these.

- **TOC (r2) handling.** ELFv2 local calls don't restore r2, global calls do. Getting this wrong produces crashes deep in libc. Bake a `debugAssertR2Correct()` check into MacroAssemblerPPC64 in debug builds.
- **Link register.** LR is not a GPR on PPC; every call clobbers it. Baseline/DFG call sequences must save LR into the frame before making nested calls. This differs sharply from ARM64 (where LR is just x30) and from RISCV64.
- **Condition register.** PPC compares write to CR fields, not GPRs. Lower MacroAssembler's `branch32(...)`-style API to `cmpd` + `bc` via a specific CR field; adopt SM's convention of CR0 for the fast path and stick to it.
- **Scratch-register pool.** r11/r12 as primary, r16 as pool-base, r0 **not** usable as a load/store base. See Lessons section — this is the top source of SM bugs.
- **Memory ordering.** ARM64 has acquire/release loads/stores as primitives; PPC needs explicit `lwsync` / `isync` / full `sync` fences. DFG/FTL concurrent-JIT assumptions need audit. PPC uses `lwsync` for acquire/release; full `sync` only for seq-cst.
- **Compare-exchange reservation leak on fail path.** SM has this open and unsolved — all three workarounds pessimise the success path. Design the PPC64 `compareExchange` macro with separate success/fail exits so the fail path can be enlarged without regressing success.
- **Executable memory.** `mmap(PROT_EXEC)` + icache flush semantics on recent kernels — verify on the actual test box kernel version. Use `__builtin___clear_cache` and verify the emitted `dcbst; sync; icbi; isync` sequence.
- **POWER8 validation gap.** Runtime-gated POWER8-forced testing is not equivalent to real POWER8 silicon. Unguarded POWER9 instructions pass the forced test and crash on real P8. Acquire POWER8 hardware or qemu-power8 access before claiming POWER8 support.
- **mimalloc + 64K pages.** Default on for RISCV64 and ARM64. 64K pages are standard on Linux PPC64LE (not 4K). Confirm upstream mimalloc supports PPC64LE page sizes; if not, default `USE_MIMALLOC=OFF` for PPC64LE initially.

## Immediate next actions

Phase 0 is complete with zero PPC64-specific blockers. Proceeding to Phase 1.

1. **Evaluate mimalloc on 64K pages.** Try `-DUSE_MIMALLOC=ON` rebuild on PPC64 box. If it works cleanly, update the Phase 0 recipe and drop `USE_SYSTEM_MALLOC=ON`; the PPC64LE `WebKitFeatures.cmake` branch should then default to mimalloc (matching RISCV64/MIPS).
2. **Start Phase 1** — assembler skeleton (`PPC64Registers.h`, `PPC64Assembler.h`, `MacroAssemblerPPC64.h`) and wire into `MacroAssembler.h` / `TargetAssemblerDefinitions.h`. Read `Source/JavaScriptCore/assembler/RISCV64Assembler.h` and `Source/JavaScriptCore/assembler/ARM64Assembler.h` for structure reference before writing any PPC64 code.
3. **Upstream bytecode-cache race** — out-of-band, not a port-work item. Report to WebKit upstream with the reproducer documented in the "Upstream cross-platform bug" section above.

## Appendix — commands cheat sheet

```sh
# Rsync local checkout → PPC64 box (~10–15 min over LAN, no .git)
rsync -a --progress \
    --exclude='.git/' --exclude='WebKitBuild/' \
    --exclude='.DS_Store' --exclude='**/.DS_Store' \
    /Users/tle/Work/WebKit/ \
    tle@192.168.1.247:/home/tle/Work/WebKit/

# Verified Phase 0 build (C_LOOP only, Fedora 44 / GCC 16 / ppc64le)
CXXFLAGS="-Wno-error=sfinae-incomplete" \
  Tools/Scripts/build-jsc --jsc-only --release \
    --cmakeargs="-DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_WEBASSEMBLY=OFF \
                 -DENABLE_STATIC_JSC=OFF -DUSE_SYSTEM_MALLOC=ON"

# Future: PPC64LE with new JIT once Phase 2 lands
Tools/Scripts/build-jsc --jsc-only --release \
    --cmakeargs="-DENABLE_JIT=ON -DENABLE_FTL_JIT=OFF"

# Smoke stress tests (C_LOOP build — --no-jit is required)
Tools/Scripts/run-jsc-stress-tests \
    --jsc WebKitBuild/JSCOnly/Release/bin/jsc \
    --no-jit -c 8 \
    <test-dir>
# Results: ./results/resultsByFamily, ./results/results, ./results/passed

# jsc one-liner
WebKitBuild/JSCOnly/Release/bin/jsc -e 'print(40+2)'   # → 42

# Dump IR at every tier (for Phase 2+ debugging)
jsc --dumpBytecode=1 --dumpDFGGraph=1 \
    --useFTLJIT=0 --useConcurrentJIT=0 \
    test.js 2>&1 | less

# Assembler encoding verification (Phase 1+)
echo 'addi 3,4,5' | powerpc64le-linux-gnu-as - -o /tmp/x.o
objdump -d /tmp/x.o
```
