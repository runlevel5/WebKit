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

### Phase 2 status — IN PROGRESS (updated 2026-07-13)

**Where we are.** The offlineasm ppc64le backend assembles all of `LowLevelInterpreter*.asm` through GAS; jsc builds and links with `ENABLE_JIT=ON, ENABLE_C_LOOP=OFF, ENABLE_WEBASSEMBLY=ON` (BBQ/OMG off), `USE_SYSTEM_MALLOC=ON`, `CXXFLAGS=-Wno-error=sfinae-incomplete` on the power9 box (`ssh power9`, repo at `/home/tle/Work/WebKit`, build dir `WebKitBuild/JSCOnly/Release`).

**What works**: `jsc --useJIT=0 --useWasm=0 -e 'print(40+2)'` prints `42`. Native LLInt executes JavaScript correctly for simple evaluations.

**Current crash (the active debugging front)**: after printing the correct result, the process segfaults at `op_call_return_location`, i.e. on the return path of an `llint_op_call` (gdb: frame #0 `op_call_return_location`, frame #1 `llint_op_call`, "frame did not save the PC"). With JIT or Wasm left enabled the crash happens before any output (expected — MacroAssemblerPPC64 is still largely stubs; always test with `--useJIT=0 --useWasm=0` in Phase 2). **Confirmed unchanged after the 2026-07-13 rebase** (clean rebuild on power9: `print(40+2)` → 42 then SIGSEGV).

**~~Second known bug — silent integer overflow wraparound~~ FIXED 2026-07-14** (commits `0fee2379768d` + `17425477775f` + `9a44b9d5285e`). Two stacked miscompiles: (1) the `badd*o/bsub*o` lowerings used `addo./bso`, but the dot form copies the STICKY XER[SO] into CR0 (not this op's OV), `addic.` never sets OV at all, and `addo` detects 64-bit overflow which two int32s can never produce — so `baddio` never branched and int32 adds wrapped mod 2^32; (2) `ci2ds` converted the full 64-bit boxed register (`fcfid` of `0xFFFE…0001` = −(2⁴⁹−1)) instead of the signed low word, corrupting every int+double add once a value became a double. Fixed XER-free (extsw+cmpd exact-64-bit check for int32; sign algebra for int64; `mtvsrwa/wz/d + fcfid*` for the whole int→float family), all v2.07B/POWER8-safe, all formulas hardware-verified against `__builtin_*_overflow` oracles. The `s+=i` 100k loop prints 4999950000 exactly; mixed int/double accumulation exact. Also fixed en route: `offlineasm/ppc64le.rb` was missing from CMake's `OFFLINE_ASM` dependency list, so backend edits didn't regenerate LLIntAssembly.h (`ninja: no work to do`) — that masked one fix iteration entirely.

**~~Call-return crash~~ FIXED 2026-07-14** (commit `5ac37ef259e0`): the Ruby-replicated makePtrTagHash matched no real tag value, so every indirect call was classified JS and emitted a bare `bctrl`. Host functions live in the jsc binary (different module → different TOC); with no `ld r2, 24(r1)` after `bctrl`, the first TOC-relative dispatch (`ld rX, off(r2)` loads the opcode table) after any host-call return faulted. Both the two-print crash and the exit-path crash were this one bug. Fix: capture offlineasm's constexpr name→value resolution via a `Module#prepend` hook on `ConstExpr#resolveOffsets`, classify symbolically (JS-entry tags → bare call; C tags + all untagged register calls → full ELFv2 stanza with 32-byte linkage frame, r2 save/restore, r12=entry), raise on unknown tags. **Never replicate a WTF hash in Ruby.**

**~~i-op width convention~~ FIXED 2026-07-14** (commit from this session): offlineasm `i` ops must zero-extend their 32-bit result into the 64-bit register (x86/arm64 hardware semantics). Our 64-bit lowerings let JSValue tag bits survive in the upper word; `op_switch_imm`'s `subi min, scrutinee` then indexed the jump table with `(numberTag<<2)+base` — every generator/async/iterator test crashed (generators dispatch resume points through a switch). All i-form ops now clear their result (`rldicl dst,dst,0,32`); hardware probe matrix documented in the arith section of ppc64le.rb. Inputs may stay dirty — 32-bit PPC ops read only low words and 64-bit op low-words equal the 32-bit result, so output-cleaning is a complete contract.

**Also fixed 2026-07-14**: runtime defaults `useJIT=false`/`useWasm=false` for PPC64LE (`jitEnabledByDefault()`/`canUseWasm()` — stubs abort on tier-up; no validated wasm engine; both overridable with `--useJIT=1`/`--useWasm=1`); C-tagged tail calls (`jmp reg, CustomAccessorPtrTag`) set r12 for the callee's GEP; offlineasm `addis` (add-and-set-flags, misimplemented as PPC add-immediate-shifted) now raises pending a real consumer.

### Phase 2 GATE MET — 2026-07-14

Full `JSTests/stress` on power9, LLInt-only (`--no-jit`, useJIT/useWasm defaulted off): **19546 PASS / 18 FAIL (99.91%)**. Round 1 (before the i-op width fix) was 18619/945 — the width fix swept all 261 generator/async/iterator failures including the entire `waitasync` cluster. All 18 residual failures classified, **zero PPC64 codegen bugs**:

| Failure | Count | Cause |
|---|---|---|
| `typed-array-oom-in-buffer-accessor` (all modes) | 4 | memoryHog + OOM-killer, known since Phase 0 |
| `map-forEach`, `resizable-array-constant-folding` | 5 | need the WebAssembly global; useWasm defaults off on PPC64LE |
| `many-substrings-of-rope…` | 1 | 64K-page memory threshold, known since Phase 0 |
| `proxy-set-failure-inline-cache`, `call-var-args-phantom-*` (bytecode-cache mode) | 4 | upstream cross-platform bytecode-cache race under concurrent runners (Phase 0 investigation); all pass in isolation |
| `*-on-non-promise`/`-on-non-regexp`, `string-replace-…` ("too many recompiles") | 4 | JIT-assumption tests running in default mode with useJIT defaulted off — test-harness artifact, not codegen |

Remaining Phase 2 polish before calling it fully closed: remove the `call C/JS` classification comments if noise bothers, and IPInt validation (wasm) can begin any time — the offlineasm backend now assembles it. Otherwise: **proceed to Phase 3 (Baseline JIT)**.

**Latent bugs (not yet exercised by any passing path — fix before their opcodes go live)**:
- `cd2i`/`truncated2is` clobbers its source FPR (`fctiwz src, src`) AND reads the wrong half of the stored double on LE (`lwz dst, 4(1)` reads the ISA-undefined high word; fctiwz's int32 lands at offset 0 on LE). Correct shape: FPR-Tmp expansion + `fctiwz ftmp, src; mfvsrwz dst, ftmp` (both v2.07B).
- `bdneq`/`bfneq` lowering is an explicit placeholder (`:lteq`) — ordered not-equal needs blt+bgt (NaN must not branch).
- `countTrailingZerosi` emits `cnttzw`, a POWER9 (ISA 3.0) instruction, unguarded — violates the POWER8 baseline; needs a v2.07 fallback or a gate.
- Shift-by-register count masking: JS `<<`/`>>` need count&31 (x86/arm64 hardware behavior); PPC `slw`/`srw` yield 0 for counts 32–63. Unverified whether LLInt feeds unmasked counts — test `x << 32` semantics and add explicit masking if needed.

**Hypothesis space being worked**: PPC64's call/return discipline vs LLInt's calling convention. `bctrl`/`bl` put the return address in LR (not on the stack like x86, and unlike ARM64 there is no guarantee the LLInt frame layout absorbs it the same way). Three commits circle this area:
- `pushCalleeSaves` uses an 80-byte sub-frame to avoid LLInt frame overlap;
- LABEL-form `call` (direct C slow-path calls) wrapped with an ELFv2 linkage area (`stdu/std r2/.../ld r2/addi`);
- WIP (committed, does NOT yet fix the crash): indirect `call` discriminates C callees from JS callees via the PtrTag operand — C callees need full ELFv2 (r12=entry for GEP, linkage area, r2 save/restore), JS callees must get a bare `mtctr/bctrl` because makeJavaScriptCall has already arranged the frame and an extra `stdu` shifts the callee's cfr.

**Debug aid in place**: the `call` lowering emits an asm comment (`ppc64le call dbg: nops=... tag_val=... c=...`) at every call site. Inspect the generated `LLIntAssembly.h` on the box to enumerate call sites and their resolved tags. Remove before Phase 2 is declared done.

**Next debugging steps**:
1. Enumerate every `call` site in generated LLIntAssembly.h by tag class; verify the C/JS classification is right at each (the PtrTag-hash matching in ppc64le.rb is fragile — consider having offlineasm resolve tag names symbolically instead of replicating WTF's `makePtrTagHash`).
2. Trace the JS→JS call/return convention end to end: where does the return address live when LLInt JS code calls LLInt JS code (`makeJavaScriptCall` → callee prologue → `ret`)? On ARM64 `call` sets lr and the callee's `functionPrologue` pushes it; our backend must put the return PC where `op_call_return_location`'s frame teardown expects it. The "frame did not save the PC" gdb note says the frame chain is already broken at entry to the return location.
3. Compare against ARM64: same test, `-d` disassembly of llint_op_call surroundings on the ARM64 box, and map each instruction to the PPC64 emission.

### Rebase log

- **2026-07-13**: rebased all 96 port commits onto `origin/main` @ `08160f69fe42` (previous base 2026-04-13, ~5850 upstream commits). Single conflict: `jit/GdbJIT.cpp` (upstream added RISCV64 to the same CPU guards where we added PPC64LE — resolved as union). Upstream changes reviewed for impact: new offlineasm instructions `adcq/sbcq/umulhq/smulhq/addqs/subqs` (wasm wide arithmetic) are per-arch **opt-in** lists in `instructions.rb` — no backend action required until we want the fast paths; `op_try_get_by_id` removed; wasm tail calls use a lazy restore frame; relaxed wasm SIMD landed (Phase 5+ concern). Pre-rebase branch preserved as `ppc64-pre-rebase-20260713`.

## Phase 3 — Baseline JIT

Gate: `jsc --useDFGJIT=0 --useFTLJIT=0 --useWasmJIT=0` (Baseline only) passes the stress suite.

### Phase 3 status — IN PROGRESS (2026-07-15)

**Done** (commits `c62d2e2f5f86`, `9714382cca82`):
- **Linking architecture**: RISCV64-style fixed 8-instruction slots for jump/branch/call, rewritten in place at link time (near `b`/`bc` or far `li64(r12)+mtctr+bctr(l)`); call slots keep the branch LAST so the return address is fixed; full link/relink/repatch/readPointer surface; `cacheFlush` via `__builtin___clear_cache`; patchable sizes = 32 bytes.
- **MacroAssembler core**: CR0 condition mapping, branch/compare families, 32/64-bit arith+logical with the zero-extension contract, masked shifts, overflow branches (Phase 2 technique), full load/store matrix with DS-form alignment fallback, farJump, `getEffectiveAddress`, JSC frame prologue/epilogue ([fp]=caller-fp, [fp+8]=return-pc; LR spilled via scratch). Scratches r11/r12 (SM convention).
- **testmasm harness operational** — with caveats below.

**Hard-won harness lessons**:
- **testmasm MUST run with `JSC_useJIT=1`** — our PPC64LE runtime default disables the executable pool; without it EVERY LinkBuffer silently finalizes to a NULL code pointer in Release and the harness calls address 0. Symptom: SIGSEGV at PC=0 with ctr=0, lr in the test-runner dispatch.
- **testmasm prints test names from worker threads** — a printed name does NOT mean the test passed; an "earlier test passed" reading cost a debugging detour. Judge only by the exit status of a single-filter run.
- The 32MB+guard-pages RWX pool reservation works on 64K-page Linux ppc64le.

**Grind progress (2026-07-16)**: JSC::initialize fully passes with the JIT live — all LLInt glue thunks emit and run, including the ELFv2 C-call round-trip (r2 saved at sp+24 inside the 96-byte maxFrameExtentForSlowPathCall reservation; was wrongly 0). Landed: call family (compile-time PtrTag discrimination), full FP surface (classic PPC float model, CR-logic single-branch DoubleConditions via cror/crandc), conditional moves (aliasing-safe bc-skip), 64-bit shifts, signed loads, pairs, memory-form arith, **and the full Probe API** (commit 32b42ab79387): handwritten ctiMasmProbeTrampoline saving/restoring all GPRs/FPRs + pc/cr/xer/lr/ctr, all 5 probe tests pass. testmasm caught several real bugs (getEffectiveAddress base-aliasing; resolveAddress(BaseIndex) addis-overflow ≥ 0x7FFF8000; probe pc-vs-lr conflation; r12 GEP-clobber round-trip; the initializeStackFunction TOC-save landing on the State's offset-24 field). **testmasm: 58 tests passing** (`JSC_useJIT=1`; stubs self-identify via PPC64_UNIMPLEMENTED — the grind loop is run-read-implement-repeat). Next stub: `or32(TrustedImm32, AbsoluteAddress)`. Continue the testmasm grind (memory-operand logical ops, atomics, remaining families), then jsc baseline bring-up.
- Open design item: JIT→C call TOC discipline (r2 save/restore + ELFv2 linkage headroom in JIT frames) — decide when CCallHelpers-level C calls first appear in the grind.

1. Expand MacroAssemblerPPC64 to cover the full Baseline surface: all integer arith, logical, shift, comparison, branches, loads/stores across sizes, float arith, truncation, conversion. Use ISA XO-form for arith, X-form for loads/stores, B-form for conditional branches via CR fields.
2. Implement `nearCall`/`farCall` — PPC64LE needs a local-call fast path (direct `bl`) and an external-call path that restores TOC via r2 reload (`ld r2, 24(r1)` per ELFv2 §2.3.2).
3. Implement probe trampolines for the Probe API (`Probe.cpp` hooks).
4. Flush icache correctly. On PPC the sequence is `dcbst; sync; icbi; isync` — the Linux kernel exposes `__builtin___clear_cache`; use it and verify it emits the right sequence via `objdump`.

**Verification**: Baseline vs LLInt parity. Any test that passes in LLInt and fails in Baseline is a pure codegen bug; isolate with `JSC_useBaselineJIT=true JSC_jitPolicyScale=0.001`.

### Phase 3 status — Baseline JIT surface COMPLETE, correctness debugging begins (2026-07-18)

**testmasm: 87/87 GREEN** — the entire MacroAssembler surface the harness exercises is verified on POWER9 (arith, logicals, shifts, all loads/stores/addressing modes, branches, compares, conditional moves, calls, full FP, and the complete Probe API). Run it with `JSC_useJIT=1` (mandatory — see harness lessons).

**Baseline JIT: full function compiles.** Driven by `jsc --useJIT=1 --useDFGJIT=0 --useFTLJIT=0 --useWasm=0 --thresholdForJITAfterWarmUp=10`, the grind (run → read the PPC64_UNIMPLEMENTED name → implement → repeat, batching related overloads to amortize the ~90s jsc rebuild) drove the Baseline tier until a whole JS function compiles with **zero remaining MacroAssembler stubs on the hot path**. Everything since testmasm-green is committed (memory-operand branches/tests, calls, callOperation, pairs, transfers, conditional moves, FP-memory arith, 64-bit immediate arith, fences).

**BASELINE JIT EXECUTES JAVASCRIPT (2026-07-19).** `g(41)`→42, `f(1000)`→499500, and a broad gauntlet (recursion incl. `n*fact(n-1)`, array iteration, exceptions, higher-order functions, `this`-methods, closures, float accumulation, string methods) all correct under `--useJIT=1 --useDFGJIT=0 --useFTLJIT=0`. **First correctness bug FIXED** (commit: mulp/muli 3-operand form): the `mulp/mulq/muli` lowering only handled 2-operand `dst*=src`, hardcoding dest=operands[1]; the 3-operand `mul(src1,src2,dst)` form (dst=LAST operand) wrote its product to the wrong register. The LLInt `argumentProfileLoop` (`mulp sizeof, t0, t2`) corrupted its loop counter and left the profile pointer unset → store walked into unmapped memory. Only surfaced under `--useJIT=1` because the argument-value-profile array is allocated only when JITting (JIT-off skips the loop via `btpz t3, done`) — which is exactly why LLInt-only passed all of Phase 2 while the first JIT call crashed. **Method to find it**: gdb the SIGSEGV → source-annotated generated LLIntAssembly.h → spot `mulli 3,3,24` where dest should be r5. **Next: run the stress suite at baseline tier for the Phase 3 gate.**

**Baseline stress status (2026-07-19)**: mainstream JS is correct, but a baseline-vs-LLInt sweep of 400 `JSTests/stress` files (JIT on, DFG/FTL off, threshold 6) showed ~189 SIGSEGV/SIGABRT — real baseline codegen bugs in adversarial edge cases (add-overflow recovery with aliased registers, custom `valueOf` re-entrancy, OSR-adjacent, fuzzer-agent tests). None were PPC64_UNIMPLEMENTED (the hot surface is complete); they are miscompiles to grind down individually. **Caveats on that sweep**: (1) it was a crude hand-harness — the ~211 "DIFF"s are mostly noise because raw stress files lack the runner's preamble (`noInline`, `testLoopCount`, `shouldBe`) and honor `//@ runDefault(...)` directives the hand-harness ignored; trust the CRASH count, not the diffs. (2) Our jsc has DFG compiled in (ENABLE_JIT=ON gates it; only FTL is off), so **baseline must be measured with explicit `--useDFGJIT=0 --useFTLJIT=0`** — the runner's default modes would tier into the un-brought-up DFG (Phase 4) and crash everywhere, which is not a baseline signal.

**Triage method that works** (used for the mulp bug): gdb the SIGSEGV → note the faulting instruction and that it's in LLInt glue vs JIT code → confirm JIT-on-specific (compare `--useJIT=0`) → open the source-annotated generated `LLIntAssembly.h` (grep the function label, the `// LowLevelInterpreter*.asm:NNNN` comments map each instruction to source) → spot the wrong register/opcode → fix the ppc64le.rb lowering or the MacroAssembler method. **Next session: grind the baseline stress crashes**, then run the formal Phase 3 gate (full stress at baseline via a proper runner invocation forcing `--useDFGJIT=0`). **Triage over 500 stress files (2026-07-19, baseline-only crashes, i.e. crashes gone under `--useJIT=0`): 243/500 (~49%)** — 181 SIGSEGV, 62 SIGABRT. Harness: `/tmp/triage.sh` on power9 (writes `/tmp/crashers.txt`, `/tmp/asserts.txt`); repro individual tests with `/tmp/pre.js` = `globalThis.testLoopCount = 50;` prepended and `--useDollarVM=1`. The crashes cluster into two classes:

### Phase 3 GATE — MET (2026-07-22)

**Full `JSTests/stress/` run at baseline tier (`--useJIT=1 --useDFGJIT=0 --useFTLJIT=0`, single `default` mode): ZERO baseline-specific codegen bugs.** 5447 stress files, ~5368 test-runs. 42 unique failing files, ALL classified out-of-scope by the LLInt-vs-baseline filter (a failure is a real baseline bug only if it PASSES under `--useJIT=0` but FAILS at baseline — none did):
- **~38 DFG/tier-up-requiring tests** (fail identically under LLInt): all `arith-*-on-various-types` (self-check `numberOfDFGCompiles`/"should have detected polymorphic"), `big-int-negate-jit`, `big-int-spec-to-this`, `bit-op-with-object-returning-int32` (asserts `numberOfDFGCompiles<=1`), `bitwise-not-fixup-rules`, `compare-strict-eq-on-various-types`, `arith-abs-to-arith-negate-range-optimizaton`, `for-of-const-closure-captured-tier-up`, `int8-repeat-in-then-out-of-bounds`, and `promise-prototype-catch-on-non-promise`/`regexp-prototype-{test,symbol-search}-on-non-regexp`/`string-replace-regexp-with-own-property` (these throw "too many recompiles: 1000000" — baseline keeps jettisoning/recompiling because DFG tier-up is disabled). These require Phase 4 (DFG) and are correctly out of scope.
- **4 environment artifacts** (same as Phase 2, fail under LLInt too or only under load): `typed-array-oom-in-buffer-accessor` + `unlinked-code-block-destructor` (`memoryHog!` OOM/timeout on the loaded 64 GB box — both PASS run standalone), `many-substrings-of-rope-shouldnt-use-excessive-memory` (PPC64LE 64 KB pages vs 4 KB-tuned threshold), `map-forEach` (`ReferenceError: WebAssembly` — built `ENABLE_WEBASSEMBLY=OFF`).

**Method** (mirrors Phase 2): forced baseline in `run-jsc-stress-tests` by adding `--useDFGJIT=false` to `BASE_OPTIONS`, adding `powerpc64` to the `$isFTLPlatform` exclusion (kills DFG/FTL/Wasm modes), and short-circuiting `defaultRunConfig` to the single `default` mode; then classified every failure by re-running under `--useJIT=0` vs baseline. Runner edits were local to the power9 checkout and reverted after. **Phase 3 (Baseline JIT) is complete — proceed to Phase 4 (DFG).**

### Phase 3 status — ALL 243 baseline crashes fixed (2026-07-22)

**The last crasher `allocation-sinking-puthint-control-flow.js` is FIXED (commit "reserve linkage area in throw-from-call thunks").** It was NOT a DFG issue — it was another **ELFv2 LR/TOC save collision**, same class as the nativeForGenerator fix. `throwExceptionFromCallGenerator`/`throwExceptionFromCallSlowPathGenerator` (ThunkGenerators.cpp) call `operationLookupExceptionHandler` from a tiny frame without reserving the C-call linkage area, so the callee's `std LR,16(sp)`/`std r2,24(sp)` clobbered a live call frame's codeBlock(+16)/callee(+24) slots. `genericUnwind`→`vm.topJSCallFrame()`→`isZombieFrame()`→`jsCallee()->realm()` then asserted (`JSObject.h:575`, "realmless structure") on the garbage callee while unwinding a thrown exception (the test calls a non-callable `d()` inside a hot baseline function). Fix: reserve `maxFrameExtentForSlowPathCall` before the call (exception handler resets sp). Found by instrumenting `genericUnwind` to log `vm.topCallFrame`'s callee/codeBlock — the codeBlock was a code pointer (the tell-tale of the LR-save clobber). **All 243 original baseline-only stress crashers now pass.** Next: re-run the full 500-file triage (remove the now-invalid `--maxDFGNodeCount=0` from `/tmp/triage.sh`) and run the formal Phase 3 gate (full JSTests/stress at baseline tier).

### (historical) Phase 3 status — 239/243 baseline crashes fixed (98%), 4 remain (2026-07-21)

**Session result: the 243 baseline-only stress crashers dropped to 4** via two root-cause fixes plus two missing-method stubs (all committed): (1) ELFv2 native-call LR/TOC save collision in `nativeForGenerator` [205 fixed]; (2) `farJump(TrustedImmPtr)` implemented; (3) r24/csr10 not preserved across vmEntry [eval + ~28 arith tests]; (4) `and64(TrustedImmPtr, RegisterID)` implemented. **The 4 REMAINING crashers are distinct edge cases, NOT part of the two big clusters** — investigate fresh next session:
- `array-flatmap.js` — SIGSEGV in `llint_op_tail_call` (called from `llint_op_new_array_with_species`). Tail-call frame-shuffling bug.
- `array-iterators-next-with-call.js` — SIGSEGV (likely also tail-call / call-related).
- `allocation-sinking-puthint-control-flow.js` and `ArgumentsEliminationPhase-…-nodeIndex-pass-zero.js` — DFG-phase-named; may be DFG-adjacent even with --useDFGJIT=0, or unrelated. Get backtraces first.

**RESOLVED (2026-07-21, commit "fix PPC64LE numberTag/notCellMask swap in saved-tags thunk path").** Root cause: `emitSaveThenMaterializeTagRegisters` (AssemblyHelpers.h) took the x86 `#else` branch on PPC — `push(numberTag); push(notCellMask)` lays the pair out `[sp+0]=notCellMask, [sp+8]=numberTag`, but `nativeForGenerator`'s saved-tags native entry pops with `popPair(numberTag, notCellMask)` (`numberTag←[sp+0]`) → the two tag registers came back SWAPPED. It only broke cell-checks of the pure-immediate values undefined(0xa)/null(0x2)/bool (their 0x2 OtherTag bit is exactly what moves), so mainstream JS survived. Reached via `h(){return f.call({}, ...)}` tail-calling the native `Function.prototype.call`. Fix: add `CPU(PPC64LE)` to the `pushPair`/`popPair` branch in `emitSaveThenMaterializeTagRegisters`/`emitRestoreSavedTagRegisters` so the producer matches the `popPair` consumer. **All 243 baseline stress crashers now pass.** The decisive step was catching the swapped register values (numberTag=0xfffe…02, notCellMask=0xfffe…00) with reliable register reads at the crash, then recognizing 0xa as `undefined` (not the int 10) — the empirical fix confirmed the mechanism.

**(historical) DEEP DIAGNOSIS of the 2 tail-call crashers (2026-07-21) — root cause is a TAG-REGISTER CORRUPTION, not tail-call frame shuffling.** Both `array-flatmap.js` and `array-iterators-next-with-call.js` share ONE root cause. Minimal repro: `var f=[].flatMap; function h(){return f.call({}, ()=>{})} for(i<300)h()` — crashes ONLY under `--useJIT=1` (any threshold, even 100000 where nothing JIT-compiles), works under `--useJIT=0`. The trigger is flatMap on a plain object with a MISSING `.length` (`flatMap.call([])` and `flatMap.call({length:0})` both WORK).
- **Symptom (definitive, via reliable register reads):** at the crash, `numberTag`(r22)=`0xfffe000000000002` and `notCellMask`(r23)=`0xfffe000000000000` — the two are SWAPPED (correct is numberTag=`...000`, notCellMask=`...002`). This ONLY breaks cell-checks of `undefined`(0xa)/`null`(0x2)/bool, so mainstream JS survives. `undefined & notCellMask` becomes 0 → undefined mis-identified as a cell → `arrayProfileForCall` (op_call/op_tail_call prologue, LowLevelInterpreter64.asm:2454) does `loadi JSCell::m_structureID[undefined]` → SIGSEGV. That's why the fault is `lwz r6,0(r3)` with r3=0xa in `llint_op_tail_call`/`llint_op_call`.
- **Localized to:** the corruption happens under useJIT=1 during flatMap's execution BEFORE `op_new_array_with_species` (tags already swapped at its entry), around the `op_get_by_id` path for `array.length` (the `{}`-miss slow path `llint_slow_path_get_by_id`). Tags are CORRECT at `llint_op_get_by_id` entry but wrong by the crash. The corrupting write happens inside a call gdb's register-watchpoint single-stepping doesn't decompose on PPC (so the exact instruction isn't pinned yet).
- **ELIMINATED (verified correct, do NOT re-investigate):** `pushPair`/`popPair` (MacroAssemblerPPC64.h — consistent), `preserveCalleeSavesUsedByLLInt`/`restoreCalleeSavesUsedByLLInt` for csr8/csr9 (store `csr9→-8,csr8→-16`; load `csr8←-16,csr9←-8` — consistent), `or64(TrustedImm32,src,dest)` 3-operand lowering (generates `addi 23,22,2` correct), the prologue tag setup (LowLevelInterpreter.asm:1831-1832 `move TagNumber,numberTag; addq TagOther,numberTag,notCellMask` → correct), the get_by_id IC scratch allocation (PPC `numberOfRegisters=8`, pool is r3–r10 only, never r22/r23), `performGetByIDHelper`/`valueProfile` (use t0–t3, not tags), and get_by_id-miss in isolation (doesn't repro).
- **NEXT STEP:** the swap points to a store/load or push/pop PAIR with mismatched order somewhere in the useJIT=1 get_by_id/call glue that gdb steps over — candidates not yet checked: `copyCalleeSavesToBuffer`/restore-from-buffer csr8/csr9 offsets (LowLevelInterpreter.asm ~1085/1200, the `storep csr10,80[buffer]` region), `storePair64`/`loadPair64` PPC lowering, baseline `emitSaveCalleeSavesFor`/`emitRestoreCalleeSavesFor` RegisterAtOffsetList ordering for r22/r23, and `emitSaveThenMaterializeTagRegisters`/`emitRestoreSavedTagRegisters` (AssemblyHelpers.h:425/438 — PPC uses the `#else` individual push/pop while `nativeForGenerator` consumes with `popPair`). Best tool: get a working JIT disassembler for PPC, or gdb `stepi` (not `continue`) through the get_by_id slow-path return to catch the exact swap. Then re-run the full 500-file triage (remove the now-invalid `--maxDFGNodeCount=0` from `/tmp/triage.sh`) and run the formal Phase 3 gate.

### Phase 3 status — Class A SOLVED, 205/243 baseline crashes fixed (2026-07-20)

**Class A ROOT CAUSE FOUND AND FIXED (commit "fix PPC64LE native-call frame corruption (ELFv2 LR/TOC save collision)").** All the prior Class-A theories (setupArguments marshaling, callLinkInfo load, frame arithmetic) were RED HERRINGS — the gdb register reads at JIT-operation breakpoints on PPC are systematically unreliable (they returned r3=0, r31==r4==small values across unrelated functions), which sent the earlier sessions chasing ghosts. **The reliable technique was C++ instrumentation, not gdb**: added a temporary probe in `CallFrame::callerSourceOrigin` (gated on `getenv("PPC_FRAME_PROBE")`) that walks the raw frame chain with `reinterpret_cast` memory reads and dataLogs each slot — memory reads ARE reliable on PPC, register reads at these break points are not.

The probe showed the native (host-function) call frame's **codeBlock slot (`[fp+16]`) held `intFuncThunk+0x54` — a return address into the thunk — instead of null** (LLInt correctly stores null there; baseline did not). `StackVisitor::readFrame` (DFG compiled in) reads `callFrame->codeBlock()`, sees non-null, treats the host frame as a JS frame, and dereferences the bogus pointer via `codeBlock->hasCodeOrigins()` → crash. Mechanism: **ELFv2 requires a called C function to save the caller's LR at `16(sp)` and TOC at `24(sp)` BEFORE allocating its own frame.** `nativeForGenerator` (ThunkGenerators.cpp) called the host with `sp` still at the bare 2-word JS call header, so the host's prologue wrote LR/TOC onto the JS frame's codeBlock(+16)/callee(+24) slots. Non-walking natives (Math.max, charCodeAt) never noticed; stack-walking natives (Function ctor, Error().stack) and returns from allocating natives (parseInt, sort) crashed. **Fix: reserve `maxFrameExtentForSlowPathCall` below the frame before the host call** (every other C-call site already does this; `emitFunctionEpilogue`/exception handler reset sp from fp so no restore needed). **Result: 205/243 known baseline crashers now pass (84%).** Also implemented `farJump(TrustedImmPtr)` (was a PPC64_UNIMPLEMENTED stub the arith-*-on-various-types tests hit).

**SECOND BUG — FIXED (commit "preserve r24 (csr10) across vmEntry; implement and64(TrustedImmPtr)").** The eval/callee-save leak below was root-caused and fixed: `pushCalleeSaves()` (LowLevelInterpreter.asm, the vmEntry callee-save save) saved r25–r30 but SKIPPED r24 (csr10). r24 is C-callee-saved and used by JS-entry dispatch (r24 held `llint_eval_prologue` at the crash) yet is NOT in GPRInfo's regCS0–9 (r14–r23), so `preserveCalleeSavesUsedByLLInt` (which only saves csr6–9) never covered it — r24 was preserved by nobody. A nested C→JS entry (`executeEval` running `eval(...)` under --useJIT=1) corrupted the C caller's r24 → wild store into LLInt code. Fix: grow the vmEntry callee-save area 80→96 bytes and save/restore r24 (r25–r30 and the csr6–9 window keep their exact cfr offsets; `copyCalleeSavesToBuffer` already treated csr10 as live). This unblocked all ~28 `arith-*-on-various-types` crashers (they now throw a benign DFG-required exception, not crash) plus general eval. Also implemented `and64(TrustedImmPtr, RegisterID)` (a stub the eval/arith paths reached once the leak was fixed). **NOTE the general lesson: JSC's offlineasm uses C-callee-saved GPRs (r24–r30) that are NOT in GPRInfo's regCS set; any C↔JS boundary must preserve the full set offlineasm uses, not just the JIT's regCS0–9.**

**(historical) SECOND BUG diagnosis — r24–r30 callee-save leak across a nested C→JS boundary (useJIT=1-specific).** Minimal repro: `function h(){return eval("1+2")} for(i<50)h(); print(h())` crashes under `--useJIT=1` (any threshold, even with h staying LLInt) but WORKS under `--useJIT=0`. Fault: `stb r9,-8710(r24)` inside `executeEval` (C++) writing into LLInt executable code (SIGSEGV) — because **r24 (a C-callee-saved GPR that JSC's offlineasm uses as `csr10`, and r25/r26=ws2/ws3, r27–r30=Tmp scratch per PPC64LE_EXTRA_GPRS in ppc64le.rb) is garbage after `executeEval` calls into the eval'd LLInt code**. r24–r30 are NOT in GPRInfo's regCS0–9 (=r14–r23) nor in `RegisterSet::vmCalleeSaveRegisters()`, so the useJIT=1 vmEntry/callee-save path fails to preserve them while the LLInt clobbers them. Next: find where the useJIT=1 JS-entry (vmEntry thunk / the nested eval entry) saves callee-saves and make it preserve the full C-callee-saved set r14–r31 that offlineasm actually uses (or add r24–r30 to the vm callee-save set + buffer). The ~28 arith-*-on-various-types crashers are all blocked on THIS eval bug (their `shouldBe` harness uses eval); the remaining ~10 (8bit-*, array-flatmap, aggregate-error, etc.) are unclassified — recheck after the eval fix. Triage harness note: the `--maxDFGNodeCount=0` flag in `/tmp/triage.sh` is now REJECTED as invalid (rc=134 artifact) — remove it before re-running.

**Class A (historical, now SOLVED) — Stack-walk / native-call frame corruption (DOMINANT cluster; high-leverage).** gdb-sampling shows most crashers abort in `StackVisitor::StackVisitor`. Repro even minimally: `function h(){var f=Function("return 1");return f()} for(i<100)h(); h()` crashes, as does `Error().stack` in a hot loop — ANY stack walk through a native call made from baseline-JIT code. FULLY DIAGNOSED: the abort is `RELEASE_ASSERT(result)` in `JSObject::realm()` (JSObject.h:575), inlined into the walk, because a frame's callee has no realm. Root cause pinned by manually walking the frame chain in gdb (break `JSC::CallFrame::callerSourceOrigin`, `set $cf=$r3`, read `*(long*)$cf` etc.): the **native callee frame's CallerFrame slot (`[cf+0]`) holds a JSObject pointer (e.g. 0x101cf388, which has a valid structureID 0x14b50) instead of the caller's stack fp** (should be a `0x7fff…` address). The native thunk `nativeForGenerator` (ThunkGenerators.cpp:466) sets this via `emitFunctionPrologue`, which stores `framePointerRegister`. Verified `framePointerRegister == callFrameRegister == r31` (GPRInfo.h:868) and `calleeGPR == regT0 == r3` (no *direct* r31 collision), so the corruption is subtler than a register-constant clash — likely the baseline op_call/op_construct frame-construction arithmetic (JITCall.cpp ~L141-160) or a CallFrameShuffler writing a value into the CallerFrame slot / a store landing at the wrong offset on PPC. Fixing this should clear a large fraction of the 243.
- **Class B — Arithmetic/opcode miscompiles** — like the mulp bug; fix in ppc64le.rb / MacroAssembler as they surface.

**Class A investigation progress (2026-07-19, session 2)** — narrowed substantially, not yet fixed:
- An **assertion-enabled jsc now exists**: `WebKitBuild/Asserts/bin/jsc` on power9 (configured with `-DENABLE_ASSERTS=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo`, same JIT flags; `-O2` so it's usable). Rebuild it with `ninja -C WebKitBuild/Asserts -j4 jsc`. Under it the minimal repro `function h(){var f=Function("return 1");return f()} for(i<200)h(); h()` fires `ASSERTION FAILED: m_ptr` in `Ref<JSLock>::operator->()` — i.e. `operationDefaultCall`'s `owner->vm()` where `owner = callLinkInfo->ownerForSlowPath(calleeFrame)` returns garbage.
- **Key finding (gdb, assertion build, `break operationDefaultCall`)**: at entry, `calleeFrame`(r3)=`0x44000002` and `callLinkInfo`(r4)=`0x38210080` are **garbage instruction-like words** (`0x38210080` = `addi r1,r1,128`), while `cfr`(r31)=`0x7fffffffd3a0` is VALID. So the `defaultCallThunk`'s `setupArguments<decltype(operationDefaultCall)>(GPRInfo::regT2)` (LLIntThunks.cpp) is NOT marshaling `callFrameRegister`→argumentGPR0(r3) and regT2→argumentGPR1(r4) correctly, OR the baseline caller (JITCall.cpp) loads `callLinkInfo` into regT2 via a miscompiled load (the instruction-byte values suggest a load reading CODE as data). NOTE: argumentGPR0..7 ALIAS regT0..7 (r3..r10) on PPC — watch for move-ordering clobbers in setupArguments. This could be Class B (a load/address miscompile) rather than a pure frame bug, and if it's in the shared slow-path-call marshaling it would explain a large share of the 243 crashes.
- **ELIMINATIONS (session 2, verified — do NOT re-investigate)**: (a) baseline callee-save save/restore works — `fib(20)=6765` and `fact(10)=3628800` are correct, and recursion round-trips regCS6-9 = metadataTable(r20)/jitData(r21)/numberTag(r22)/notCellMask(r23) across nested calls; (b) `setupArguments` for ordinary operations works — arithmetic slow paths (operationValueAdd/Mul) run correctly; (c) `loadPairPtr` handles the `dest1==base` aliasing in `emitMaterializeMetadataAndConstantPoolRegisters` correctly (traced by hand); (d) `emitFunctionPrologue` works (testSimple + probe tests); (e) JS→JS stack walks work (`arguments.callee.caller` returns the caller). So the bug is NARROW: the **native/host call path from baseline specifically** (Function() ctor, Error.stack machinery) — JS→JS calls and walks are fine, only native-involved frames corrupt. The RELEASE conditional watchpoint that caught `operationDefaultCall` writing `0x101cf388` was a RED HERRING (a reused stack slot during call *linking*, not the crash frame — the crash frame is built later by the native thunk). Prime remaining suspects: `CallLinkInfo::emitFastPath` (the actual call emission), the native `nativeForGenerator` thunk path, or op_construct-specific frame setup — all where a native callee's frame gets linked. The assertion-build "garbage args to operationDefaultCall" may be a separate assertion-build-codegen artifact (release links fine and crashes later in the walk); prefer reproducing on RELEASE.
- **Original do-next**: `gdb break operationDefaultCall`, read `$lr` on the RELEASE build (assertion build's `$lr` is polluted by the abort's `_Unwind_Backtrace`), disassemble ~20 insns before `$lr` to see the thunk's actual argument setup; and read the baseline caller's `callLinkInfo` load in `JIT::compileOpCall`/`JITCall.cpp`. Confirm whether r3/r4 garbage is the marshaling (thunk) or the callLinkInfo load (caller).

**Original NEXT-SESSION note**: build an **assertion-enabled jsc** (Debug or RelWithDebInfo + forced `ASSERT_ENABLED`). StackVisitor.cpp:137 already has `dataLogLn("Invalid codeblock type: ", *cell); dataLogLn("Callee: ", …)` under `#if ASSERT_ENABLED` — it will print the exact bad frame contents. Assertions also preserve locals for gdb and fire earlier/closer to the real corruption. Then finish Class A (inspect JITCall.cpp op_call frame setup + the generated baseline code — note the disassembler isn't wired for PPC, so either decode raw bytes at the JIT code range or add a minimal PPC path to the JSC disassembler). Then grind Class B and run the formal Phase 3 gate.

**(historical) First crash signature**: the compiled code segfaulted at runtime. `jsc … -e 'function f(x){var s=0;for(var i=0;i<x;i++)s+=i;return s} print(f(1000))'` (expect 499500) → SIGSEGV in `llint_function_for_call_prologue+384`, at `std r5, 0(r6)` where `r6 = <frameslot> - 24`. Frame #1 in gdb is a garbage return address `0x3821002048dec9ad` — those are raw PPC instruction bytes (`0x38210020` = `addi r1,r1,32`), i.e. an epilogue instruction sitting in a return-address slot. This is the **JIT↔LLInt shared-frame ABI**: the argument-copy / arity-fixup loop in the LLInt call prologue reads/writes bad frame slots when entered from (or transitioning with) baseline-JIT code. Likely suspects: CallerFrameAndPCSize / CallFrame header layout on PPC64, the `move`/`store` of the return-PC into the frame header vs LR handling, or the caller-frame slot arithmetic in `makeJavaScriptCall`-adjacent code. This is a focused frame-ABI investigation, distinct from the surface grind — start here next session. Repro is deterministic and single-function.

## Phase 4 — DFG (Data Flow Graph) JIT

Gate: `jsc --useFTLJIT=0 --useWasmJIT=0` (Baseline + DFG) passes the stress suite.

### Phase 4 status — DFG BRINGING UP (2026-07-23)

DFG is compiled (`ENABLE_DFG_JIT=1`, FTL off) and defaults on when JIT is on — that's why Phase 3 used explicit `--useDFGJIT=0`. **Bring-up command:** `--useJIT=1 --useBaselineJIT=1 --useDFGJIT=1 --useFTLJIT=0 --useConcurrentJIT=0 --thresholdForOptimizeAfterWarmUp=10 --thresholdForOptimizeSoon=10` (concurrent off ⇒ synchronous compile so crashes land on the main thread; `--useBaselineJIT=1` needed to make tier-up engage in short tests). **Diagnostic note:** `numberOfDFGCompiles(f)` returns the sentinel `1000000` when DFG is effectively OFF (TestRunnerUtils.cpp:74) — not a real recompile count; with DFG genuinely on it returns 1 for stable code.

**Working now** (verified correct results): monomorphic arithmetic/compare/double, array read/write, object-property inline caches, OSR exit (int→double / int→string type-change deopt), closures, `n*fact(n-1)`, float accumulation, try/catch. **DFG MacroAssembler gaps implemented this session** (all in MacroAssemblerPPC64.h, committed): `branchAdd32`/`branchSub32`/`branchMul32` (reg+imm and Address/AbsoluteAddress memory-modify forms — the store is emitted BEFORE the branch, 64-bit overflow check reused); `abortWithReason` (reason/misc→scratch, trap); `moveFixedLi64` + `moveWithPatch(TrustedImmPtr)` / `storePtrWithPatch` (fixed 5-insn li64, patchable in place by the assembler's `repatchPointer`/`linkPointer`, DataLabelPtr at the sequence START); `repatchCall` (via `relinkCall`); `replaceWithJump`/`replaceWithNops` (via the assembler jump-slot / `fillNops`). Grind method identical to Phase 3: run a DFG gauntlet with the bring-up flags, `grep unimplemented`, implement, rebuild (~2 min), repeat.

**NEXT BUGS (not fixed):**

(1) **DFG two direct calls — PARTIAL FIX committed (2026-07-25, commit `3a2c366e9ac9`); a deeper heisenbug remains.** Repro: `function f(x){return x<2?x:f(x-1)+f(x-1)} noInline(f); print(f(N))`.

> **✅ ROOT CAUSE OF THE "HEISENBUG" FOUND & BASELINE FIXED (2026-07-27, commit `49191d1`).** Valgrind memcheck WITH the JIT (`valgrind --smc-check=all-non-file --track-origins=yes` — it *runs* jsc's JIT fine) plus the rebuilt asserts build cracked it. The probabilistic corruption was **`MacroAssemblerPPC64::call(PtrTag)` returning `Call::LinkableNear` instead of `Call::Linkable`** (it was identical to `nearCall()`). `call()` is the general repatchable call used by `callOperation`; JITMathIC does `linkBuffer.locationOf(slowPathCall)` to repatch the arithmetic inline-cache slow-path call, and `locationOf` `RELEASE_ASSERT`s `!Near`. With the bogus `Near` flag, RELEASE builds silently computed the WRONG location and **repatched the wrong PC → JIT code corruption**; whether that landed somewhere fatal was ASLR/layout-sensitive → the "intractable heisenbug". Fix: `call()` → `Call::Linkable` (only `nearCall()` is `LinkableNear`; on PPC both link via the same `applyCallSlot`, so the flag only routes location/repatch). **Result: baseline JIT is now solid** — `a+b/a-b/a*b` MathICs fixed; the whole baseline gauge (push, user-call, array-write, object-write, 100k-iter loops) went from probabilistic crashes to **0/15 crashes**; default multi-tier clean. Methodology that worked (use it again): **valgrind runs the JIT**; the **asserts build** (`WebKitBuild/Asserts`, rebuilt from current source) turns silent corruption into a named `RELEASE_ASSERT`/`ASSERT` at the fault site — infinitely better than gdb/probe/ASLR whack-a-mole.
>
> **✅ SECOND FIX — DFG call-linking (commit `608c4d5`).** The `pc=0` (null call target) was `MacroAssemblerPPC64::linkCall` routing a **non-Near** call to `PPC64Assembler::linkPointer` (5-insn li64, no `mtctr`/`bctrl`) instead of `applyCallSlot`. That branch was **dead code** while `call()` wrongly returned `LinkableNear`; correcting `call()`→`Linkable` (fix #1) started taking it → the target was materialized but never called → fall-through to `pc=0`. Every `call()`/`nearCall()` emits the same 8-insn slot and must link via `applyCallSlot`; only a tail call is a jump. **Result: DFG regular calls (`g→h`) fixed, and DFG works broadly.**
>
> **✅ NET RESULT (2026-07-27): the port is largely working.** Default config (real-world) is clean — fib(30), sort, 100k push loops, Math.sqrt loops, JSON all correct. Baseline-only: 0 crashes across the gauge. **DFG with DEFAULT thresholds (the Phase 4 gate config `--useDFGJIT=1 --useFTLJIT=0`): fib(20)=6765, direct-call recursion f(12)=2048, loop-calls, array-push all 0/3 crashes.** Three real bugs fixed this session (`3a2c366` linkage-area, `49191d1` call() flag, `608c4d5` linkCall) — the first turned the "intractable probabilistic heisenbug" into deterministic bugs, the latter two fixed them.
>
> **✅ THIRD FIX — DFG direct-call relink stale calleeGPR (commit `29c0993`). The direct-call heisenbug is RESOLVED.** The `threshold=10` `f(x-1)+f(x-1)`/classic-fib crash was the `JSObject::realm()` realmless assert in `operationLinkDirectCall` because `calleeGPR` was **stale**: it's dead once the callee is stored into the staged frame (before `mainPath`), so the register allocator reused it; on a RELINK (callee tiers up → near-call reset → slow path re-enters at `mainPath`, after the materialization) the slow path passed the stale register. **A non-perturbing global-variable probe (write to `volatile` globals, read from the core — NOT gdb breakpoints, which perturbed the run to a passing execution and created the "valid callee yet realmless" paradox) proved the callee was a type-`0x12` `CodeBlockType` object with null `m_realm`, not the JSFunction.** Fix: reload `calleeGPR` from `calleeFrameSlot(CallFrameSlot::callee)` (where it was just stored) right before the operation. Result: f(6..12), fib(20)/fib(25) at threshold=10 all pass (8/8-crash → 0/5); no regression. **KEY LESSON: for perturbation-sensitive JIT bugs, capture ground truth with a `volatile`-global write + core read, never a gdb breakpoint.**
>
> **STATUS: the DFG call path is working.** Four fixes this session (`3a2c366` linkage-area, `49191d1` call() flag → baseline, `608c4d5` linkCall applyCallSlot → DFG regular calls, `29c0993` direct-call relink calleeGPR). Baseline solid; DFG works at default AND threshold=10; direct/regular/native calls, recursion, fib all green. Next: run the full Phase 4 gate (JSTests/stress at Baseline+DFG, `--useFTLJIT=0`) to find the remaining long tail.

> **🚨 CRITICAL (2026-07-28): BOTH the Phase 3 and Phase 4 stress gates were INVALID — they ran in LLInt, not JIT.** On PPC64LE `useJIT` defaults to **false** (and so do `useBaselineJIT`/`useDFGJIT`), and `Tools/Scripts/run-jsc-stress-tests`'s `BASE_OPTIONS` (line 688) never forced them on. So every "gate" test ran under the interpreter. Proof: `numberOfDFGCompiles(f)` returns the sentinel `1000000` (its `!useJIT||!useBaselineJIT||!useDFGJIT` path) with the runner's exact flags, and the first "Phase 4 gate" (48307 runs, 0 crashes, 0 real bugs) was LLInt. **This does NOT invalidate the direct `--useJIT=1` testing** (the heisenbug fixes, fib, push, recursion, arith — all verified with JIT explicitly on) — but it means the STRESS SUITE has never actually exercised baseline/DFG, so there is an unquantified tail of real JIT bugs. **RUNNER FIXED**: added `BASE_OPTIONS += ["--useJIT=1","--useBaselineJIT=true","--useDFGJIT=true"] if $architecture == "powerpc64"` after line 688 (backup `/tmp/rjst2.bak`; the `$isFTLPlatform=false` edit, backup `/tmp/run-jsc-stress-tests.bak`, is also live — revert both when done). The repro commands now carry `useJIT=1 useDFGJIT=true`. **A valid gate is now running** (`/tmp/p4gate2.log`). **Real JIT bugs already surfaced once JIT is actually on:** (a) ✅ `branchConvertDoubleToInt32` was `PPC64_UNIMPLEMENTED` (DFG `compileArithRounding`: Math.floor/ceil/round/trunc) — **FIXED, commit `212e0f2`** (fctiwz + round-trip compare + neg-zero sign check); monomorphic Math.floor/ceil/round/trunc now correct. (b) ❌ `CallFrameShuffler::prepareForTailCall()` **aborts** in baseline `compileTailCall<OpTailCall>` — a tail-call bug the `arith-*-on-various-types` ("use strict") tests trip; a minimal `f(x){return g(x)}` tail call passes, so it's register/stack-config specific. (c) polymorphic `arith-*-on-various-types` fail (3/3, SIGABRT) — same tail-call abort. **Phase 4 is NOT done — the valid gate will enumerate the real tail; grind it down (tail calls next).**
>
> **Phase 4 grind progress (2026-07-29):** Fixes landed with JIT actually on — `212e0f2` branchConvertDoubleToInt32 (DFG rounding); `6af2020` **prepareForTailCall** PPC branch (CallFrameShuffler, `m_newFrameOffset=-2` + `restoreReturnAddressBeforeReturn` mtlr) → **fixed-arg tail calls + tail recursion work** (`sum(100,0)`=5050, more/fewer/no-arg tail calls all correct); `2e8e519` **branchSub64** (was UNIMPLEMENTED: non-overflow via sub64+branchTest64, Overflow via `(a^b)&(a^result)<0`) + **prepareForTailCallSlow** PPC branch (CCallHelpers, mirrors ARM64). **STILL OPEN (WIP/next):** (a) **varargs tail calls** (`g.apply(null,arguments)` in strict mode → op_tail_call_varargs) get past the UNREACHABLE + branchSub64 but SIGSEGV in the frame shuffle downstream — the `prepareForTailCallSlow` PPC frame math/copy loop needs validation (no regression: crashed before too). (b-RESOLVED, 2026-07-29) **The baseline named-property crash was TWO separate PPC64 codegen bugs, both now fixed and committed:**
  1. **`emitCompare32/64` register aliasing** (`b0c3a55`, MacroAssemblerPPC64.h): `branch32/64(cond, Address, TrustedImm32)` loads the memory operand into `dataTempRegister` as `left`, then `emitCompare` materialized an out-of-16-bit immediate ALSO into `dataTempRegister`, clobbering `left` → `cmpw r11,r11` (always-equal). Broke every compare-memory-vs-large-immediate, incl. the get_by_id/put_by_id IC structure check (structure IDs > 16 bits) and Asserts jitAsserts. Fix: `scratch = (left==dataTempRegister) ? memoryTempRegister : dataTempRegister`.
  2. **`emitDataICPrepareForCall` was a no-op Phase-2 stub on PPC** (`cb5b619`, InlineCacheCompiler.cpp): the baseline DataIC handler is entered as a leaf with its return address live in LR (set by `bctrl` at the `jit.call(Address(handlerGPR,...))` IC site) and keeps the caller's cfr. Before its nested slow-path C call (operationGetByIdOptimize / getter / custom), the return address MUST be spilled since `bctrl` clobbers LR. The stub did nothing → handler returned through a stale LR (a JSCell in a temp) → base object landed in the frame returnPC slot → wild jump. Fix mirrors ARM_THUMB2: push a 16-byte `[fp][returnPC]` header (manual `moveFromLR` since linkRegister=r0 is a placeholder) WITHOUT changing fp, then reserve `maxFrameExtentForSlowPathCall`(96); matches `emitDataICRestoreAfterCall`'s `addPtr(96)+emitFunctionEpilogueWithEmptyFrame()`.
  **VERIFIED PASSING at forced baseline (`--useJIT=1 --useBaselineJIT=1 --useDFGJIT=0 --useFTLJIT=0`):** monomorphic get_by_id/put_by_id, get_by_id-in-loop, 2-way polymorphic, JS getters, put-transitions, method calls (proto chain), JSON round-trip, string concat, array push/sum, for-in, recursion (fib). DFG-on (default) still works — no regression.
  **UPDATE 2026-07-30: ALL THREE baseline bugs now FIXED. The two "remaining" sp-leaks were a single shared-thunk bug (`4e5d1d6`):** `emitCTIThunkPrologue/Epilogue` (CCallHelpers.cpp) did `pushPair/popPair(framePointerRegister, linkRegister)`, but PPC `linkRegister` is only an r0 placeholder — the real return address is in the LR SPR. So the shared `SlowPathCall` thunk spilled/reloaded r0 (garbage) while the true return address in LR got clobbered by the nested `callOperation` (bctrl); the tail-jump to CheckException then `blr`'d through the stale LR, re-entering the thunk epilogue in a tight loop that added `maxFrameExtentForSlowPathCall+16` to sp each turn until sp overran the stack top (SIGSEGV, sp≈0x800000000000). Fix: on PPC `moveFromLR(dataTempRegister)` before the push and `moveToLR` after the pop. This one fix cured BOTH `op_new_array_buffer`/`new_array_with_size`/spread AND the mono→poly get_by_id transition (3+ structures both route through SlowPathCall). VERIFIED PASSING at forced baseline: `[1,2,3]` in hot loops, array.length, `new Array(n)`, `Math.max` spread, 3-way & 8-way polymorphic get_by_id, typed arrays, closures, try/catch, regexp, Array.sort, Map/Set, arguments object — plus the earlier monomorphic/getter/transition/method/JSON/for-in/recursion set. No regressions. **Baseline JIT (--useDFGJIT=0) is now broadly healthy on PPC64; next: run the full JSTests/stress gate at baseline tier to quantify.**

  [Historical — the three fixes, in order: (1) `b0c3a55` emitCompare32/64 aliasing; (2) `cb5b619` emitDataICPrepareForCall stub; (3) `4e5d1d6` emitCTIThunkPrologue/Epilogue LR. All three share the theme: PPC's linkRegister=r0 is a placeholder, so any return-address save/restore must use mflr/mtlr, not a register move.]

  **`array.length` was NOT a bug — it was FIXED earlier too.** The apparent failure was a test artifact: `for(...)ff([1,2,3])` builds a fresh array LITERAL every iteration (`op_new_array_buffer`), and THAT leaks. Hoisting the array (`var arr=[1,2,3]; ...ff(arr)`) → passes. So all get_by_id/put_by_id, incl. custom-value getters like `.length`, work at baseline.
  **TWO genuinely-remaining, DISTINCT baseline bugs (both crash: sp climbs to the stack ceiling ~0x800000000000 over ~hundreds of calls — n≤100 ok, n≥500 crash — then a later handler faults on `ld r31,0(r1)`/`ld r0,8(r1)` from the corrupt sp, loading filesystem-path strings; it is an sp LEAK per fast-path call):**
    1. **`op_new_array_buffer` fast-path handler leaks sp.** Repro: `function ff(){return [1,2,3]} noInline(ff); for(50000)ff()`. Also any inline `[1,2,3]` in a hot loop. Note: `slow_path_new_array_buffer` is NOT called (fast path taken), so the leak is in the baseline fast-path handler for op_new_array_buffer (baseline JIT emission, likely JITOpcodes/the new_array_buffer handler thunk — NOT InlineCacheCompiler). `{x:1,y:2}` object literals (op_new_object + put_by_id) do NOT leak — pass.
    2. **3-way+ polymorphic get_by_id leaks sp.** Repro (array hoisted, so unrelated to #1): `var a=[{x:1},{x:2,y:3},{z:0,x:4}]; function ff(o){return o.x} noInline(ff); for(50000)ff(a[j%3])`. 2-way polymorphic and monomorphic PASS; only 3+ structures fail. Likely the polymorphic-list handler chain (`emitDataICJumpNextHandler` farJump between chained handlers) or the mono→poly IC transition running an `emitDataICRestoreAfterCall` without a matching `emitDataICPrepareForCall`.
  Next: disassemble the op_new_array_buffer fast handler and the poly-list handler; grep for a restore reachable without a prepare; measure r1 drift by breaking at the mapped fast handler once JIT'd. [Original investigation trail — superseded but kept for method:]

(b-orig) **`arith-*-on-various-types` SIGSEGV → earlier theory: BASELINE JIT named-property inline-cache bug, NOT DFG/OSR (correct direction, now root-caused above).** Tier isolation on the minimal repro proves it: `function ff(o){return o.x} noInline(ff); for(50000)ff({x:5})` → **LLInt (`--useJIT=0`) WORKS, forced BASELINE (`--useJIT=1 --useDFGJIT=0 --useFTLJIT=0`) CRASHES (139/134), DFG crashes too (it runs the baseline phase first), and DEFAULT config WORKS (tiers up to DFG fast, baseline phase too brief).** That is why the earlier "DFG" and "OSR-exit" theories were wrong and why `--dumpDFGDisassembly` was empty (never reaches DFG). Narrowed: crashes on **named-property access via the DataIC** — `get_by_id` (`o.x`) AND `put_by_id` (the `{x:5}` literal is `new_object` + `put_by_id IsDirect x=5`) both die; but **empty `{}` works, `put_by_val`/`get_by_val` (`o[k]`, `a[0]`) — wait `a[0]` also crashed, so it's broader indexed too — and passing an object without dereferencing works, and int/arith works.** Core proof of the corruption: at the crash `pc=lr=0xfffe000000000005` — a boxed JSValue (int 5 = `o.x`/the stored property value); the property value is written to the frame's **returnPC header slot** instead of the object's inline slot → the `blr` returns to a boxed-int address → wild jump (SIGSEGV/SIGABRT/SIGTRAP via `llint_op_call`), with `cfr` also zeroed. So the baseline inline-property IC (get_by_id/put_by_id DataIC) on PPC uses a **wrong base register or offset**, hitting the call frame instead of the object. **This is the single highest-leverage bug — inline named-property access is ubiquitous; it was hidden because the stress gate ran LLInt and the direct testing used DFG/default config which tiers past baseline.** POSSIBLE REGRESSION to rule out first: check whether the `linkCall`/`call()`-flag fixes (`608c4d5`/`49191d1`) broke the DataIC handler call — bisect by testing baseline `ff(o){return o.x}` at commit `49191d1` vs now. NEXT (deterministic, baseline): the fast-path store/load is inline in ff's function code (addr `0x…05c0`, NOT captured by the PPC_CODEDUMP hook which only caught the 0x7fff shared thunks) — use a WATCHPOINT: `setarch -R`, break at `op_enter_handler`, read r31 (ff cfr) on a baseline call, watch `[r31+8]`, continue to catch the store of `5`; or fix the code-dump to capture function code (0x…05c0 pool). Minimal repros: `function ff(){return {x:5}} noInline(ff); for(50000)ff()` and `function ff(o){return o.x} noInline(ff); for(50000)ff({x:5})`, both with `--useJIT=1 --useBaselineJIT=1 --useDFGJIT=0 --useFTLJIT=0`. [EARLIER (also superseded) "DFG frame-offset" note kept below for the minimization data:]

(b-old) **DFG codegen frame-offset theory (SUPERSEDED — it is baseline, see b above):** Concrete proof from a core: at the crash `pc=0xfffe000000000004`, `lr=0xfffe000000000005` — both are **JSValues** (boxed ints 4 and 5), and 5 is exactly `o.x`'s value. So the DFG stored a **local/operand (the get_by_id result) into the frame's returnPC header slot**; the subsequent `blr` jumps to a boxed-int "address" → wild jump (seen as SIGSEGV/SIGABRT/SIGTRAP via `llint_op_call`). `cfr` (r31) is likewise corrupt. **Trigger = a DFG-compiled function that does a HEAP op (get_by_id `o.x`, get_by_val `a[0]`, or object allocation) — those spill; pure-int loops are fine.** Minimal deterministic repro: `function ff(o){var s=0;for(var i=0;i<40000;i++)s+=o.x;return s} noInline(ff); print(ff({x:5}))` (exit 139) with `--useJIT=1 --useBaselineJIT=1 --useDFGJIT=1 --useFTLJIT=0 --thresholdForOptimizeAfterWarmUp=10 --thresholdForOptimizeSoon=10`. Note `function f(o){return o.x}; for(i)f(g)` (same op, no inner loop, entered by call not loop-OSR) WORKS — so it correlates with functions that spill across a loop / have a larger frame. **This is the single highest-leverage Phase 4 bug** (gates all polymorphic + heap-in-loop tests). NEXT: it's a stack-slot/frame-size miscalculation — disassemble ff's DFG code (re-add the temporary `PPC_CODEDUMP` hook in Disassembler.cpp → `powerpc64le-objdump -b binary`) and find the store of the get_by_id result that lands at the returnPC offset; compare the DFG frame-slot→offset mapping / `frameRegisterCount` to baseline. `--useOSREntryToDFG=0` does NOT fix it (so it is not loop-OSR-entry). Baseline heap ops work, so it is DFG-codegen-specific. [SUPERSEDED HYPOTHESIS follows — kept for the minimization data:] Minimization: plain `int→double` OSR **works** (`function f(x){return x+1}` fed ints then doubles → 4.5 ✓), but transitioning the speculated value to a **cell** crashes — `int→string` (`function f(x){return x*2}` fed ints then `"s"+i` → abort) and **object structure-change** (`function f(o){return o.x}` fed `{x:int}` then `{x:double,y}` → SIGSEGV) both die. Every variant is a **wild jump into JIT code reached via `llint_op_call`** (no clean assertion even in the asserts build), i.e. OSR exit reconstructs a **corrupt baseline frame** when recovering a cell value, and the next call jumps wild. Since `int→double` (unboxed number) is fine and only cell recoveries fail, the bug is in the OSR-exit value-recovery / reboxing path for `DataFormatCell`/`JSValue` on PPC (DFGOSRExit / reifyInlinedCallFrames / the recovery emitter). Repro (deterministic, `--useJIT=1 --useBaselineJIT=1 --useDFGJIT=1 --useFTLJIT=0 --thresholdForOptimizeAfterWarmUp=10 --thresholdForOptimizeSoon=10`): `function f(o){return o.x} noInline(f); for(i<20000)f({x:i}); for(i<20000)f({x:1.5,y:2}); print(f({x:9.5}))`. **This is the single highest-leverage Phase 4 bug** — it gates all polymorphic-type tests. It is a wild-jump (not a stub), so use the ground-truth method (volatile-global write in the OSR-exit C path + core read, OR disassemble the OSR-exit stub at the crash), NOT gdb breakpoints. Monomorphic Math rounding is correct (rounding fix stands); this is orthogonal to the rounding op. **Method for the grind:** run each failing stress file directly with `--useJIT=1 --useBaselineJIT=1 --useDFGJIT=1 --useFTLJIT=0 --forceUnlinkedDFG=0 --useDollarVM=true`; asserts build (`WebKitBuild/Asserts`) names the fault; `PPC64_UNIMPLEMENTED` messages point at the next MacroAssembler gap. A valid gate is running (`/tmp/p4gate2.log`, JIT forced) — but it is on the PRE-tail-call-fix binary; **re-run it on the current binary for an accurate failure count** once the open items above are closed.

> **⚠ CRITICAL REASSESSMENT (2026-07-26): the remaining heisenbug is NOT DFG-specific and NOT deterministic — it affects the BASELINE JIT too, and its manifestation is PROBABILISTIC per run (ASLR-sensitive).** A full day chasing a "deterministic DFG→native-call frame corruption" turned out to be an artifact of (a) a **stale binary** (an `rsync` that reported `ninja: no work to do` so my revert never rebuilt) and (b) **bad ASLR streaks**. On a clean rebuild the *same* repro `function g(n){var a=[];for(i<n)a.push(i)} noInline(g); for(50)g(20)` crashed 3/3, then passed 3/3, then 0/6 across consecutive commands — **pure per-run ASLR luck**. Confirmed: it hits **baseline-only** (`--useJIT=1 --useDFGJIT=0 --useFTLJIT=0`) as well as DFG; it hits **user-JS-fn calls too** (not just natives — natives/allocation just raise the per-run crash probability); the **default multi-tier config mostly passes** (tiers up fast, low incidence); a bare top-level `push` loop (no called function) passes far more often than a `noInline`'d called function. So the earlier "native-vs-user" and "DFG-vs-baseline" splits were **crash-probability differences, not categorical** — do not treat them as the boundary. **This means the Phase 3 "gate MET" is suspect** — baseline carries the same latent probabilistic corruption; the stress runner likely got lucky ASLR draws and/or its patterns have low incidence. **`setarch -R` (no-ASLR) makes a given (binary,repro) pair deterministic** and reproduces under gdb — but the outcome still flips across rebuilds (layout changes). **The root cause is a probabilistic memory-corruption in the PPC JIT call path (frame/callee slot clobber), exposed by exact process memory layout — the specifics below (realmless assert, wasm-thunk wild target, corrupt callee slot) are all faces of this one corruption.** NEXT: stop the gdb/probe/ASLR whack-a-mole — build with a **sanitizer (ASAN)** or run under **valgrind/memcheck** (accepting the JIT-code caveats) to catch the invalid store at its source deterministically, regardless of the probabilistic surface manifestation.

**FIXED — ELFv2 linkage clobber in the direct-call link slow path** (DFGSpeculativeJIT64.cpp `emitCall`, `#if CPU(PPC64LE)`): the direct-call fast path stages the callee frame at `[sp+0..]` and re-enters `mainPath` *after* the callee/argCount/this stores; `operationLinkDirectCall` (a C fn) saved LR@`16(sp)`/TOC@`24(sp)` = the staged frame's argCount/this slots, and the mainPath re-entry never repaired them. Fix: reserve `maxFrameExtentForSlowPathCall` (96 B, verified nonzero for PPC in MaxFrameExtentForSlowPathCall.h) below the staged frame around the `callOperation`, restore after. This made **shallow** two-call recursion pass (f(3)/f(4)/f(5) → 4/8/16 ✓); no regression (baseline, single-call recursion, DFG arith/loops/object-ICs all green).

**STILL BROKEN — perturbation-resistant heisenbug at deeper recursion** (n≥6, e.g. f(6)). Crash = the `JSObject::realm()` RELEASE_ASSERT (JSObject.h:575, "realmless structure") *inside* `operationLinkDirectCall`, called from DFG call-site 2 (return addr confirmed via op's saved LR in the core — NOT a stale-stack guess). **Extensively disproven this session (do NOT re-investigate these):** the codegen is correct — call site 2 bakes `calleeGPR`(r4) via `lis r4,9801; ori r4,r4,11008` = valid f, `std r4,8(sp)`, and **r4 is provably never rewritten between the callee store and the `bctrl`** (full core + live disasm). The staged frame `[sp+8]`=callee, `[sp+16]`=argCount=2, `[sp+24]`=this, `[sp+32]`=arg0 are all valid in the core. The callee object (header, executable, scope) and its Structure (`base 0x7ffe00000000 + structureID`, `m_realm` at struct+0x28) are valid. When observed at op entry via dprintf, `callee=…2b00`, `structureID=0x120e0`, `structure=0x7ffe000120e0`, `m_realm` NON-NULL — realm() *cannot* fail on what we observe, yet unobserved it asserts. **NOT concurrent-GC** (`--useConcurrentGC=0` still crashes). Single-threaded, layout/timing-sensitive: n=6 crashes but n=7 passes (non-monotonic); only `--thresholdForOptimizeAfterWarmUp=10` crashes n=6 (5/20/50/100 pass); `--collectContinuously=1` makes n=7/8 crash too (GC-timing gated). **Any in-process observation hides it** (WTFLogAlways, a single `volatile` global store, gdb breakpoints, even non-stopping `dprintf` at the realm check) → the value op reads as the callee/structure is transiently garbage from **uninitialized stack/heap or a register-liveness gap**, exposed by exact memory layout.

**KEY TOOL for next session — no-ASLR gives a reliable repro under gdb:** `setarch -R <jsc> …` crashes deterministically AND reproduces under gdb (normal ASLR runs hide it under gdb). Get a core via `coredumpctl`, decode structure = `0x7ffe00000000 + (structureID & ~1)`, m_realm at `+0x28`. **NEXT:** build with a scrubbing/zap allocator (`--scribbleFreeCells=1`, JSC zap options) so the garbage callee becomes a recognizable pattern, OR audit DFG `emitCall` register liveness for `calleeGPR` across the *first* call's slow-path `callOperation` (does op clobber a caller-saved reg the 2nd call assumes live? the 2nd callee is re-baked so it *shouldn't* — but the arg0 reload `ld r4,-72(r31)` and spills `std r3,-64(r31)` interleave). Debug probes (PPC_DIRECTCALL_PROBE, PPC_LINK_PROBE, PPC_CODEDUMP, g_ppc_last_callee) all REVERTED — reintroduce via no-ASLR gdb dprintf, not in-process logging.

**REFRAMED 2026-07-25 — the direct-call heisenbug is one facet of a broader, RELIABLE, deterministic bug: DFG calling a NATIVE/host function corrupts the caller frame.** Isolation (all `setarch -R`, minimal `50×g(20)`): DFG functions that call a **native** crash reliably — `a.push(i)`, `a.indexOf(2)`, `Math.abs(x)` all SIGSEGV; but calling a **user JS function** `h(i)` WORKS (→1275), and inline allocation `[]`/`{}` WORKS. Pure arithmetic works. **Not GC-dependent** (huge `--gcMaxHeapSize` still crashes), **not a heisenbug** (5/5 crash, reproduces under gdb without no-ASLR games). Crash signature (release core): a **misaligned return address** (e.g. `0x100d5691` — PPC insns are 4-byte aligned) in the DFG frame's returnPC slot, pointing into a **DATA region** (JSValues/metadata: `.long 0x2,0xd,0x7fff…`), so `blr` jumps into data → wild code (`wasm_to_js_wrapper_entry`/`operationGetWasmCalleeStackSize` are just nearest-symbol noise). ⇒ the DFG frame's returnPC (or an adjacent slot) is **overwritten during a native call** — the ELFv2 LR/TOC-save-into-caller's-linkage class (same family as the Phase 3 `nativeForGenerator`/`throwExceptionFromCall` fixes and the direct-call fix above), but on the DFG→native path specifically. Baseline→native works (Phase 3 gate passed), so it's DFG-specific: likely the DFG stages the native's call frame at an `sp` where the native entry's (or the host C fn's) `std LR,16(sp)`/`std r2,24(sp)` lands on a live DFG frame slot, and/or the DFG's callee-save (r14–r23 hold JS values) interaction with `nativeForGenerator`. The two-direct-call heisenbug (realmless assert, GC-gated) is plausibly the same clobber hitting the callee slot instead of returnPC, made timing-sensitive by GC. **LOCALIZED via the (now rebuilt) assertion build** (`WebKitBuild/Asserts/bin/jsc`, current source, has symbols; reproduces the crash): minimal repro `function g(n){var a=[];for(var i=0;i<n;i++)a.push(i);return a.length} noInline(g); for(i<50)g(20)` (no `setarch` needed — 3/3 crash). Symbolized: SIGSEGV in `Wasm::operationGetWasmCalleeStackSize(functionInfo=0x0)` ← `wasm_to_js_wrapper_entry` — **red-herring wasm symbols**; `useWasm=0`, and gdb can't unwind JIT frames so its `#2+` are data pointers on the stack (spilled JSValues/butterflies like `{structureID, 0x010e2400 typeblob, …}`), not real frames. The REAL caller (from the wasm thunk's LR at entry) is **`llint_op_call_ignore_result+360`**: `ld r8,32(r8); mr r12,r8; mtctr r12; bctrl` — i.e. the call loads the callee's code-entry from `[executable+32]` and it is `wasm_to_js_wrapper_entry` (WRONG). Reading the staged call frame at the thunk (r1): **`callee[cfr+24]=0`, `codeBlock[cfr+16]=0`, `argumentCount[cfr+32]=0x1008e328` (a POINTER where a small int belongs)** — the DFG staged a **malformed callee frame** (null callee/codeBlock, a pointer in the argc slot), so the generic call path dereferences garbage and lands in the wasm thunk. This is the ELFv2 frame-slot-clobber family (argc slot holding a pointer ↔ the `std LR/TOC,16/24(sp)` pattern) but on the **DFG→native staged-frame** path. **NEXT (tractable, deterministic): under the asserts build, `break wasm_to_js_wrapper_entry`, then walk back to the DFG code that staged the frame and dump the staged `[sp+0..40]` right after the DFG's call setup vs. right before the `bctrl` — find which store zeroes the callee/codeBlock slots or writes the pointer into argc. Compare the DFG native-call frame staging to baseline's (baseline + same workload PASSES). Prime suspect: the DFG's outgoing-call frame store sequence and/or a callOperation between the frame stores and the call whose ELFv2 LR/TOC save lands on the staged callee/codeBlock/argc slots (same fix shape as the committed direct-call linkage reservation, but for the general/native call path).**

**ASSERTS-BUILD CAVEAT (2026-07-25):** `WebKitBuild/Asserts/bin/jsc` was rebuilt with current source (has symbols, `abortWithReason` now implemented). It reproduces the DFG→native crash AND gave the symbolized localization above — BUT it *also crashes the same workload under **baseline** (`--useDFGJIT=0`)*, which the **release** build does NOT (release baseline push → 200; Phase 3 gate passed). So the asserts build has an additional, PPC-specific crash of its own — almost certainly the debug-only jit-assert emission (`jitAssertCodeBlockOnCallFrameWithType` etc. at AssemblyHelpers.cpp, emitted only when ASSERT_ENABLED) exercising a buggy MacroAssembler path. Treat asserts-build baseline crashes as a *separate* bug; trust the release build for the DFG-vs-baseline distinction. The `wasm_to_js_wrapper_entry` wrong-target reproduces in the **release** core too, so the corrupt-frame finding is real. TODO(separate): fix the asserts-build baseline crash (audit the MacroAssembler ops used by AssemblyHelpers jitAssert* on PPC) — a working asserts build is valuable for all future JIT work. LLInt-only (`--useJIT=0`) passes everything.**

(2) **reentrant natives** (`sort` with a comparator) crash — same DFG→native call-path bug as the reframed (1). (3) `moveWithPatch(TrustedImm32)`/`branchPtrWithPatch` still stubs — implement when hit (need `repatchInt32`/`readInt32` for the 2-insn lis+ori form). Then run the Phase-4 gate (stress at Baseline+DFG, `--useFTLJIT=0`).

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

## Immediate next actions (updated 2026-07-13)

Phase 0 done; Phase 1 (assembler skeleton) built and linked; Phase 2 (LLInt) is the active front — LLInt executes JS but crashes on the `op_call` return path. See "Phase 2 status" above for the full picture.

1. **Fix the `op_call_return_location` crash** — the JS→JS call/return convention in the ppc64le offlineasm backend. Minimal repro: `jsc --useJIT=0 --useWasm=0 -e 'print(1); print(2)'` — prints 1, segfaults before 2, every time. Follow the "Next debugging steps" in the Phase 2 status section. Everything else in Phase 2 is blocked on this. (Post-rebase re-validation: done 2026-07-14; overflow/conversion arithmetic: fixed and verified, see status.)
2. **Fix the latent lowerings found during the overflow work** (see status list): `truncated2is` FPR-clobber + LE word pick, `bdneq` placeholder, `cnttzw` P9 gate, then the broader i-op width audit.
3. **Remove the `ppc64le call dbg` asm comment** from ppc64le.rb once the call classification is validated.
4. **Evaluate mimalloc on 64K pages** (carried over) — try `-DUSE_MIMALLOC=ON` on the box once Phase 2 is green; drop `USE_SYSTEM_MALLOC=ON` if clean.
5. **Upstream bytecode-cache race** (carried over, out-of-band) — report to WebKit upstream with the reproducer in the Phase 0 section.

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
