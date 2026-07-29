/*
 * Copyright (C) 2026 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <wtf/Platform.h>

#if ENABLE(ASSEMBLER) && CPU(PPC64LE)

#include "AbstractMacroAssembler.h"
#include "PPC64Assembler.h"

// MacroAssemblerPPC64 — Phase 1 skeleton.
//
// This class exists only so a JSC build with ENABLE_JIT=ON can link on
// PPC64LE. Every operation currently traps at runtime via
// UNREACHABLE_FOR_PLATFORM(). The actual instruction lowering will be
// filled in Phase 3 (Baseline JIT) — before any code path that calls a
// stub is exercised, we will have working implementations.
//
// File structure and register-role conventions mirror
// MacroAssemblerRISCV64.h (most recent clean new-arch template). The
// *semantic* reference — what each method needs to do, how it interacts
// with OSR exit, IC patching, etc. — is MacroAssemblerARM64.h. See
// PLAN.md §"Architecture survey" for the rationale.

namespace JSC {

using Assembler = TARGET_ASSEMBLER;

// Unimplemented-method trap: names the missing method on the way down so
// each testmasm/bring-up iteration identifies its next target directly.
#define PPC64_UNIMPLEMENTED() do { \
        WTFLogAlways("PPC64 MacroAssembler unimplemented: %s", __PRETTY_FUNCTION__); \
        RELEASE_ASSERT_NOT_REACHED(); \
    } while (0)

class MacroAssemblerPPC64 : public AbstractMacroAssembler<Assembler> {
public:
    static constexpr unsigned numGPRs = 32;
    static constexpr unsigned numFPRs = 32;

    // PPC64 I-form unconditional branch has ±32 MB reach (24-bit signed
    // word displacement); bclr/bcctr require setting up LR/CTR first and
    // have no displacement at all. For MacroAssembler purposes the
    // "near-jump" bound is the cheap-form reach — we will revisit when
    // the real Jump machinery lands.
    static constexpr size_t nearJumpRange = 32 * MB;

    // Scratch pool per ELFv2: r11 is the environment / PLT-call scratch,
    // r12 is the function-entry / static-chain scratch. Both are volatile
    // and not used by our JIT-emitted ABI. See PLAN.md §"Quick reference:
    // register convention to adopt". f0 and f1 are used for FP scratch.
    static constexpr RegisterID dataTempRegister = PPC64Registers::r11;
    static constexpr RegisterID memoryTempRegister = PPC64Registers::r12;
    static constexpr FPRegisterID fpTempRegister = PPC64Registers::f0;
    static constexpr FPRegisterID fpTempRegister2 = PPC64Registers::f1;

    static constexpr RegisterID InvalidGPRReg = PPC64Registers::InvalidGPRReg;

    RegisterID scratchRegister()
    {
        RELEASE_ASSERT(m_allowScratchRegister);
        return dataTempRegister;
    }

    // ABI-mandated register roles (ELFv2 §2.2.1). The MacroAssembler reads
    // these constants when setting up frames and indirect calls.
    static constexpr RegisterID stackPointerRegister = PPC64Registers::sp;   // r1
    static constexpr RegisterID framePointerRegister = PPC64Registers::fp;   // r31
    // PPC has no general "link register" GPR — LR is a dedicated SPR.
    // We set linkRegister to r0 as a placeholder that's never allocated;
    // callers that need LR must go through mflr/mtlr. This matches the
    // fact that any ABI caller-save pattern that would mention LR also
    // needs an mflr sequence, not a regular register move.
    static constexpr RegisterID linkRegister = PPC64Registers::r0;

    // Feature flags consulted by JSC when generating code. Both default
    // to conservative "not yet supported" until their implementations
    // land; overriding can be done as instruction coverage grows.
    static bool supportsFloatingPointRounding() { return false; }
    static bool supportsFloat16() { return false; }

    // Conditions. Values are opaque to callers — the branch-emitting
    // methods (branch32, branchPtr, branchAdd32, etc.) will decode these
    // to the appropriate (BO, BI) tuple when we implement them. For now
    // they only need to exist as a type so that ABI-agnostic JIT code
    // that references MacroAssembler::Equal compiles.
    enum RelationalCondition : uint8_t {
        Equal,
        NotEqual,
        Above,
        AboveOrEqual,
        Below,
        BelowOrEqual,
        GreaterThan,
        GreaterThanOrEqual,
        LessThan,
        LessThanOrEqual,
    };

    static constexpr RelationalCondition invert(RelationalCondition cond)
    {
        switch (cond) {
        case Equal:              return NotEqual;
        case NotEqual:           return Equal;
        case Above:              return BelowOrEqual;
        case AboveOrEqual:       return Below;
        case Below:              return AboveOrEqual;
        case BelowOrEqual:       return Above;
        case GreaterThan:        return LessThanOrEqual;
        case GreaterThanOrEqual: return LessThan;
        case LessThan:           return GreaterThanOrEqual;
        case LessThanOrEqual:    return GreaterThan;
        }
        return Equal; // unreachable; silences -Wreturn-type
    }

    enum ResultCondition : uint8_t {
        Carry,
        Overflow,
        Signed,
        PositiveOrZero,
        Zero,
        NonZero,
    };

    enum ZeroCondition : uint8_t {
        IsZero,
        IsNonZero,
    };

    enum DoubleCondition : uint8_t {
        DoubleEqualAndOrdered,
        DoubleNotEqualAndOrdered,
        DoubleGreaterThanAndOrdered,
        DoubleGreaterThanOrEqualAndOrdered,
        DoubleLessThanAndOrdered,
        DoubleLessThanOrEqualAndOrdered,
        DoubleEqualOrUnordered,
        DoubleNotEqualOrUnordered,
        DoubleGreaterThanOrUnordered,
        DoubleGreaterThanOrEqualOrUnordered,
        DoubleLessThanOrUnordered,
        DoubleLessThanOrEqualOrUnordered,
    };

    // ===================================================================
    // Method implementations begin here. Phase 1 strategy:
    //   * Methods named in PLAN.md "minimal compile" list (nop, breakpoint,
    //     move, load64, store64, add64, sub64, jump, call, ret, push, pop)
    //     get real implementations that emit the right Power ISA.
    //   * All other MacroAssembler methods get `UNREACHABLE_FOR_PLATFORM()`
    //     bodies, added on demand as the JIT/DFG/FTL/Wasm builds surface
    //     them. This keeps unverified code from accumulating.
    //
    // Cross-platform reference for what each method's BODY needs to do:
    // MacroAssemblerARM64.h. We follow ARM64's contract here and use
    // PPC64Assembler primitives to emit the right encodings.
    // ===================================================================

    // nop — single-instruction no-op. Power ISA preferred form is
    //   `ori 0, 0, 0` → 0x60000000.
    void nop()
    {
        m_assembler.nop();
    }

    // breakpoint — emit a trap that delivers SIGTRAP to the process when
    //   executed. Used by JSC's JIT-debug paths and as a marker after
    //   unreachable codegen. Power ISA encoding: `tw 31, 0, 0` → 0x7fe00008.
    void breakpoint()
    {
        m_assembler.trap();
    }

    // move — 64-bit register-to-register copy. The `if (src != dest)`
    // guard avoids emitting a no-op `mr rN, rN` (self-move) which some
    // JIT call sites emit redundantly. Power ISA: `mr RA, RS` is the
    // simplified mnemonic for `or RA, RS, RS` (X-form, opcode 31,
    // XO=444). Verified earlier: mr 7,8 → 0x7d074378.
    void move(RegisterID src, RegisterID dest)
    {
        if (src != dest)
            m_assembler.mr(dest, src);
    }

    // move — load a 32-bit signed immediate, sign-extended to 64 bits.
    //
    // Three encoding strategies depending on the value:
    //   (a) v ∈ [-32768, 32767]    →  addi dest, r0, v       (1 insn, "li")
    //   (b) v & 0xFFFF == 0        →  lis  dest, v >> 16     (1 insn)
    //   (c) general                →  lis  dest, hi16
    //                                 ori  dest, dest, lo16  (2 insns)
    //
    // Why lis+ori (not lis+addi): addi sign-extends its 16-bit immediate.
    // For values where the LOW-16 bit pattern has bit 15 set (e.g.
    // 0x7FFF8000), lis + addi would subtract from the lis-loaded value
    // because addi would treat 0x8000 as -32768. ori does NOT sign-extend,
    // so it cleanly OR-merges the low 16 bits regardless of the high bit.
    //
    // Sign-extension of the final 32-bit result up to 64 bits happens
    // automatically because lis sign-extends its imm<<16 to 64 bits;
    // ori only writes the low 16 bits and leaves the rest untouched.
    void move(TrustedImm32 imm, RegisterID dest)
    {
        int32_t v = imm.m_value;
        if (v >= INT16_MIN && v <= INT16_MAX) {
            // case (a): 16-bit signed range — single addi suffices.
            m_assembler.addi(dest, PPC64Registers::r0, static_cast<int16_t>(v));
            return;
        }

        // Extract the high 16 bits of v as a 16-bit pattern (unsigned shift
        // first to avoid implementation-defined behavior of right-shifting
        // a signed negative value, then narrow to int16_t).
        int16_t hi16 = static_cast<int16_t>(static_cast<uint32_t>(v) >> 16);
        uint16_t lo16 = static_cast<uint16_t>(v);

        if (lo16 == 0) {
            // case (b): low 16 zero — single lis suffices.
            m_assembler.lis(dest, hi16);
            return;
        }

        // case (c): general 32-bit immediate.
        m_assembler.lis(dest, hi16);
        m_assembler.ori(dest, dest, lo16);
    }

    // move — load a 64-bit pointer immediate. Always emits the
    // 5-instruction full sequence; small-pointer optimization can be
    // added when MacroAssembler is integrated and we can profile.
    //
    //   lis  dest, bits[48:63]              (sign-extends into upper 48)
    //   ori  dest, dest, bits[32:47]         (set bits[15:0] of dest)
    //   sldi dest, dest, 32                  (shift up; upper 32 = old low 32)
    //   oris dest, dest, bits[16:31]         (set bits[31:16] of low half)
    //   ori  dest, dest, bits[0:15]          (set bits[15:0] of low half)
    //
    // Trace for v = 0x123456789ABCDEF0:
    //   after lis(0x1234)         dest = 0x0000000012340000
    //   after ori(_, 0x5678)      dest = 0x0000000012345678
    //   after sldi 32             dest = 0x1234567800000000
    //   after oris(_, 0x9ABC)     dest = 0x123456789ABC0000
    //   after ori(_, 0xDEF0)      dest = 0x123456789ABCDEF0  ✓
    //
    // The lis sign-extension is handled correctly: for high pointers
    // with bit 63 set, the lis-loaded sign extension contributes the
    // intended top bits before the sldi shifts the value into place.
    void move(TrustedImmPtr imm, RegisterID dest)
    {
        uintptr_t v = reinterpret_cast<uintptr_t>(imm.m_value);

        // Cast through unsigned types first to avoid implementation-defined
        // behavior of narrowing a wider signed value; the final
        // static_cast<int16_t>(uint16_t) is well-defined on all 2's-complement
        // platforms (and standardized in C++20).
        int16_t  hiHi16 = static_cast<int16_t>(static_cast<uint16_t>(v >> 48));
        uint16_t hiLo16 = static_cast<uint16_t>(v >> 32);
        uint16_t loHi16 = static_cast<uint16_t>(v >> 16);
        uint16_t lo16   = static_cast<uint16_t>(v);

        m_assembler.lis(dest, hiHi16);
        m_assembler.ori(dest, dest, hiLo16);
        m_assembler.sldi(dest, dest, 32);
        m_assembler.oris(dest, dest, loHi16);
        m_assembler.ori(dest, dest, lo16);
    }

    // ===================================================================
    // 64-bit add. Power ISA: `add RT, RA, RB` → RT = RA + RB (XO-form).
    // For immediate variants: `addi RT, RA, SI` → RT = RA + sign_extend(SI).
    // ===================================================================

    // dest = dest + src
    void add64(RegisterID src, RegisterID dest)
    {
        m_assembler.add(dest, dest, src);
    }

    // dest = src1 + src2
    void add64(RegisterID src1, RegisterID src2, RegisterID dest)
    {
        m_assembler.add(dest, src1, src2);
    }

    // dest = dest + imm
    void add64(TrustedImm32 imm, RegisterID dest)
    {
        add64(imm, dest, dest);
    }

    // dest = src + imm
    void add64(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        if (imm.m_value >= INT16_MIN && imm.m_value <= INT16_MAX) {
            m_assembler.addi(dest, src, static_cast<int16_t>(imm.m_value));
            return;
        }
        // Out of 16-bit range: load imm into scratch, then add.
        // Use dataTempRegister (r11) as the scratch.
        move(imm, dataTempRegister);
        m_assembler.add(dest, src, dataTempRegister);
    }

    // ===================================================================
    // 64-bit subtract. Power ISA: `subf RT, RA, RB` → RT = RB - RA
    // (note the REVERSED operand order versus most ISAs).
    // ===================================================================

    // dest = dest - src
    void sub64(RegisterID src, RegisterID dest)
    {
        m_assembler.subf(dest, src, dest);
    }

    // dest = src1 - src2
    void sub64(RegisterID src1, RegisterID src2, RegisterID dest)
    {
        // subf reverses: RT = RB - RA, so pass (dest, src2, src1).
        m_assembler.subf(dest, src2, src1);
    }

    // dest = dest - imm
    void sub64(TrustedImm32 imm, RegisterID dest)
    {
        sub64(imm, dest, dest);
    }
    void add64(TrustedImm64 imm, RegisterID dest) { add64(imm, dest, dest); }
    void add64(TrustedImm64 imm, RegisterID src, RegisterID dest)
    {
        if (isInt16(imm.m_value))
            m_assembler.addi(dest, src, int16_t(imm.m_value));
        else {
            moveImmToScratch(imm.m_value, dataTempRegister);
            m_assembler.add(dest, src, dataTempRegister);
        }
    }
    void sub64(TrustedImm64 imm, RegisterID dest)
    {
        if (isInt16(-imm.m_value) && imm.m_value != INT64_MIN)
            m_assembler.addi(dest, dest, int16_t(-imm.m_value));
        else {
            moveImmToScratch(imm.m_value, dataTempRegister);
            m_assembler.subf(dest, dataTempRegister, dest);
        }
    }
    void xor64(TrustedImm64 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.xor_(dest, dest, dataTempRegister);
    }
    void and64(TrustedImm64 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.and_(dest, src, dataTempRegister);
    }
    void xor64(TrustedImm64 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.xor_(dest, src, dataTempRegister);
    }
    void or64(TrustedImm64 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.or_(dest, src, dataTempRegister);
    }
    void or64(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);   // sign-extend
        m_assembler.or_(dest, src, dataTempRegister);
    }
    void and64(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        m_assembler.and_(dest, src, dataTempRegister);
    }
    void xor64(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        m_assembler.xor_(dest, src, dataTempRegister);
    }

    // dest = src - imm (operand order used by DFG/thunks)
    void sub64(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        sub64(imm, src, dest);
    }

    // dest = src - imm
    void sub64(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        // Implement as `dest = src + (-imm)` when -imm fits in int16_t.
        // The lower bound is -INT16_MAX rather than INT16_MIN because
        // negating INT16_MIN (= -32768) overflows int16_t.
        if (imm.m_value >= -INT16_MAX && imm.m_value <= INT16_MAX) {
            m_assembler.addi(dest, src, static_cast<int16_t>(-imm.m_value));
            return;
        }
        // Out of negatable-int16 range: materialize imm in scratch, subf.
        move(imm, dataTempRegister);
        m_assembler.subf(dest, dataTempRegister, src);
    }

    // ===================================================================
    // 64-bit memory access. Power ISA `ld` / `std` are DS-form: 14-bit
    // signed displacement, 4-byte aligned (low 2 bits of byte offset
    // forced to 0 — they hold the DS-form XO field). For arbitrary
    // offsets we fall through to compute-address-into-scratch + ldx/stdx.
    // ===================================================================

    void load64(Address address, RegisterID dest)
    {
        int32_t offset = address.offset;
        // ld can encode offset in [-32768, 32767] AND multiple of 4.
        if (offset >= INT16_MIN && offset <= INT16_MAX && (offset & 0x3) == 0) {
            m_assembler.ld(dest, static_cast<int16_t>(offset), address.base);
            return;
        }
        // Out-of-range / unaligned offset: materialize the offset in
        // memoryTempRegister (r12), then load via indexed form.
        move(TrustedImm32(offset), memoryTempRegister);
        m_assembler.ldx(dest, address.base, memoryTempRegister);
    }

    void store64(RegisterID src, Address address)
    {
        int32_t offset = address.offset;
        if (offset >= INT16_MIN && offset <= INT16_MAX && (offset & 0x3) == 0) {
            m_assembler.std(src, static_cast<int16_t>(offset), address.base);
            return;
        }
        move(TrustedImm32(offset), memoryTempRegister);
        m_assembler.stdx(src, address.base, memoryTempRegister);
    }

    // ===================================================================
    // Return / push / pop. JSC's MacroAssembler push/pop manipulate a
    // GPR-sized stack slot (8 bytes on PPC64). They are NOT function
    // prologue/epilogue — those have their own ELFv2-shaped sequences
    // (LR save area at +16, parameter save area, TOC restore, etc.) and
    // get emitted by higher-level frame-setup helpers.
    // ===================================================================

    static constexpr int stackSlotSize = 8;

    // Return — branch to LR. Power ISA `blr` (= bclr with BO=20=always).
    // Verified: blr → 0x4e800020.
    void ret()
    {
        m_assembler.blr();
    }

    // Push: pre-decrement SP by 8 and atomically store RS at the new SP.
    // The stdu update form does both in one instruction.
    // Verified: stdu r3, -8(r1) → 0xf861fff9.
    void push(RegisterID src)
    {
        m_assembler.stdu(src, -stackSlotSize, stackPointerRegister);
    }

    // Pop: load from SP, then post-increment SP by 8. Two instructions
    // because PPC has no `ld with post-update` (ldu is pre-update,
    // which gives the wrong address for pop). Order matters: load first,
    // then bump SP — otherwise the load would read past the stack frame.
    // Verified: ld r3, 0(r1) → 0xe8610000;  addi r1, r1, 8 → 0x38210008.
    void pop(RegisterID dest)
    {
        m_assembler.ld(dest, 0, stackPointerRegister);
        m_assembler.addi(stackPointerRegister, stackPointerRegister, stackSlotSize);
    }

    // ===================================================================
    // Stub block — minimal one-overload-per-method UNREACHABLE_FOR_PLATFORM
    // bodies. These exist solely to satisfy the `using MacroAssemblerBase::*`
    // declarations in MacroAssembler.h. Every method here is unimplemented
    // and will trap at runtime; real bodies will be added when a JIT/DFG/FTL
    // call site surfaces a need for it during build iteration.
    //
    // Each stub provides the most common (RegisterID, RegisterID) /
    // (TrustedImm32, RegisterID) overload — the C++ compiler will report
    // additional missing overloads at actual call sites, which we add
    // one-at-a-time as the build iteration drives them.
    // ===================================================================

    // ===================================================================
    // Phase 3 core — real implementations.
    //
    // Register contract: r11 (dataTemp) and r12 (memoryTemp) are the two
    // assembler scratches (SM convention; neither is JIT-allocatable).
    // Width contract: every 32-bit op zero-extends its result into the
    // 64-bit register, matching x86 32-bit ops and arm64 Wn writes (and
    // the offlineasm i-op contract established in Phase 2).
    // Condition mapping: compares set CR0 (LT=bit0, GT=bit1, EQ=bit2);
    // branches test one CR0 bit with bo=12 (bit set) / bo=4 (bit clear).
    // ===================================================================

    static bool isInt16(int64_t v) { return v >= INT16_MIN && v <= INT16_MAX; }
    static bool isUInt16(int64_t v) { return v >= 0 && v <= 0xFFFF; }

    void zeroExtend32ToWordInternal(RegisterID reg)
    {
        m_assembler.rldicl(reg, reg, 0, 32);   // clrldi reg, reg, 32
    }

    void moveImmToScratch(int64_t value, RegisterID scratch)
    {
        if (isInt16(value)) {
            m_assembler.addi(scratch, PPC64Registers::r0, int16_t(value));
            return;
        }
        if (value >= INT32_MIN && value <= INT32_MAX) {
            m_assembler.lis(scratch, int16_t(uint16_t(uint64_t(value) >> 16)));
            m_assembler.ori(scratch, scratch, uint16_t(value));
            return;
        }
        m_assembler.lis(scratch, int16_t(uint16_t(uint64_t(value) >> 48)));
        m_assembler.ori(scratch, scratch, uint16_t(uint64_t(value) >> 32));
        m_assembler.rldicr(scratch, scratch, 32, 31);
        m_assembler.oris(scratch, scratch, uint16_t(uint64_t(value) >> 16));
        m_assembler.ori(scratch, scratch, uint16_t(value));
    }

    // Fixed-width 5-instruction li64 (never optimized), so the sequence can be
    // rewritten in place by PPC64Assembler::repatchPointer / linkPointer. This
    // is the emit-side twin of writeLi64 — used by the *WithPatch primitives.
    void moveFixedLi64(RegisterID dest, uint64_t value)
    {
        m_assembler.lis(dest, int16_t(uint16_t(value >> 48)));
        m_assembler.ori(dest, dest, uint16_t(value >> 32));
        m_assembler.rldicr(dest, dest, 32, 31);
        m_assembler.oris(dest, dest, uint16_t(value >> 16));
        m_assembler.ori(dest, dest, uint16_t(value));
    }

    // Compute an Address / BaseIndex effective address into `dest`
    // (which may be memoryTempRegister).  Returns the base register and
    // a 16-bit displacement usable directly in a D-form access when
    // possible, else (dest, 0) after materializing.
    struct ResolvedAddress {
        RegisterID base;
        int16_t offset;
    };

    ResolvedAddress resolveAddress(Address address, RegisterID scratch)
    {
        if (isInt16(address.offset) && address.base != PPC64Registers::r0)
            return { address.base, int16_t(address.offset) };
        moveImmToScratch(address.offset, scratch);
        m_assembler.add(scratch, scratch, address.base);
        return { scratch, 0 };
    }

    ResolvedAddress resolveAddress(BaseIndex address, RegisterID scratch)
    {
        if (address.scale) {
            m_assembler.sldi(scratch, address.index, address.scale);
            m_assembler.add(scratch, scratch, address.base);
        } else
            m_assembler.add(scratch, address.index, address.base);
        if (isInt16(address.offset))
            return { scratch, int16_t(address.offset) };
        // Large offset: materialize and add.  (An addis high-adjust trick
        // overflows int16 for offsets >= 0x7FFF8000 — testmasm exercises
        // offsets near INT32_MAX.)
        moveImmToScratch(int64_t(address.offset), dataTempRegister);
        m_assembler.add(scratch, scratch, dataTempRegister);
        return { scratch, 0 };
    }

    // --- Condition plumbing --------------------------------------------

    struct BranchBits { uint32_t bo; uint32_t bi; };

    static BranchBits branchBitsFor(RelationalCondition cond)
    {
        // CR0 bit indices: 0=LT, 1=GT, 2=EQ.  bo 12 = branch if bit set,
        // bo 4 = branch if bit clear.  Signed vs unsigned is chosen by the
        // COMPARE (cmpw/cmpd vs cmplw/cmpld); the bits read the same.
        switch (cond) {
        case Equal:                 return { 12, 2 };
        case NotEqual:              return { 4,  2 };
        case Above:                 return { 12, 1 };
        case AboveOrEqual:          return { 4,  0 };
        case Below:                 return { 12, 0 };
        case BelowOrEqual:          return { 4,  1 };
        case GreaterThan:           return { 12, 1 };
        case GreaterThanOrEqual:    return { 4,  0 };
        case LessThan:              return { 12, 0 };
        case LessThanOrEqual:       return { 4,  1 };
        }
        RELEASE_ASSERT_NOT_REACHED();
        return { 20, 0 };
    }

    static bool isUnsignedCondition(RelationalCondition cond)
    {
        return cond == Above || cond == AboveOrEqual || cond == Below || cond == BelowOrEqual;
    }

    Jump makeBranch(RelationalCondition cond)
    {
        return Jump(m_assembler.emitUnlinkedBranch(branchBitsFor(cond).bo, branchBitsFor(cond).bi));
    }

    // Compare reg/reg or reg/imm at the given width, setting CR0.
    void emitCompare32(RelationalCondition cond, RegisterID left, RegisterID right)
    {
        if (isUnsignedCondition(cond))
            m_assembler.cmplw(0, left, right);
        else
            m_assembler.cmpw(0, left, right);
    }

    void emitCompare32(RelationalCondition cond, RegisterID left, TrustedImm32 right)
    {
        // The immediate scratch must not alias `left`: callers such as
        // branch32(Address, TrustedImm32) load the memory operand into
        // dataTempRegister and pass it as `left`, so materializing the
        // immediate there would clobber it before the compare.
        RegisterID scratch = (left == dataTempRegister) ? memoryTempRegister : dataTempRegister;
        if (isUnsignedCondition(cond)) {
            if (isUInt16(uint32_t(right.m_value)))
                m_assembler.cmplwi(0, left, uint16_t(right.m_value));
            else {
                moveImmToScratch(uint32_t(right.m_value), scratch);
                m_assembler.cmplw(0, left, scratch);
            }
        } else {
            if (isInt16(right.m_value))
                m_assembler.cmpwi(0, left, int16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, scratch);
                m_assembler.cmpw(0, left, scratch);
            }
        }
    }

    void emitCompare64(RelationalCondition cond, RegisterID left, RegisterID right)
    {
        if (isUnsignedCondition(cond))
            m_assembler.cmpld(0, left, right);
        else
            m_assembler.cmpd(0, left, right);
    }

    void emitCompare64(RelationalCondition cond, RegisterID left, TrustedImm64 right)
    {
        // See emitCompare32: the immediate scratch must not alias `left`.
        RegisterID scratch = (left == dataTempRegister) ? memoryTempRegister : dataTempRegister;
        if (isUnsignedCondition(cond)) {
            if (isUInt16(right.m_value))
                m_assembler.cmpldi(0, left, uint16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, scratch);
                m_assembler.cmpld(0, left, scratch);
            }
        } else {
            if (isInt16(right.m_value))
                m_assembler.cmpdi(0, left, int16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, scratch);
                m_assembler.cmpd(0, left, scratch);
            }
        }
    }

    // Set dest = 0/1 from CR0 per cond: mfcr + rlwinm bit extraction,
    // inverting via xori when the condition is a bit-clear sense.
    void setFromCondition(RelationalCondition cond, RegisterID dest)
    {
        BranchBits bits = branchBitsFor(cond);
        m_assembler.mfcr(dest);
        // Rotate CR0 bit `bi` into the LSB: CR bit i (MSB numbering) sits
        // at 32-bit position 31-i from LSB; rlwinm with SH = bi + 1.
        m_assembler.rlwinm(dest, dest, bits.bi + 1, 31, 31);
        if (bits.bo == 4)
            m_assembler.xori(dest, dest, 1);
    }

    // --- 32-bit arithmetic ---------------------------------------------

    void add32(RegisterID src, RegisterID dest) { add32(src, dest, dest); }
    void add32(RegisterID a, RegisterID b, RegisterID dest)
    {
        m_assembler.add(dest, a, b);
        zeroExtend32ToWordInternal(dest);
    }
    void add32(TrustedImm32 imm, RegisterID dest) { add32(imm, dest, dest); }
    void add32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        if (isInt16(imm.m_value))
            m_assembler.addi(dest, src, int16_t(imm.m_value));
        else {
            moveImmToScratch(imm.m_value, dataTempRegister);
            m_assembler.add(dest, src, dataTempRegister);
        }
        zeroExtend32ToWordInternal(dest);
    }

    void sub32(RegisterID src, RegisterID dest)
    {
        m_assembler.subf(dest, src, dest);
        zeroExtend32ToWordInternal(dest);
    }
    void sub32(RegisterID left, RegisterID right, RegisterID dest)
    {
        m_assembler.subf(dest, right, left);
        zeroExtend32ToWordInternal(dest);
    }
    void sub32(TrustedImm32 imm, RegisterID dest)
    {
        add32(TrustedImm32(-imm.m_value), dest);
    }

    void mul32(RegisterID src, RegisterID dest) { mul32(src, dest, dest); }
    void mul32(RegisterID a, RegisterID b, RegisterID dest)
    {
        m_assembler.mullw(dest, a, b);
        zeroExtend32ToWordInternal(dest);
    }
    void mul32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        mul32(dataTempRegister, src, dest);
    }

    void neg32(RegisterID dest)
    {
        m_assembler.neg(dest, dest);
        zeroExtend32ToWordInternal(dest);
    }

    // --- 32-bit logical -------------------------------------------------

    void and32(RegisterID src, RegisterID dest) { and32(src, dest, dest); }
    void and32(RegisterID a, RegisterID b, RegisterID dest)
    {
        m_assembler.and_(dest, a, b);
        zeroExtend32ToWordInternal(dest);
    }
    void and32(TrustedImm32 imm, RegisterID dest) { and32(imm, dest, dest); }
    void and32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        if (isUInt16(uint32_t(imm.m_value))) {
            m_assembler.andi_(dest, src, uint16_t(imm.m_value));   // clears upper ✓
            return;
        }
        moveImmToScratch(uint32_t(imm.m_value), dataTempRegister);
        and32(dataTempRegister, src, dest);
    }

    void or32(RegisterID src, RegisterID dest) { or32(src, dest, dest); }
    void or32(RegisterID a, RegisterID b, RegisterID dest)
    {
        m_assembler.or_(dest, a, b);
        zeroExtend32ToWordInternal(dest);
    }
    void or32(TrustedImm32 imm, RegisterID dest) { or32(imm, dest, dest); }
    void or32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        if (isUInt16(uint32_t(imm.m_value)) && src == dest) {
            m_assembler.ori(dest, src, uint16_t(imm.m_value));
            zeroExtend32ToWordInternal(dest);
            return;
        }
        moveImmToScratch(uint32_t(imm.m_value), dataTempRegister);
        or32(dataTempRegister, src, dest);
    }

    void xor32(RegisterID src, RegisterID dest) { xor32(src, dest, dest); }
    void xor32(RegisterID a, RegisterID b, RegisterID dest)
    {
        m_assembler.xor_(dest, a, b);
        zeroExtend32ToWordInternal(dest);
    }
    void xor32(TrustedImm32 imm, RegisterID dest) { xor32(imm, dest, dest); }
    void xor32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        if (imm.m_value == -1) {
            m_assembler.nor(dest, src, src);
            zeroExtend32ToWordInternal(dest);
            return;
        }
        moveImmToScratch(uint32_t(imm.m_value), dataTempRegister);
        xor32(dataTempRegister, src, dest);
    }

    void not32(RegisterID dest)
    {
        m_assembler.nor(dest, dest, dest);
        zeroExtend32ToWordInternal(dest);
    }

    // --- 64-bit logical -------------------------------------------------

    void and64(RegisterID src, RegisterID dest) { m_assembler.and_(dest, src, dest); }
    void and64(RegisterID a, RegisterID b, RegisterID dest) { m_assembler.and_(dest, a, b); }
    void and64(TrustedImm32 imm, RegisterID dest)
    {
        if (isUInt16(uint32_t(imm.m_value)) && imm.m_value >= 0) {
            m_assembler.andi_(dest, dest, uint16_t(imm.m_value));
            return;
        }
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);   // sign-extended
        m_assembler.and_(dest, dataTempRegister, dest);
    }
    void and64(TrustedImm64 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.and_(dest, dataTempRegister, dest);
    }
    void and64(TrustedImmPtr imm, RegisterID dest)
    {
        moveImmToScratch(int64_t(imm.asIntptr()), dataTempRegister);
        m_assembler.and_(dest, dataTempRegister, dest);
    }

    void or64(RegisterID src, RegisterID dest) { m_assembler.or_(dest, src, dest); }
    void or64(RegisterID a, RegisterID b, RegisterID dest) { m_assembler.or_(dest, a, b); }
    void or64(TrustedImm32 imm, RegisterID dest)
    {
        if (imm.m_value >= 0 && isUInt16(imm.m_value)) {
            m_assembler.ori(dest, dest, uint16_t(imm.m_value));
            return;
        }
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        m_assembler.or_(dest, dataTempRegister, dest);
    }
    void or64(TrustedImm64 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.or_(dest, dataTempRegister, dest);
    }

    void xor64(RegisterID src, RegisterID dest) { m_assembler.xor_(dest, src, dest); }
    void xor64(RegisterID a, RegisterID b, RegisterID dest) { m_assembler.xor_(dest, a, b); }
    void xor64(TrustedImm32 imm, RegisterID dest)
    {
        if (imm.m_value == -1) {
            m_assembler.nor(dest, dest, dest);
            return;
        }
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        m_assembler.xor_(dest, dataTempRegister, dest);
    }

    // --- Shifts (count masked like x86/arm64 hardware) -------------------

    void lshift32(RegisterID shiftAmount, RegisterID dest) { lshift32(dest, shiftAmount, dest); }
    void lshift32(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rlwinm(dataTempRegister, shiftAmount, 0, 27, 31);   // count & 31
        m_assembler.slw(dest, src, dataTempRegister);                    // slw zero-extends
    }
    void lshift32(TrustedImm32 imm, RegisterID dest) { lshift32(dest, imm, dest); }
    void lshift32(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        m_assembler.slwi(dest, src, imm.m_value & 31);
    }

    void rshift32(RegisterID shiftAmount, RegisterID dest) { rshift32(dest, shiftAmount, dest); }
    void rshift32(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rlwinm(dataTempRegister, shiftAmount, 0, 27, 31);
        m_assembler.sraw(dest, src, dataTempRegister);
        zeroExtend32ToWordInternal(dest);                                // sraw sign-extends
    }
    void rshift32(TrustedImm32 imm, RegisterID dest) { rshift32(dest, imm, dest); }
    void rshift32(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        m_assembler.srawi(dest, src, imm.m_value & 31);
        zeroExtend32ToWordInternal(dest);
    }

    void urshift32(RegisterID shiftAmount, RegisterID dest) { urshift32(dest, shiftAmount, dest); }
    void urshift32(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rlwinm(dataTempRegister, shiftAmount, 0, 27, 31);
        m_assembler.srw(dest, src, dataTempRegister);                    // srw zero-extends
    }
    void urshift32(TrustedImm32 imm, RegisterID dest) { urshift32(dest, imm, dest); }
    void urshift32(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        if (!(imm.m_value & 31)) {
            move(src, dest);
            zeroExtend32ToWordInternal(dest);
            return;
        }
        m_assembler.srwi(dest, src, imm.m_value & 31);
    }

    // --- Compare-to-register ---------------------------------------------

    void compare32(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID dest)
    {
        emitCompare32(cond, left, right);
        setFromCondition(cond, dest);
    }
    void compare32(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID dest)
    {
        emitCompare32(cond, left, right);
        setFromCondition(cond, dest);
    }
    void compare64(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID dest)
    {
        emitCompare64(cond, left, right);
        setFromCondition(cond, dest);
    }
    void compare64(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID dest)
    {
        emitCompare64(cond, left, TrustedImm64(right.m_value));
        setFromCondition(cond, dest);
    }

    // --- Branches ---------------------------------------------------------

    Jump branch32(RelationalCondition cond, RegisterID left, RegisterID right)
    {
        emitCompare32(cond, left, right);
        return makeBranch(cond);
    }
    Jump branch32(RelationalCondition cond, RegisterID left, TrustedImm32 right)
    {
        emitCompare32(cond, left, right);
        return makeBranch(cond);
    }

    Jump branch64(RelationalCondition cond, RegisterID left, RegisterID right)
    {
        emitCompare64(cond, left, right);
        return makeBranch(cond);
    }
    Jump branch64(RelationalCondition cond, RegisterID left, TrustedImm32 right)
    {
        emitCompare64(cond, left, TrustedImm64(right.m_value));
        return makeBranch(cond);
    }
    Jump branch64(RelationalCondition cond, RegisterID left, TrustedImm64 right)
    {
        emitCompare64(cond, left, right);
        return makeBranch(cond);
    }

    Jump branchPtr(RelationalCondition cond, RegisterID left, RegisterID right)
    {
        return branch64(cond, left, right);
    }

    // --- Memory-operand branches: load the memory side, then reg/reg or
    // reg/imm compare. Narrow widths (8/16) load zero-extended and the
    // immediate is masked to the width. ---
    Jump branch32(RelationalCondition cond, Address left, RegisterID right)
    {
        load32(left, dataTempRegister);
        return branch32(cond, dataTempRegister, right);
    }
    Jump branch32(RelationalCondition cond, BaseIndex left, RegisterID right)
    {
        load32(left, dataTempRegister);
        return branch32(cond, dataTempRegister, right);
    }
    Jump branch32(RelationalCondition cond, RegisterID left, Address right)
    {
        load32(right, dataTempRegister);
        return branch32(cond, left, dataTempRegister);
    }
    Jump branch32(RelationalCondition cond, AbsoluteAddress left, RegisterID right)
    {
        load32(left.m_ptr, dataTempRegister);
        return branch32(cond, dataTempRegister, right);
    }
    Jump branch32(RelationalCondition cond, Address left, TrustedImm32 right)
    {
        load32(left, dataTempRegister);
        return branch32(cond, dataTempRegister, right);
    }

    Jump branch64(RelationalCondition cond, Address left, RegisterID right)
    {
        load64(left, dataTempRegister);
        return branch64(cond, dataTempRegister, right);
    }
    Jump branch64(RelationalCondition cond, BaseIndex left, RegisterID right)
    {
        load64(left, dataTempRegister);
        return branch64(cond, dataTempRegister, right);
    }
    Jump branch64(RelationalCondition cond, RegisterID left, Address right)
    {
        load64(right, dataTempRegister);
        return branch64(cond, left, dataTempRegister);
    }
    Jump branch64(RelationalCondition cond, AbsoluteAddress left, RegisterID right)
    {
        load64(left.m_ptr, dataTempRegister);
        return branch64(cond, dataTempRegister, right);
    }
    Jump branch64(RelationalCondition cond, Address left, TrustedImm64 right)
    {
        load64(left, dataTempRegister);
        return branch64(cond, dataTempRegister, right);
    }

    Jump branchPtr(RelationalCondition cond, Address left, RegisterID right) { return branch64(cond, left, right); }
    Jump branchPtr(RelationalCondition cond, BaseIndex left, RegisterID right) { return branch64(cond, left, right); }
    Jump branchPtr(RelationalCondition cond, RegisterID left, Address right) { return branch64(cond, left, right); }
    Jump branchPtr(RelationalCondition cond, Address left, Address right)
    {
        load64(left, dataTempRegister);
        load64(right, memoryTempRegister);
        return branch64(cond, dataTempRegister, memoryTempRegister);
    }

    // Byte / halfword comparisons: load zero-extended, mask the immediate.
    Jump branch8(RelationalCondition cond, Address left, TrustedImm32 right)
    {
        load8(left, dataTempRegister);
        return branch32(cond, dataTempRegister, TrustedImm32(right.m_value & 0xFF));
    }
    Jump branch8(RelationalCondition cond, BaseIndex left, TrustedImm32 right)
    {
        load8(left, dataTempRegister);
        return branch32(cond, dataTempRegister, TrustedImm32(right.m_value & 0xFF));
    }
    Jump branch8(RelationalCondition cond, AbsoluteAddress left, TrustedImm32 right)
    {
        load8(left.m_ptr, dataTempRegister);
        return branch32(cond, dataTempRegister, TrustedImm32(right.m_value & 0xFF));
    }
    Jump branch16(RelationalCondition cond, Address left, TrustedImm32 right)
    {
        load16(left, dataTempRegister);
        return branch32(cond, dataTempRegister, TrustedImm32(right.m_value & 0xFFFF));
    }

    // ResultCondition-based test branch: and the operands, test CR0.
    Jump branchTestImpl32(ResultCondition cond, RegisterID valueLow32)
    {
        // valueLow32 has the 32-bit test value zero-extended.
        switch (cond) {
        case Zero:
            m_assembler.cmpdi(0, valueLow32, 0);
            return Jump(m_assembler.emitUnlinkedBranch(12, 2));
        case NonZero:
            m_assembler.cmpdi(0, valueLow32, 0);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        case Signed:
            m_assembler.cmpwi(0, valueLow32, 0);
            return Jump(m_assembler.emitUnlinkedBranch(12, 0));
        case PositiveOrZero:
            m_assembler.cmpwi(0, valueLow32, 0);
            return Jump(m_assembler.emitUnlinkedBranch(4, 0));
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return Jump();
        }
    }

    Jump branchTest32(ResultCondition cond, RegisterID reg, RegisterID mask)
    {
        m_assembler.and_(dataTempRegister, reg, mask);
        zeroExtend32ToWordInternal(dataTempRegister);
        return branchTestImpl32(cond, dataTempRegister);
    }
    Jump branchTest32(ResultCondition cond, RegisterID reg, TrustedImm32 mask = TrustedImm32(-1))
    {
        if (mask.m_value == -1) {
            m_assembler.rldicl(dataTempRegister, reg, 0, 32);
            return branchTestImpl32(cond, dataTempRegister);
        }
        if (isUInt16(uint32_t(mask.m_value))) {
            m_assembler.andi_(dataTempRegister, reg, uint16_t(mask.m_value));
            return branchTestImpl32(cond, dataTempRegister);
        }
        moveImmToScratch(uint32_t(mask.m_value), dataTempRegister);
        return branchTest32(cond, reg, dataTempRegister);
    }

    Jump branchTest64(ResultCondition cond, RegisterID reg, RegisterID mask)
    {
        m_assembler.and_(dataTempRegister, reg, mask);
        return branchTest64Impl(cond, dataTempRegister);
    }
    Jump branchTest64(ResultCondition cond, RegisterID reg, TrustedImm32 mask = TrustedImm32(-1))
    {
        if (mask.m_value == -1)
            return branchTest64Impl(cond, reg);
        if (mask.m_value >= 0 && isUInt16(mask.m_value)) {
            m_assembler.andi_(dataTempRegister, reg, uint16_t(mask.m_value));
            return branchTest64Impl(cond, dataTempRegister);
        }
        moveImmToScratch(int64_t(mask.m_value), dataTempRegister);
        return branchTest64(cond, reg, dataTempRegister);
    }
    Jump branchTest64(ResultCondition cond, RegisterID reg, TrustedImm64 mask)
    {
        if (mask.m_value == -1)
            return branchTest64Impl(cond, reg);
        moveImmToScratch(mask.m_value, dataTempRegister);
        m_assembler.and_(dataTempRegister, reg, dataTempRegister);
        return branchTest64Impl(cond, dataTempRegister);
    }
    Jump branchTest64(ResultCondition cond, Address address, TrustedImm32 mask = TrustedImm32(-1))
    {
        load64(address, memoryTempRegister);
        return branchTest64(cond, memoryTempRegister, mask);
    }
    Jump branchTest64(ResultCondition cond, BaseIndex address, TrustedImm32 mask = TrustedImm32(-1))
    {
        load64(address, memoryTempRegister);
        return branchTest64(cond, memoryTempRegister, mask);
    }
    Jump branchTest64(ResultCondition cond, Address address, RegisterID mask)
    {
        load64(address, memoryTempRegister);
        return branchTest64(cond, memoryTempRegister, mask);
    }
    Jump branchTest32(ResultCondition cond, Address address, TrustedImm32 mask = TrustedImm32(-1))
    {
        load32(address, memoryTempRegister);
        return branchTest32(cond, memoryTempRegister, mask);
    }

        Jump branchTest64Impl(ResultCondition cond, RegisterID value)
    {
        switch (cond) {
        case Zero:
            m_assembler.cmpdi(0, value, 0);
            return Jump(m_assembler.emitUnlinkedBranch(12, 2));
        case NonZero:
            m_assembler.cmpdi(0, value, 0);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        case Signed:
            m_assembler.cmpdi(0, value, 0);
            return Jump(m_assembler.emitUnlinkedBranch(12, 0));
        case PositiveOrZero:
            m_assembler.cmpdi(0, value, 0);
            return Jump(m_assembler.emitUnlinkedBranch(4, 0));
        default:
            RELEASE_ASSERT_NOT_REACHED();
            return Jump();
        }
    }

    // branchAdd32 / branchSub32 / branchMul32 with Overflow use the
    // exact-64-bit technique validated in the Phase 2 offlineasm work:
    // do the arithmetic on sign-extended copies; int32 overflow iff the
    // 64-bit result differs from its own low-word sign extension.
    Jump branchAdd32(ResultCondition cond, RegisterID src, RegisterID dest) { return branchAdd32(cond, src, dest, dest); }
    Jump branchAdd32(ResultCondition cond, RegisterID a, RegisterID b, RegisterID dest)
    {
        if (cond == Overflow) {
            m_assembler.extsw(dataTempRegister, a);
            m_assembler.extsw(memoryTempRegister, b);
            m_assembler.add(dataTempRegister, dataTempRegister, memoryTempRegister);
            m_assembler.rldicl(dest, dataTempRegister, 0, 32);           // wrapped result, zero-extended
            m_assembler.extsw(memoryTempRegister, dataTempRegister);
            m_assembler.cmpd(0, memoryTempRegister, dataTempRegister);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));           // != → overflow
        }
        add32(a, b, dest);
        return branchTestImpl32(resultConditionForArith(cond), dest);
    }
    Jump branchAdd32(ResultCondition cond, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchAdd32(cond, memoryTempRegister, dest, dest);
    }
    Jump branchAdd32(ResultCondition cond, TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchAdd32(cond, memoryTempRegister, src, dest);
    }

    Jump branchSub32(ResultCondition cond, RegisterID src, RegisterID dest) { return branchSub32(cond, dest, src, dest); }
    Jump branchSub32(ResultCondition cond, RegisterID left, RegisterID right, RegisterID dest)
    {
        if (cond == Overflow) {
            m_assembler.extsw(dataTempRegister, left);
            m_assembler.extsw(memoryTempRegister, right);
            m_assembler.subf(dataTempRegister, memoryTempRegister, dataTempRegister);
            m_assembler.rldicl(dest, dataTempRegister, 0, 32);
            m_assembler.extsw(memoryTempRegister, dataTempRegister);
            m_assembler.cmpd(0, memoryTempRegister, dataTempRegister);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        }
        sub32(left, right, dest);
        return branchTestImpl32(resultConditionForArith(cond), dest);
    }
    Jump branchSub32(ResultCondition cond, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchSub32(cond, dest, memoryTempRegister, dest);
    }

    Jump branchMul32(ResultCondition cond, RegisterID src, RegisterID dest) { return branchMul32(cond, src, dest, dest); }
    Jump branchMul32(ResultCondition cond, RegisterID a, RegisterID b, RegisterID dest)
    {
        if (cond == Overflow) {
            m_assembler.extsw(dataTempRegister, a);
            m_assembler.extsw(memoryTempRegister, b);
            m_assembler.mulld(dataTempRegister, dataTempRegister, memoryTempRegister);
            m_assembler.rldicl(dest, dataTempRegister, 0, 32);
            m_assembler.extsw(memoryTempRegister, dataTempRegister);
            m_assembler.cmpd(0, memoryTempRegister, dataTempRegister);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        }
        mul32(a, b, dest);
        return branchTestImpl32(resultConditionForArith(cond), dest);
    }

    static ResultCondition resultConditionForArith(ResultCondition cond)
    {
        RELEASE_ASSERT(cond == Zero || cond == NonZero || cond == Signed || cond == PositiveOrZero);
        return cond;
    }

    Jump jump()
    {
        return Jump(m_assembler.emitUnlinkedJump());
    }

    // branchTest{8,32,64} against an absolute memory location: load, mask, test.
    Jump branchTest64(ResultCondition cond, AbsoluteAddress address, TrustedImm32 mask = TrustedImm32(-1))
    {
        // Load the value into memoryTempRegister (reusing the address slot)
        // so a large-mask materialization into dataTempRegister can't clobber
        // it.
        moveToAbsolute(address.m_ptr);
        m_assembler.ld(memoryTempRegister, 0, memoryTempRegister);
        return branchTest64(cond, memoryTempRegister, mask);
    }
    Jump branchTest32(ResultCondition cond, AbsoluteAddress address, TrustedImm32 mask = TrustedImm32(-1))
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lwz(memoryTempRegister, 0, memoryTempRegister);
        return branchTest32(cond, memoryTempRegister, mask);
    }
    Jump branchTest8(ResultCondition cond, AbsoluteAddress address, TrustedImm32 mask = TrustedImm32(-1))
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lbz(dataTempRegister, 0, memoryTempRegister);
        return finishNarrowTest(cond, mask, 0xFF, /*halfword*/ false);
    }

    // branchTest8/16: load the value, apply the mask, sign-extend, then
    // test as 64-bit.  Sign extension makes Signed/PositiveOrZero read the
    // narrow value's sign bit and is neutral for Zero/NonZero.
    Jump branchTest8(ResultCondition cond, Address address, TrustedImm32 mask = TrustedImm32(-1))
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dataTempRegister, r.offset, r.base);
        return finishNarrowTest(cond, mask, 0xFF, /*halfword*/ false);
    }
    Jump branchTest8(ResultCondition cond, BaseIndex address, TrustedImm32 mask = TrustedImm32(-1))
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dataTempRegister, r.offset, r.base);
        return finishNarrowTest(cond, mask, 0xFF, false);
    }
    Jump branchTest16(ResultCondition cond, Address address, TrustedImm32 mask = TrustedImm32(-1))
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lhz(dataTempRegister, r.offset, r.base);
        return finishNarrowTest(cond, mask, 0xFFFF, true);
    }
    Jump branchTest16(ResultCondition cond, BaseIndex address, TrustedImm32 mask = TrustedImm32(-1))
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lhz(dataTempRegister, r.offset, r.base);
        return finishNarrowTest(cond, mask, 0xFFFF, true);
    }
    Jump finishNarrowTest(ResultCondition cond, TrustedImm32 mask, uint32_t widthMask, bool halfword)
    {
        if (mask.m_value != -1)
            m_assembler.andi_(dataTempRegister, dataTempRegister, uint16_t(uint32_t(mask.m_value) & widthMask));
        if (halfword)
            m_assembler.extsh(dataTempRegister, dataTempRegister);
        else
            m_assembler.extsb(dataTempRegister, dataTempRegister);
        return branchTest64Impl(cond, dataTempRegister);
    }

    // --- Loads and stores -------------------------------------------------
    // load32/16/8 zero-extend (lwz/lhz/lbz); ld/std are DS-form and need
    // 4-aligned displacements — resolveAddressDS falls back to adding the
    // displacement into the scratch when it is misaligned.

    template<typename AddressType>
    ResolvedAddress resolveAddressDS(AddressType address, RegisterID scratch)
    {
        ResolvedAddress r = resolveAddress(address, scratch);
        if (!(r.offset & 3))
            return r;
        m_assembler.addi(scratch, r.base, r.offset);
        return { scratch, 0 };
    }

    void moveToAbsolute(const void* address)
    {
        moveImmToScratch(int64_t(intptr_t(address)), memoryTempRegister);
    }

    void load64(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.ld(dest, r.offset, r.base);
    }
    void load64(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.ld(dest, 0, memoryTempRegister);
    }

    void load32(Address address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dest, r.offset, r.base);
    }
    void load32(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dest, r.offset, r.base);
    }
    void load32(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lwz(dest, 0, memoryTempRegister);
    }

    void load16(Address address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lhz(dest, r.offset, r.base);
    }
    void load16(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lhz(dest, r.offset, r.base);
    }
    void load16(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lhz(dest, 0, memoryTempRegister);
    }

    void load8(Address address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dest, r.offset, r.base);
    }
    void load8(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dest, r.offset, r.base);
    }
    void load8(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lbz(dest, 0, memoryTempRegister);
    }

    void store64(RegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.std(src, r.offset, r.base);
    }
    void store64(RegisterID src, void* address)
    {
        moveToAbsolute(address);
        m_assembler.std(src, 0, memoryTempRegister);
    }
    void store64(TrustedImm64 imm, Address address)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        store64(dataTempRegister, address);
    }
    void store64(TrustedImm32 imm, Address address)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        store64(dataTempRegister, address);
    }
    void store64(TrustedImmPtr imm, Address address)
    {
        moveImmToScratch(int64_t(imm.asIntptr()), dataTempRegister);
        store64(dataTempRegister, address);
    }
    void store64(TrustedImm64 imm, BaseIndex address)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        store64(dataTempRegister, address);
    }
    void store64(TrustedImm32 imm, BaseIndex address)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        store64(dataTempRegister, address);
    }

    void store32(RegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stw(src, r.offset, r.base);
    }
    void store32(RegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stw(src, r.offset, r.base);
    }
    void store32(RegisterID src, void* address)
    {
        moveToAbsolute(address);
        m_assembler.stw(src, 0, memoryTempRegister);
    }
    void store32(TrustedImm32 imm, Address address)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        store32(dataTempRegister, address);
    }
    void store32(TrustedImm32 imm, void* address)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        store32(dataTempRegister, address);
    }

    void store16(RegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.sth(src, r.offset, r.base);
    }
    void store16(RegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.sth(src, r.offset, r.base);
    }

    void store8(RegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stb(src, r.offset, r.base);
    }
    void store8(RegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stb(src, r.offset, r.base);
    }
    void store8(TrustedImm32 imm, Address address)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        store8(dataTempRegister, address);
    }

    void getEffectiveAddress(BaseIndex address, RegisterID dest)
    {
        if (address.scale) {
            // Shift into the scratch: sldi straight into dest would clobber
            // the base when dest == address.base.
            m_assembler.sldi(dataTempRegister, address.index, address.scale);
            m_assembler.add(dest, dataTempRegister, address.base);
        } else
            m_assembler.add(dest, address.index, address.base);
        if (address.offset) {
            if (isInt16(address.offset))
                m_assembler.addi(dest, dest, int16_t(address.offset));
            else {
                moveImmToScratch(int64_t(address.offset), dataTempRegister);
                m_assembler.add(dest, dest, dataTempRegister);
            }
        }
    }

    void move(TrustedImm64 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, dest);
    }

    // Memory-to-memory transfers: load via dataTempRegister, store back.
    // (dataTempRegister holds the value; memoryTempRegister resolves each
    // address in turn — the load's address is fully consumed before the
    // store resolves its own.)
    void transfer32(Address src, Address dest)
    {
        load32(src, dataTempRegister);
        store32(dataTempRegister, dest);
    }
    void transfer32(BaseIndex src, BaseIndex dest)
    {
        load32(src, dataTempRegister);
        store32(dataTempRegister, dest);
    }
    void transfer64(Address src, Address dest)
    {
        load64(src, dataTempRegister);
        store64(dataTempRegister, dest);
    }
    void transfer64(BaseIndex src, BaseIndex dest)
    {
        load64(src, dataTempRegister);
        store64(dataTempRegister, dest);
    }
    void transferPtr(Address src, Address dest) { transfer64(src, dest); }
    void transferPtr(BaseIndex src, BaseIndex dest) { transfer64(src, dest); }

    enum BranchTruncateType { BranchIfTruncateFailed, BranchIfTruncateSuccessful };

    // ------------------------------------------------------------------
    // Floating point.  FPRs hold doubles; float32 values live in the FPR
    // in double format (lfs/stfs and the -s arithmetic forms convert),
    // matching the classic PPC float model.  f0 is the FP scratch (SM
    // convention: ScratchDoubleReg = f0).
    // ------------------------------------------------------------------

    void loadDouble(Address address, FPRegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lfd(dest, r.offset, r.base);
    }
    void loadDouble(BaseIndex address, FPRegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lfd(dest, r.offset, r.base);
    }
    void loadDouble(TrustedImmPtr address, FPRegisterID dest)
    {
        move(address, memoryTempRegister);
        m_assembler.lfd(dest, 0, memoryTempRegister);
    }
    void loadFloat(Address address, FPRegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lfs(dest, r.offset, r.base);
    }
    void loadFloat(BaseIndex address, FPRegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lfs(dest, r.offset, r.base);
    }
    void loadFloat(TrustedImmPtr address, FPRegisterID dest)
    {
        move(address, memoryTempRegister);
        m_assembler.lfs(dest, 0, memoryTempRegister);
    }
    void storeDouble(FPRegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stfd(src, r.offset, r.base);
    }
    void storeDouble(FPRegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stfd(src, r.offset, r.base);
    }
    void storeDouble(FPRegisterID src, TrustedImmPtr address)
    {
        move(address, memoryTempRegister);
        m_assembler.stfd(src, 0, memoryTempRegister);
    }
    void storeFloat(FPRegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stfs(src, r.offset, r.base);
    }
    void storeFloat(FPRegisterID src, BaseIndex address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.stfs(src, r.offset, r.base);
    }

    void moveDouble(FPRegisterID src, FPRegisterID dest)
    {
        if (src != dest)
            m_assembler.fmr(dest, src);
    }
    void moveZeroToDouble(FPRegisterID dest)
    {
        m_assembler.addi(dataTempRegister, PPC64Registers::r0, 0);
        m_assembler.mtvsrd(dest, dataTempRegister);
    }
    void move64ToDouble(RegisterID src, FPRegisterID dest)
    {
        m_assembler.mtvsrd(dest, src);
    }
    void move64ToDouble(TrustedImm64 imm, FPRegisterID dest)
    {
        moveImmToScratch(imm.m_value, dataTempRegister);
        m_assembler.mtvsrd(dest, dataTempRegister);
    }
    void moveDoubleTo64(FPRegisterID src, RegisterID dest)
    {
        m_assembler.mfvsrd(dest, src);
    }
    // Bitcast int32 <-> float32 must round-trip through memory: FPRs hold
    // float values in double format, so raw float32 bits only become a
    // usable float via lfs's format conversion.  Uses the ELFv2 red zone.
    void move32ToFloat(RegisterID src, FPRegisterID dest)
    {
        m_assembler.stw(src, -8, stackPointerRegister);
        m_assembler.lfs(dest, -8, stackPointerRegister);
    }
    void move32ToFloat(TrustedImm32 imm, FPRegisterID dest)
    {
        moveImmToScratch(int64_t(uint32_t(imm.m_value)), dataTempRegister);
        move32ToFloat(dataTempRegister, dest);
    }
    void moveFloatTo32(FPRegisterID src, RegisterID dest)
    {
        m_assembler.stfs(src, -8, stackPointerRegister);
        m_assembler.lwz(dest, -8, stackPointerRegister);
    }

    void convertInt32ToDouble(RegisterID src, FPRegisterID dest)
    {
        m_assembler.mtvsrwa(dest, src);
        m_assembler.fcfid(dest, dest);
    }
    void convertInt32ToDouble(TrustedImm32 imm, FPRegisterID dest)
    {
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        convertInt32ToDouble(dataTempRegister, dest);
    }
    void convertInt32ToDouble(BaseIndex address, FPRegisterID dest)
    {
        load32(address, dataTempRegister);
        convertInt32ToDouble(dataTempRegister, dest);
    }
    void convertInt64ToDouble(RegisterID src, FPRegisterID dest)
    {
        m_assembler.mtvsrd(dest, src);
        m_assembler.fcfid(dest, dest);
    }
    void convertUInt32ToDouble(RegisterID src, FPRegisterID dest)
    {
        m_assembler.mtvsrwz(dest, src);
        m_assembler.fcfid(dest, dest);
    }
    void convertUInt32ToDouble(RegisterID src, FPRegisterID dest, FPRegisterID)
    {
        convertUInt32ToDouble(src, dest);
    }
    void convertUInt64ToDouble(RegisterID src, FPRegisterID dest)
    {
        m_assembler.mtvsrd(dest, src);
        m_assembler.fcfidu(dest, dest);
    }
    void convertFloatToDouble(FPRegisterID src, FPRegisterID dest)
    {
        moveDouble(src, dest);      // already double format in the FPR
    }
    void convertDoubleToFloat(FPRegisterID src, FPRegisterID dest)
    {
        m_assembler.frsp(dest, src);
    }

    void addDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest) { m_assembler.fadd(dest, a, b); }
    void addDouble(FPRegisterID src, FPRegisterID dest)               { m_assembler.fadd(dest, dest, src); }
    void subDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest) { m_assembler.fsub(dest, a, b); }
    void subDouble(FPRegisterID src, FPRegisterID dest)               { m_assembler.fsub(dest, dest, src); }
    void mulDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest) { m_assembler.fmul(dest, a, b); }
    void mulDouble(FPRegisterID src, FPRegisterID dest)               { m_assembler.fmul(dest, dest, src); }
    void divDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest) { m_assembler.fdiv(dest, a, b); }
    void divDouble(FPRegisterID src, FPRegisterID dest)               { m_assembler.fdiv(dest, dest, src); }
    void sqrtDouble(FPRegisterID src, FPRegisterID dest)              { m_assembler.fsqrt(dest, src); }

    // FP arith with a memory source: load into the FP scratch, then combine.
    void addDouble(Address src, FPRegisterID dest) { loadDouble(src, fpTempRegister); m_assembler.fadd(dest, dest, fpTempRegister); }
    void subDouble(Address src, FPRegisterID dest) { loadDouble(src, fpTempRegister); m_assembler.fsub(dest, dest, fpTempRegister); }
    void mulDouble(Address src, FPRegisterID dest) { loadDouble(src, fpTempRegister); m_assembler.fmul(dest, dest, fpTempRegister); }
    void mulDouble(BaseIndex src, FPRegisterID dest) { loadDouble(src, fpTempRegister); m_assembler.fmul(dest, dest, fpTempRegister); }
    void divDouble(Address src, FPRegisterID dest) { loadDouble(src, fpTempRegister); m_assembler.fdiv(dest, dest, fpTempRegister); }

    // 16-bit memory compare against a register (signed halfword load).
    Jump branch32WithMemory16(RelationalCondition cond, Address left, RegisterID right)
    {
        load16SignedExtendTo32(left, dataTempRegister);
        return branch32(cond, dataTempRegister, right);
    }

    // dest = base + (shiftee >> shift)  (logical right shift)
    void addUnsignedRightShift32(RegisterID base, RegisterID shiftee, TrustedImm32 shift, RegisterID dest)
    {
        m_assembler.srwi(dataTempRegister, shiftee, shift.m_value & 31);
        m_assembler.add(dest, base, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }
    void absDouble(FPRegisterID src, FPRegisterID dest)               { m_assembler.fabs(dest, src); }

    // FP bitwise ops treat the doubles as bit patterns (used for sign-bit
    // masking, NaN canonicalization, SIMD-lane logic). FPR f_n aliases
    // VSR n, so the VSX logical ops apply directly (Power ISA v2.07B §7.6).
    void andDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest)
    {
        m_assembler.xxland(dest, a, b);
    }
    void andDouble(FPRegisterID src, FPRegisterID dest)
    {
        m_assembler.xxland(dest, dest, src);
    }
    void orDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest)
    {
        m_assembler.xxlor(dest, a, b);
    }
    void orDouble(FPRegisterID src, FPRegisterID dest)
    {
        m_assembler.xxlor(dest, dest, src);
    }
    void xorDouble(FPRegisterID a, FPRegisterID b, FPRegisterID dest)
    {
        m_assembler.xxlxor(dest, a, b);
    }
    void xorDouble(FPRegisterID src, FPRegisterID dest)
    {
        m_assembler.xxlxor(dest, dest, src);
    }
    void negateDouble(FPRegisterID src, FPRegisterID dest)            { m_assembler.fneg(dest, src); }
    void negateFloat(FPRegisterID src, FPRegisterID dest)             { m_assembler.fneg(dest, src); }
    void floorDouble(FPRegisterID src, FPRegisterID dest)             { m_assembler.frim(dest, src); }
    void ceilDouble(FPRegisterID src, FPRegisterID dest)              { m_assembler.frip(dest, src); }
    void truncDouble(FPRegisterID src, FPRegisterID dest)             { m_assembler.friz(dest, src); }
    void roundTowardZeroDouble(FPRegisterID src, FPRegisterID dest)   { m_assembler.friz(dest, src); }
    // NOTE: frin rounds ties AWAY from zero; JS ties-to-even call sites
    // guard this at a higher level.  Revisit if testmasm flags it.
    void roundTowardNearestIntDouble(FPRegisterID src, FPRegisterID dest) { m_assembler.frin(dest, src); }

    void truncateDoubleToInt32(FPRegisterID src, RegisterID dest)
    {
        m_assembler.fctiwz(fpTempRegister, src);
        m_assembler.mfvsrwz(dest, fpTempRegister);
    }
    void truncateDoubleToInt64(FPRegisterID src, RegisterID dest)
    {
        m_assembler.fctidz(fpTempRegister, src);
        m_assembler.mfvsrd(dest, fpTempRegister);
    }

    // --- Double compares/branches ---------------------------------------
    // fcmpu sets CR0 bits LT=0, GT=1, EQ=2, UN=3.  Compound conditions
    // fold UN into the tested bit with one CR-logical op (cror/crandc), so
    // every DoubleCondition is a single conditional branch.
    struct DoubleBranchBits { int cropKind; uint32_t bo; uint32_t bi; };  // cropKind: 0 none, 1 cror bi,bi,UN, 2 crandc bi,bi,UN

    static DoubleBranchBits doubleBranchBitsFor(DoubleCondition cond)
    {
        switch (cond) {
        case DoubleEqualAndOrdered:               return { 0, 12, 2 };
        case DoubleNotEqualAndOrdered:            return { 1, 4,  2 };  // !(EQ|UN)
        case DoubleGreaterThanAndOrdered:         return { 0, 12, 1 };
        case DoubleGreaterThanOrEqualAndOrdered:  return { 1, 4,  0 };  // !(LT|UN)
        case DoubleLessThanAndOrdered:            return { 0, 12, 0 };
        case DoubleLessThanOrEqualAndOrdered:     return { 1, 4,  1 };  // !(GT|UN)
        case DoubleEqualOrUnordered:              return { 1, 12, 2 };  // EQ|UN
        case DoubleNotEqualOrUnordered:           return { 2, 4,  2 };  // !(EQ&!UN)
        case DoubleGreaterThanOrUnordered:        return { 1, 12, 1 };
        case DoubleGreaterThanOrEqualOrUnordered: return { 2, 4,  0 };
        case DoubleLessThanOrUnordered:           return { 1, 12, 0 };
        case DoubleLessThanOrEqualOrUnordered:    return { 2, 4,  1 };
        }
        RELEASE_ASSERT_NOT_REACHED();
        return { 0, 20, 0 };
    }

    void emitDoubleConditionCRop(DoubleBranchBits bits)
    {
        if (bits.cropKind == 1)
            m_assembler.cror(bits.bi, bits.bi, 3);
        else if (bits.cropKind == 2)
            m_assembler.crandc(bits.bi, bits.bi, 3);
    }

    Jump branchDouble(DoubleCondition cond, FPRegisterID left, FPRegisterID right)
    {
        DoubleBranchBits bits = doubleBranchBitsFor(cond);
        m_assembler.fcmpu(0, left, right);
        emitDoubleConditionCRop(bits);
        return Jump(m_assembler.emitUnlinkedBranch(bits.bo, bits.bi));
    }
    Jump branchFloat(DoubleCondition cond, FPRegisterID left, FPRegisterID right)
    {
        return branchDouble(cond, left, right);   // both are doubles in FPRs
    }
    Jump branchDoubleNonZero(FPRegisterID reg, FPRegisterID scratch)
    {
        moveZeroToDouble(scratch);
        return branchDouble(DoubleNotEqualAndOrdered, reg, scratch);
    }
    Jump branchDoubleZeroOrNaN(FPRegisterID reg, FPRegisterID scratch)
    {
        moveZeroToDouble(scratch);
        return branchDouble(DoubleEqualOrUnordered, reg, scratch);
    }

    Jump branchTruncateDoubleToInt32(FPRegisterID src, RegisterID dest, BranchTruncateType branchType = BranchIfTruncateFailed)
    {
        // fctiwz saturates out-of-range/NaN inputs to INT32_MIN/INT32_MAX;
        // treat either saturation value as failure (conservative, matches
        // other RISC ports): failed iff uint32(dest + 0x80000001) <= 1.
        truncateDoubleToInt32(src, dest);
        add32(TrustedImm32(int32_t(0x80000001)), dest, dataTempRegister);
        return branch32(branchType == BranchIfTruncateFailed ? BelowOrEqual : Above, dataTempRegister, TrustedImm32(1));
    }

    // --- Conditional moves: short forward bc (skip 1 insn) over an mr ----

    void emitSkipOneInstruction(uint32_t bo, uint32_t bi)
    {
        m_assembler.bc(bo, bi, 8);
    }

    template<typename CompareEmitter>
    void moveConditionallyImpl(BranchBits bits, RegisterID thenCase, RegisterID elseCase, RegisterID dest, const CompareEmitter& emitCompareFn)
    {
        emitCompareFn();
        if (dest == thenCase) {
            emitSkipOneInstruction(bits.bo, bits.bi);          // taken: keep then-value
            m_assembler.mr(dest, elseCase);
        } else if (dest == elseCase) {
            emitSkipOneInstruction(bits.bo ^ 0x8, bits.bi);    // not taken: keep else-value
            m_assembler.mr(dest, thenCase);
        } else {
            m_assembler.mr(dest, elseCase);
            emitSkipOneInstruction(bits.bo ^ 0x8, bits.bi);
            m_assembler.mr(dest, thenCase);
        }
    }

    void moveConditionally32(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID src, RegisterID dest)
    {
        emitCompare32(cond, left, right);
        emitSkipOneInstruction(branchBitsFor(cond).bo ^ 0x8, branchBitsFor(cond).bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionally32(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        moveConditionallyImpl(branchBitsFor(cond), thenCase, elseCase, dest, [&] { emitCompare32(cond, left, right); });
    }
    void moveConditionally32(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        moveConditionallyImpl(branchBitsFor(cond), thenCase, elseCase, dest, [&] { emitCompare32(cond, left, right); });
    }
    void moveConditionally64(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID src, RegisterID dest)
    {
        emitCompare64(cond, left, right);
        emitSkipOneInstruction(branchBitsFor(cond).bo ^ 0x8, branchBitsFor(cond).bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionally64(RelationalCondition cond, RegisterID left, RegisterID right, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        moveConditionallyImpl(branchBitsFor(cond), thenCase, elseCase, dest, [&] { emitCompare64(cond, left, right); });
    }
    void moveConditionallyDouble(DoubleCondition cond, FPRegisterID left, FPRegisterID right, RegisterID src, RegisterID dest)
    {
        DoubleBranchBits bits = doubleBranchBitsFor(cond);
        m_assembler.fcmpu(0, left, right);
        emitDoubleConditionCRop(bits);
        emitSkipOneInstruction(bits.bo ^ 0x8, bits.bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionallyDouble(DoubleCondition cond, FPRegisterID left, FPRegisterID right, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        DoubleBranchBits dbits = doubleBranchBitsFor(cond);
        moveConditionallyImpl({ dbits.bo, dbits.bi }, thenCase, elseCase, dest, [&] {
            m_assembler.fcmpu(0, left, right);
            emitDoubleConditionCRop(dbits);
        });
    }

    // --- 64-bit negate ----------------------------------------------------

    // --- Signed-extending loads (movs[bwl]-style: sign to 32 zero-extends
    // the upper word; sign to 64 keeps the full extension) ----------------

    template<typename AddressType>
    void load16SignedExtendTo32(AddressType address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lha(dest, r.offset, r.base);
        zeroExtend32ToWordInternal(dest);
    }
    template<typename AddressType>
    void load8SignedExtendTo32(AddressType address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dest, r.offset, r.base);
        m_assembler.extsb(dest, dest);
        zeroExtend32ToWordInternal(dest);
    }
    template<typename AddressType>
    void load8SignedExtendTo64(AddressType address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dest, r.offset, r.base);
        m_assembler.extsb(dest, dest);
    }
    template<typename AddressType>
    void load16SignedExtendTo64(AddressType address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lha(dest, r.offset, r.base);
    }
    template<typename AddressType>
    void load32SignedExtendTo64(AddressType address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.lwa(dest, r.offset, r.base);
    }
    void load8SignedExtendTo64(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lbz(dest, 0, memoryTempRegister);
        m_assembler.extsb(dest, dest);
    }
    void load16SignedExtendTo64(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lha(dest, 0, memoryTempRegister);
    }
    void load32SignedExtendTo64(const void* address, RegisterID dest)
    {
        moveToAbsolute(address);
        m_assembler.lwa(dest, 0, memoryTempRegister);
    }

    // --- Pair loads/stores (two scalar accesses; no ldp on PPC) ----------

    void loadPair32(RegisterID base, TrustedImm32 offset, RegisterID dest1, RegisterID dest2)
    {
        // dest1 may alias base; load into it LAST when needed.
        if (dest1 == base) {
            load32(Address(base, offset.m_value + 4), dest2);
            load32(Address(base, offset.m_value), dest1);
        } else {
            load32(Address(base, offset.m_value), dest1);
            load32(Address(base, offset.m_value + 4), dest2);
        }
    }
    void loadPair32(RegisterID base, RegisterID dest1, RegisterID dest2)
    {
        loadPair32(base, TrustedImm32(0), dest1, dest2);
    }
    void loadPair64(RegisterID base, TrustedImm32 offset, RegisterID dest1, RegisterID dest2)
    {
        if (dest1 == base) {
            load64(Address(base, offset.m_value + 8), dest2);
            load64(Address(base, offset.m_value), dest1);
        } else {
            load64(Address(base, offset.m_value), dest1);
            load64(Address(base, offset.m_value + 8), dest2);
        }
    }
    void loadPair64(RegisterID base, RegisterID dest1, RegisterID dest2)
    {
        loadPair64(base, TrustedImm32(0), dest1, dest2);
    }
    void loadPair64(Address address, RegisterID dest1, RegisterID dest2)
    {
        loadPair64(address.base, TrustedImm32(address.offset), dest1, dest2);
    }
    void loadPair32(Address address, RegisterID dest1, RegisterID dest2)
    {
        loadPair32(address.base, TrustedImm32(address.offset), dest1, dest2);
    }
    void storePair32(RegisterID src1, RegisterID src2, RegisterID base, TrustedImm32 offset)
    {
        store32(src1, Address(base, offset.m_value));
        store32(src2, Address(base, offset.m_value + 4));
    }
    void storePair32(RegisterID src1, RegisterID src2, Address address)
    {
        store32(src1, address);
        store32(src2, address.withOffset(4));
    }
    void storePair32(RegisterID src1, RegisterID src2, BaseIndex address)
    {
        store32(src1, address);
        store32(src2, BaseIndex(address.base, address.index, address.scale, address.offset + 4));
    }
    void storePair64(RegisterID src1, RegisterID src2, RegisterID base)
    {
        storePair64(src1, src2, base, TrustedImm32(0));
    }
    void storePair64(RegisterID src1, RegisterID src2, RegisterID base, TrustedImm32 offset)
    {
        store64(src1, Address(base, offset.m_value));
        store64(src2, Address(base, offset.m_value + 8));
    }
    void storePair64(RegisterID src1, RegisterID src2, Address address)
    {
        store64(src1, address);
        store64(src2, address.withOffset(8));
    }

    // --- 32-bit arith, memory-operand forms -------------------------------

    void sub32(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        add32(TrustedImm32(-imm.m_value), src, dest);
    }
    void sub32(TrustedImm32 imm, RegisterID src, RegisterID dest)
    {
        // dest = imm - src
        moveImmToScratch(int64_t(imm.m_value), dataTempRegister);
        m_assembler.subf(dest, src, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }
    void sub32(Address address, RegisterID dest)
    {
        load32(address, dataTempRegister);
        sub32(dataTempRegister, dest);
    }
    void sub32(RegisterID src, Address address)
    {
        load32(address, dataTempRegister);
        m_assembler.subf(dataTempRegister, src, dataTempRegister);
        store32(dataTempRegister, address);
    }
    void sub32(TrustedImm32 imm, Address address)
    {
        add32(TrustedImm32(-imm.m_value), address);
    }
    void add32(Address address, RegisterID dest)
    {
        load32(address, dataTempRegister);
        add32(dataTempRegister, dest);
    }
    void add32(RegisterID src, Address address)
    {
        // Keep the resolved address: resolveAddress may cache into r12 and
        // the value scratch is r11 — no overlap.
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.add(dataTempRegister, dataTempRegister, src);
        m_assembler.stw(dataTempRegister, r.offset, r.base);
    }
    void add32(TrustedImm32 imm, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        if (isInt16(imm.m_value))
            m_assembler.addi(dataTempRegister, dataTempRegister, int16_t(imm.m_value));
        else {
            // r11 and r12 are both busy; rebuild the address afterwards.
            add32(imm, dataTempRegister, dataTempRegister);
            r = resolveAddress(address, memoryTempRegister);
        }
        m_assembler.stw(dataTempRegister, r.offset, r.base);
    }
    void add32(TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lwz(dataTempRegister, 0, memoryTempRegister);
        if (isInt16(imm.m_value))
            m_assembler.addi(dataTempRegister, dataTempRegister, int16_t(imm.m_value));
        else {
            add32(imm, dataTempRegister, dataTempRegister);
            moveToAbsolute(address.m_ptr);
        }
        m_assembler.stw(dataTempRegister, 0, memoryTempRegister);
    }
    void add32(AbsoluteAddress address, RegisterID dest)
    {
        load32(address.m_ptr, dataTempRegister);
        add32(dataTempRegister, dest);
    }

    // --- 64-bit shifts (count masked & 63, matching x86/arm64 hardware) --

    void lshift64(TrustedImm32 imm, RegisterID dest) { m_assembler.sldi(dest, dest, imm.m_value & 63); }
    void lshift64(RegisterID src, TrustedImm32 imm, RegisterID dest) { m_assembler.sldi(dest, src, imm.m_value & 63); }
    void lshift64(TrustedImm32 imm, RegisterID src, RegisterID dest) { m_assembler.sldi(dest, src, imm.m_value & 63); }
    void lshift64(RegisterID shiftAmount, RegisterID dest) { lshift64(dest, shiftAmount, dest); }
    void lshift64(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rldicl(dataTempRegister, shiftAmount, 0, 58);   // count & 63
        m_assembler.sld(dest, src, dataTempRegister);
    }

    void rshift64(TrustedImm32 imm, RegisterID dest) { m_assembler.sradi(dest, dest, imm.m_value & 63); }
    void rshift64(RegisterID src, TrustedImm32 imm, RegisterID dest) { m_assembler.sradi(dest, src, imm.m_value & 63); }
    void rshift64(RegisterID shiftAmount, RegisterID dest) { rshift64(dest, shiftAmount, dest); }
    void rshift64(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rldicl(dataTempRegister, shiftAmount, 0, 58);
        m_assembler.srad(dest, src, dataTempRegister);
    }

    void urshift64(TrustedImm32 imm, RegisterID dest) { urshift64(dest, imm, dest); }
    void urshift64(RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        if (!(imm.m_value & 63)) {
            move(src, dest);
            return;
        }
        m_assembler.srdi(dest, src, imm.m_value & 63);
    }
    void urshift64(RegisterID shiftAmount, RegisterID dest) { urshift64(dest, shiftAmount, dest); }
    void urshift64(RegisterID src, RegisterID shiftAmount, RegisterID dest)
    {
        m_assembler.rldicl(dataTempRegister, shiftAmount, 0, 58);
        m_assembler.srd(dest, src, dataTempRegister);
    }

    void rotateRight64(TrustedImm32 imm, RegisterID dest)
    {
        // rotr64(n) = rotl64(64 - n); rldicl with mb=0 is rotldi.
        m_assembler.rldicl(dest, dest, (64 - (imm.m_value & 63)) & 63, 0);
    }

    void not64(RegisterID srcDest) { m_assembler.nor(srcDest, srcDest, srcDest); }
    void not64(RegisterID src, RegisterID dest) { m_assembler.nor(dest, src, src); }

    void countLeadingZeros32(RegisterID src, RegisterID dest) { m_assembler.cntlzw(dest, src); }
    void countLeadingZeros64(RegisterID src, RegisterID dest) { m_assembler.cntlzd(dest, src); }

    // --- Memory-destination logical read-modify-write ---------------------
    // load width, combine, store back. Byte/halfword forms operate on the
    // narrow width; the 32-bit result is not required to zero-extend in
    // memory. src register combines the full low bits (callers pass masked
    // values for narrow widths).

    void or32(RegisterID src, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.or_(dataTempRegister, dataTempRegister, src);
        m_assembler.stw(dataTempRegister, r.offset, r.base);
    }
    void or32(TrustedImm32 imm, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        if (imm.m_value >= 0 && isUInt16(imm.m_value))
            m_assembler.ori(dataTempRegister, dataTempRegister, uint16_t(imm.m_value));
        else {
            // r11 (data) and r12 (mem) are both live; stash the address.
            m_assembler.mr(PPC64Registers::r0, r.base);
            moveImmToScratch(int64_t(imm.m_value), memoryTempRegister);
            m_assembler.or_(dataTempRegister, dataTempRegister, memoryTempRegister);
            m_assembler.stw(dataTempRegister, r.offset, PPC64Registers::r0);
            return;
        }
        m_assembler.stw(dataTempRegister, r.offset, r.base);
    }
    void or32(RegisterID src, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lwz(dataTempRegister, 0, memoryTempRegister);
        m_assembler.or_(dataTempRegister, dataTempRegister, src);
        m_assembler.stw(dataTempRegister, 0, memoryTempRegister);
    }
    void or32(TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lwz(dataTempRegister, 0, memoryTempRegister);
        if (imm.m_value >= 0 && isUInt16(imm.m_value))
            m_assembler.ori(dataTempRegister, dataTempRegister, uint16_t(imm.m_value));
        else {
            moveImmToScratch(int64_t(imm.m_value), PPC64Registers::r0);
            m_assembler.or_(dataTempRegister, dataTempRegister, PPC64Registers::r0);
        }
        m_assembler.stw(dataTempRegister, 0, memoryTempRegister);
    }

    void or16(TrustedImm32 imm, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lhz(dataTempRegister, r.offset, r.base);
        m_assembler.ori(dataTempRegister, dataTempRegister, uint16_t(imm.m_value));
        m_assembler.sth(dataTempRegister, r.offset, r.base);
    }
    void or16(TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lhz(dataTempRegister, 0, memoryTempRegister);
        m_assembler.ori(dataTempRegister, dataTempRegister, uint16_t(imm.m_value));
        m_assembler.sth(dataTempRegister, 0, memoryTempRegister);
    }
    void or16(RegisterID src, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lhz(dataTempRegister, 0, memoryTempRegister);
        m_assembler.or_(dataTempRegister, dataTempRegister, src);
        m_assembler.sth(dataTempRegister, 0, memoryTempRegister);
    }

    void or8(TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lbz(dataTempRegister, 0, memoryTempRegister);
        m_assembler.ori(dataTempRegister, dataTempRegister, uint16_t(imm.m_value & 0xFF));
        m_assembler.stb(dataTempRegister, 0, memoryTempRegister);
    }

    void add8(TrustedImm32 imm, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lbz(dataTempRegister, r.offset, r.base);
        m_assembler.addi(dataTempRegister, dataTempRegister, int16_t(imm.m_value));
        m_assembler.stb(dataTempRegister, r.offset, r.base);
    }

    void sub32(TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.lwz(dataTempRegister, 0, memoryTempRegister);
        if (isInt16(-int64_t(imm.m_value)))
            m_assembler.addi(dataTempRegister, dataTempRegister, int16_t(-imm.m_value));
        else {
            moveImmToScratch(int64_t(imm.m_value), PPC64Registers::r0);
            m_assembler.subf(dataTempRegister, PPC64Registers::r0, dataTempRegister);
        }
        m_assembler.stw(dataTempRegister, 0, memoryTempRegister);
    }

    // --- Memory-source logical (dest is a register) -----------------------

    void and32(Address address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.and_(dest, dest, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }
    void and32(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.and_(dest, dest, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }
    void xor32(Address address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.xor_(dest, dest, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }
    void xor32(BaseIndex address, RegisterID dest)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.xor_(dest, dest, dataTempRegister);
        zeroExtend32ToWordInternal(dest);
    }

    void neg64(RegisterID srcDest) { m_assembler.neg(srcDest, srcDest); }

    // --- Conditional moves with immediate compare / test operands ---------
    // emitTestAndSetCR0: and reg&mask, setting CR0 (via a masked AND that
    // updates CR0); branch bits come from the ResultCondition (Zero/NonZero).
    struct BranchBitsRC { uint32_t bo; uint32_t bi; };
    static BranchBitsRC testBranchBitsFor(ResultCondition cond)
    {
        switch (cond) {
        case Zero:          return { 12, 2 };
        case NonZero:       return { 4,  2 };
        case Signed:        return { 12, 0 };
        case PositiveOrZero:return { 4,  0 };
        default: RELEASE_ASSERT_NOT_REACHED(); return { 20, 0 };
        }
    }

    void moveConditionally32(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID src, RegisterID dest)
    {
        emitCompare32(cond, left, right);
        emitSkipOneInstruction(branchBitsFor(cond).bo ^ 0x8, branchBitsFor(cond).bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionally32(RelationalCondition cond, RegisterID left, TrustedImm32 right, TrustedImm32 thenImm, RegisterID elseCase, RegisterID dest)
    {
        // dest = (cond) ? thenImm : elseCase
        moveImmToScratch(int64_t(thenImm.m_value), dataTempRegister);
        moveConditionallyImpl(branchBitsFor(cond), dataTempRegister, elseCase, dest, [&] { emitCompare32(cond, left, right); });
    }
    void moveConditionally64(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID src, RegisterID dest)
    {
        emitCompare64(cond, left, TrustedImm64(right.m_value));
        emitSkipOneInstruction(branchBitsFor(cond).bo ^ 0x8, branchBitsFor(cond).bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionally64(RelationalCondition cond, RegisterID left, TrustedImm32 right, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        moveConditionallyImpl(branchBitsFor(cond), thenCase, elseCase, dest, [&] { emitCompare64(cond, left, TrustedImm64(right.m_value)); });
    }

    void emitTest64ToCR0(RegisterID reg, RegisterID mask)
    {
        m_assembler.and_(dataTempRegister, reg, mask);
        m_assembler.cmpdi(0, dataTempRegister, 0);
    }
    void emitTest64ToCR0(RegisterID reg, TrustedImm32 mask)
    {
        if (mask.m_value == -1)
            m_assembler.cmpdi(0, reg, 0);
        else if (mask.m_value >= 0 && isUInt16(mask.m_value))
            m_assembler.andi_(dataTempRegister, reg, uint16_t(mask.m_value));  // sets CR0
        else {
            moveImmToScratch(int64_t(mask.m_value), dataTempRegister);
            m_assembler.and_(dataTempRegister, reg, dataTempRegister);
            m_assembler.cmpdi(0, dataTempRegister, 0);
        }
    }
    void moveConditionallyTest64(ResultCondition cond, RegisterID left, RegisterID mask, RegisterID src, RegisterID dest)
    {
        emitTest64ToCR0(left, mask);
        BranchBitsRC b = testBranchBitsFor(cond);
        emitSkipOneInstruction(b.bo ^ 0x8, b.bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionallyTest64(ResultCondition cond, RegisterID left, RegisterID mask, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        BranchBitsRC b = testBranchBitsFor(cond);
        moveConditionallyImpl({ b.bo, b.bi }, thenCase, elseCase, dest, [&] { emitTest64ToCR0(left, mask); });
    }
    void moveConditionallyTest64(ResultCondition cond, RegisterID left, TrustedImm32 mask, RegisterID src, RegisterID dest)
    {
        emitTest64ToCR0(left, mask);
        BranchBitsRC b = testBranchBitsFor(cond);
        emitSkipOneInstruction(b.bo ^ 0x8, b.bi);
        m_assembler.mr(dest, src);
    }
    void moveConditionallyTest64(ResultCondition cond, RegisterID left, TrustedImm32 mask, RegisterID thenCase, RegisterID elseCase, RegisterID dest)
    {
        BranchBitsRC b = testBranchBitsFor(cond);
        moveConditionallyImpl({ b.bo, b.bi }, thenCase, elseCase, dest, [&] { emitTest64ToCR0(left, mask); });
    }
    void neg64(RegisterID src, RegisterID dest) { m_assembler.neg(dest, src); }

    // LR access for prologue/epilogue code (LR is an SPR, not a GPR).
    void moveFromLR(RegisterID dest) { m_assembler.mflr(dest); }
    void moveToLR(RegisterID src)    { m_assembler.mtlr(src); }

    // Indirect jumps: mtctr + bctr (PtrTag is ignored — no pointer auth).
    void farJump(RegisterID target, PtrTag)
    {
        m_assembler.mtctr(target);
        m_assembler.bctr();
    }
    void farJump(Address address, PtrTag tag)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.ld(dataTempRegister, r.offset, r.base);
        farJump(dataTempRegister, tag);
    }
    void farJump(BaseIndex address, PtrTag tag)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.ld(dataTempRegister, r.offset, r.base);
        farJump(dataTempRegister, tag);
    }
    void farJump(AbsoluteAddress address, PtrTag tag)
    {
        moveToAbsolute(address.m_ptr);
        m_assembler.ld(dataTempRegister, 0, memoryTempRegister);
        farJump(dataTempRegister, tag);
    }
    void farJump(TrustedImmPtr imm, PtrTag tag)
    {
        moveImmToScratch(int64_t(imm.asIntptr()), dataTempRegister);
        farJump(dataTempRegister, tag);
    }
    void farJump(RegisterID target, RegisterID)                            { farJump(target, NoPtrTag); }
    void farJump(Address address, RegisterID)                              { farJump(address, NoPtrTag); }

    // Stores (stub) — store64 with Address is implemented above.

    // FP / SIMD moves (stub)
    void moveVector(FPRegisterID, FPRegisterID)      { PPC64_UNIMPLEMENTED(); }

    // Push (stub) — TrustedImm32 overload required by MacroAssembler.h wrappers.
    void push(TrustedImm32)                          { PPC64_UNIMPLEMENTED(); }

    // 32-bit / float / double / vector loads and stores (stub)
    void loadVector(Address, FPRegisterID)           { PPC64_UNIMPLEMENTED(); }
    void storeVector(FPRegisterID, Address)          { PPC64_UNIMPLEMENTED(); }

    // Additional branch32 overloads (stub)

    // Patchable branch stubs
    Jump branchPtrWithPatch(RelationalCondition, Address, DataLabelPtr&, TrustedImmPtr = TrustedImmPtr(nullptr)) { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branch32WithPatch(RelationalCondition, Address, DataLabel32&, TrustedImm32 = TrustedImm32(0))           { PPC64_UNIMPLEMENTED(); return Jump(); }

    // Abort (stub) — called by MacroAssembler::oops() via abortWithReason(B3Oops).
    void abortWithReason(AbortReason reason)
    {
        moveImmToScratch(static_cast<int64_t>(reason), dataTempRegister);
        breakpoint();
    }
    void abortWithReason(AbortReason reason, intptr_t misc)
    {
        moveImmToScratch(static_cast<int64_t>(reason), dataTempRegister);
        moveImmToScratch(static_cast<int64_t>(misc), memoryTempRegister);
        breakpoint();
    }

    // Additional add64 overloads required by MacroAssembler.h addPtr wrappers.
    void add64(Address, RegisterID)                              { PPC64_UNIMPLEMENTED(); }
    void add64(TrustedImm32, Address)                            { PPC64_UNIMPLEMENTED(); }
    void add64(AbsoluteAddress, RegisterID)                      { PPC64_UNIMPLEMENTED(); }
    void add64(TrustedImm32, AbsoluteAddress)                    { PPC64_UNIMPLEMENTED(); }

    // and64 TrustedImmPtr overload (MacroAssembler::andPtr uses it).

    // 64-bit shifts (MacroAssembler::lshiftPtr / rshiftPtr / urshiftPtr).

    // neg64 (MacroAssembler::negPtr)

    // Additional or64 overloads (MacroAssembler::orPtr)

    // rotateRight64 (MacroAssembler::rotateRightPtr)

    // Additional sub64 overloads (MacroAssembler::subPtr)

    // Additional xor64 overloads (MacroAssembler::xorPtr)
    void xor64(Address, RegisterID)                              { PPC64_UNIMPLEMENTED(); }
    void xor64(RegisterID, Address)                              { PPC64_UNIMPLEMENTED(); }

    // Additional load64 overloads (MacroAssembler::loadPtr)

    // loadPair64 (MacroAssembler::loadPairPtr)

    // Additional store64 overloads (MacroAssembler::storePtr)

    // storePair64 (MacroAssembler::storePairPtr)

    // test64 (MacroAssembler::testPtr)
    void test64(ResultCondition, RegisterID, TrustedImm32, RegisterID)      { PPC64_UNIMPLEMENTED(); }
    void test64(ResultCondition, RegisterID, RegisterID, RegisterID)        { PPC64_UNIMPLEMENTED(); }

    // Additional branch64 overloads (MacroAssembler::branchPtr)

    // branchTest64 (MacroAssembler::branchTestPtr)

    // branchAdd64 / branchSub64 (MacroAssembler::branchAddPtr/branchSubPtr)
    Jump branchAdd64(ResultCondition, TrustedImm32, RegisterID)              { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAdd64(ResultCondition, RegisterID, RegisterID)                { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchSub64(ResultCondition cond, RegisterID left, RegisterID right, RegisterID dest)
    {
        if (cond == Overflow) {
            // Signed a-b overflows iff (a^b) & (a^result) is negative. Save the
            // original left first, since dest may alias it.
            m_assembler.mr(dataTempRegister, left);
            sub64(left, right, dest);
            xor64(dataTempRegister, right, memoryTempRegister);   // origLeft ^ right
            xor64(dataTempRegister, dest, dataTempRegister);      // origLeft ^ result
            m_assembler.and_(dataTempRegister, dataTempRegister, memoryTempRegister);
            m_assembler.cmpdi(0, dataTempRegister, 0);
            return Jump(m_assembler.emitUnlinkedBranch(12, 0));   // branch if LT (negative => overflow)
        }
        sub64(left, right, dest);
        return branchTest64Impl(resultConditionForArith(cond), dest);
    }
    Jump branchSub64(ResultCondition cond, RegisterID src, RegisterID dest)              { return branchSub64(cond, dest, src, dest); }
    Jump branchSub64(ResultCondition cond, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchSub64(cond, dest, memoryTempRegister, dest);
    }
    Jump branchSub64(ResultCondition cond, RegisterID left, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchSub64(cond, left, memoryTempRegister, dest);
    }

    // move(TrustedImm64) — used by blinding helpers in MacroAssembler.

    // convertInt32ToDouble(TrustedImm32) — blinding path in MacroAssembler.

    // Additional and64 / xor64 / or64 / sub64 / compare64 overloads.
    void sub64(RegisterID, TrustedImm64, RegisterID)                         { PPC64_UNIMPLEMENTED(); }
    void compare64(RelationalCondition, RegisterID, TrustedImm64, RegisterID){ PPC64_UNIMPLEMENTED(); }

    // move32ToFloat / move64ToDouble with immediate forms.

    // branchDouble — used by MacroAssembler::compareDouble on non-X86/ARM64.

    // 3-operand 32-bit forms (MacroAssembler blinding helpers + lea32).

    // 3-operand 32-bit shifts.
    void lshift32(TrustedImm32, RegisterID, RegisterID)                      { PPC64_UNIMPLEMENTED(); }
    void rshift32(TrustedImm32, RegisterID, RegisterID)                      { PPC64_UNIMPLEMENTED(); }
    void urshift32(TrustedImm32, RegisterID, RegisterID)                     { PPC64_UNIMPLEMENTED(); }

    // Additional branchAdd32 / branchMul32 / branchSub32 overloads.
    Jump branchAdd32(ResultCondition cond, RegisterID src, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchAdd32(cond, src, memoryTempRegister, dest);
    }
    Jump branchMul32(ResultCondition cond, RegisterID op1, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchMul32(cond, op1, memoryTempRegister, dest);
    }
    Jump branchSub32(ResultCondition cond, RegisterID op1, TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(imm.m_value, memoryTempRegister);
        return branchSub32(cond, op1, memoryTempRegister, dest);
    }

    // ------------------------------------------------------------------
    // Calls.
    //
    // Unlike offlineasm, the PtrTag is a compile-time argument here, so C
    // vs JS discrimination is direct.  C callees follow the full ELFv2
    // ABI: they run their GEP off r12 (we set it), scribble the caller's
    // linkage area at sp+0..31, and may clobber r2 (TOC).  Every JSC
    // C-call site reserves maxFrameExtentForSlowPathCall (96 bytes on
    // PPC64LE) below the live frame, so sp+24 — the ELFv2 TOC save slot —
    // is ours to save/restore r2 across the call.  JS callees follow the
    // JSC convention (caller-arranged frame; writing sp+24 would corrupt
    // an argument slot), so they get a bare mtctr/bctrl.
    // ------------------------------------------------------------------

    static bool isCFunctionCallTag(PtrTag tag)
    {
        return tag == OperationPtrTag
            || tag == CFunctionPtrTag
            || tag == HostFunctionPtrTag
            || tag == CustomAccessorPtrTag;
    }

    ALWAYS_INLINE void emitCCallGuardPre()
    {
        m_assembler.std(PPC64Registers::r2, 24, stackPointerRegister);
    }
    ALWAYS_INLINE void emitCCallGuardPost()
    {
        m_assembler.ld(PPC64Registers::r2, 24, stackPointerRegister);
    }

    // nearCall / nearTailCall: fixed patchable slots, linked via linkCall.
    Call nearCall()
    {
        return Call(m_assembler.emitUnlinkedCall(), Call::LinkableNear);
    }
    Call nearTailCall()
    {
        // A tail call is a jump slot; the label is the slot START (what
        // linkJump expects).
        return Call(m_assembler.emitUnlinkedJump(), Call::LinkableNearTail);
    }

    Call call(PtrTag tag)
    {
        bool cCall = isCFunctionCallTag(tag);
        if (cCall)
            emitCCallGuardPre();
        AssemblerLabel label = m_assembler.emitUnlinkedCall();
        if (cCall)
            emitCCallGuardPost();
        // General repatchable call (callOperation etc.): must be Linkable and NOT
        // Near — locationOf()/link() require a non-near call, and JITMathIC queries
        // locationOf(slowPathCall) to repatch it. Flagging it Near made locationOf
        // return a bogus location (RELEASE builds silently repatched the wrong PC →
        // code corruption; the exact fallout was ASLR/layout-sensitive). Only
        // nearCall() is LinkableNear. On PPC both link via the same applyCallSlot,
        // so the flag only affects location/repatch routing, not the emitted slot.
        return Call(label, Call::Linkable);
    }

    Call call(RegisterID target, PtrTag tag)
    {
        bool cCall = isCFunctionCallTag(tag);
        if (cCall)
            emitCCallGuardPre();
        // r12 = entry so a C callee's GEP computes its TOC (harmless for JS).
        m_assembler.mr(memoryTempRegister, target);
        m_assembler.mtctr(memoryTempRegister);
        m_assembler.bctrl();
        if (cCall)
            emitCCallGuardPost();
        return Call(m_assembler.labelIgnoringWatchpoints(), Call::None);
    }

    Call call(Address address, PtrTag tag)
    {
        ResolvedAddress r = resolveAddressDS(address, memoryTempRegister);
        m_assembler.ld(dataTempRegister, r.offset, r.base);
        return call(dataTempRegister, tag);
    }
    Call call(RegisterID callTag)                           { UNUSED_PARAM(callTag); return call(NoPtrTag); }
    Call call(RegisterID target, RegisterID callTag)        { UNUSED_PARAM(callTag); return call(target, NoPtrTag); }
    Call call(Address address, RegisterID callTag)          { UNUSED_PARAM(callTag); return call(address, NoPtrTag); }

    // patchableJumpSize — used by JITMathIC for size computation.
    static ptrdiff_t patchableJumpSize() { return Assembler::patchableJumpSize(); }

    // xor64(TrustedImm32, src, dst) — AssemblyHelpers::branchIfBoolean.

    // branchTest64 with TrustedImm64 mask — AssemblyHelpers::isStrictInt52.

    // FP <-> GPR bit-cast moves.

    // Sign/zero extension.
    void signExtend32ToPtr(TrustedImm32 imm, RegisterID dest)
    {
        moveImmToScratch(int64_t(imm.m_value), dest);
    }
    void signExtend32ToPtr(RegisterID src, RegisterID dest)
    {
        m_assembler.extsw(dest, src);
    }
    void zeroExtend32ToWord(RegisterID src, RegisterID dest)
    {
        m_assembler.rldicl(dest, src, 0, 32);
    }

    // byte loads — AssemblyHelpers::barrierBranch and load8SignedExtendTo32.

    // branch8 with AbsoluteAddress — AssemblyHelpers::barrierBranchWithoutFence.

    // branchTest8 with AbsoluteAddress — AssemblyHelpers::jumpIfMutatorFenceNotNeeded.

    // load8 with raw pointer — AssemblyHelpers::barrierBranch(VM&, JSCell*, GPRReg).

    // or32 with Address — AssemblyHelpers::nukeStructureAndStoreButterfly.

    // Memory barriers (Power ISA v2.07B §1.7.1). lwsync orders all pairs
    // except StoreLoad; a full StoreLoad / seq-cst fence needs sync.
    void memoryFence()    { m_assembler.sync(); }       // full (StoreLoad) fence
    void storeFence()     { m_assembler.lwsync(); }     // release ordering
    void loadFence()      { m_assembler.lwsync(); }     // acquire ordering
    void storeLoadFence() { m_assembler.sync(); }

    // Count-leading-zeros — AssemblyHelpers::emitComputeButterflyIndexingMask.

    // Register swap — CCallHelpers::setupArgumentsWithExecState.
    void swap(RegisterID a, RegisterID b)
    {
        if (a == b)
            return;
        m_assembler.mr(dataTempRegister, a);
        m_assembler.mr(a, b);
        m_assembler.mr(b, dataTempRegister);
    }

    // store64(TrustedImmPtr, Address) — CCallHelpers::storeWasmCalleeToCalleeCallFrame.

    // transferPtr(BaseIndex, BaseIndex) — CCallHelpers tail-call frame copy.

    // Phase 2 stubs surfaced by InlineCacheCompiler / DFGSpeculativeJIT compilation.
    // All PPC64_UNIMPLEMENTED(); the JIT does not run yet on PPC64LE.

    // 32-bit negate
    void neg32(RegisterID, RegisterID)                                     { PPC64_UNIMPLEMENTED(); }

    // load8SignedExtendTo32 / load16 / load16SignedExtendTo32
    void load8SignedExtendTo32(const void*, RegisterID)                    { PPC64_UNIMPLEMENTED(); }
    void load16SignedExtendTo32(const void*, RegisterID)                   { PPC64_UNIMPLEMENTED(); }

    // FP conversions
    void loadFloat16(Address, FPRegisterID)                                { PPC64_UNIMPLEMENTED(); }
    void loadFloat16(BaseIndex, FPRegisterID)                              { PPC64_UNIMPLEMENTED(); }
    void convertFloat16ToDouble(FPRegisterID, FPRegisterID)                { PPC64_UNIMPLEMENTED(); }

    // FP branches

    // 32-bit arithmetic — 3-arg forms (DFGSpeculativeJIT.h:641-679)

    // add32 with TrustedImm32 + Address — DFG/IC use.

    // store32 with absolute pointer — DFGSpeculativeJIT.cpp:511 abortWithReason.

    // branchTest32 with Address operand

    // branchPtr with two Address operands — DFGJITCompiler.h:369

    // getEffectiveAddress — InlineCacheCompiler.cpp:2840

    // BaseIndex / TrustedImmPtr variants of FP load/store not already declared.
    Jump branchIfNaN(FPRegisterID)                                         { PPC64_UNIMPLEMENTED(); return Jump(); }

    // Byte/half store, FP type conversion, FP16, and 32-bit transfers.
    void store8(TrustedImm32, BaseIndex)                                   { PPC64_UNIMPLEMENTED(); }
    void convertDoubleToFloat16(FPRegisterID, FPRegisterID)                { PPC64_UNIMPLEMENTED(); }
    void storeFloat16(FPRegisterID, Address)                               { PPC64_UNIMPLEMENTED(); }
    void storeFloat16(FPRegisterID, BaseIndex)                             { PPC64_UNIMPLEMENTED(); }

    // callOperation — InlineCacheCompiler invokes this for slow-path C calls.
    template<PtrTag tag>
    void callOperation(const CodePtr<tag> operation)
    {
        move(TrustedImmPtr(operation.taggedPtr()), dataTempRegister);
        call(dataTempRegister, tag);
    }

    // test32 — DFG/IC test-and-result. Mirrors RISCV64.
    void test32(ResultCondition, RegisterID, TrustedImm32, RegisterID)     { PPC64_UNIMPLEMENTED(); }
    void test32(ResultCondition, RegisterID, RegisterID, RegisterID)       { PPC64_UNIMPLEMENTED(); }
    void test32(ResultCondition, Address, TrustedImm32, RegisterID)        { PPC64_UNIMPLEMENTED(); }

    // or16 / add8 — small-width arithmetic with memory destination (typed array writes).
    void add8(TrustedImm32, BaseIndex)                                    { PPC64_UNIMPLEMENTED(); }
    void add8(RegisterID, BaseIndex)                                      { PPC64_UNIMPLEMENTED(); }

    // Stores to absolute (raw void*) addresses — DFGOSRExit, DFGJITCompiler.
    // (store64(RegisterID, void*) and store32(RegisterID, void*) already declared above.)
    void store8(TrustedImm32, void*)                                       { PPC64_UNIMPLEMENTED(); }
    void store64(TrustedImm64, void*)                                      { PPC64_UNIMPLEMENTED(); }

    // add32 with TrustedImm32 + AbsoluteAddress — DFGOSRExitCompilerCommon.cpp:52

    // moveWithPatch — DFGLazyJSValue.cpp:252.  Returns DataLabelPtr / DataLabel32.
    DataLabelPtr moveWithPatch(TrustedImmPtr initialValue, RegisterID dest)
    {
        DataLabelPtr label(this);
        moveFixedLi64(dest, uint64_t(initialValue.asIntptr()));
        return label;
    }
    DataLabel32 moveWithPatch(TrustedImm32, RegisterID)                    { PPC64_UNIMPLEMENTED(); return DataLabel32(); }

    // load32 with absolute pointer — DFGSpeculativeJIT.cpp:511.

    // branchTest32 with AbsoluteAddress — DFGSpeculativeJIT.cpp:2460

    // 3-arg add32 — DFGSpeculativeJIT.cpp:2693.

    // FP truncate / convert / zero-to-double — DFGSpeculativeJIT.cpp.
    void branchConvertDoubleToInt32(FPRegisterID src, RegisterID dest, JumpList& failureCases, FPRegisterID fpTemp, bool negZeroCheck = true)
    {
        // Convert the (integer-valued) double to int32 via round-toward-zero,
        // then round-trip back to double and compare: any mismatch or unordered
        // result (NaN, or an out-of-int32-range value that fctiwz saturated to
        // INT32_MIN/MAX) means the double was not an exact int32 -> failure.
        m_assembler.fctiwz(fpTempRegister, src);
        m_assembler.mfvsrwz(dest, fpTempRegister);
        convertInt32ToDouble(dest, fpTemp);
        failureCases.append(branchDouble(DoubleNotEqualOrUnordered, src, fpTemp));

        // Negative zero: dest == 0 but src has its sign bit set (src == -0.0).
        if (negZeroCheck) {
            Jump valueIsNonZero = branchTest32(NonZero, dest);
            moveDoubleTo64(src, dataTempRegister);
            failureCases.append(branch64(LessThan, dataTempRegister, TrustedImm64(0)));
            valueIsNonZero.link(this);
        }
    }

    // FP rounding / arithmetic — DFGSpeculativeJIT.

    // Byte-width test/compare and bitwise NOT.
    void test8(ResultCondition, Address, TrustedImm32, RegisterID)         { PPC64_UNIMPLEMENTED(); }
    void compare8(RelationalCondition, Address, TrustedImm32, RegisterID)  { PPC64_UNIMPLEMENTED(); }
    void compare8(RelationalCondition, Address, RegisterID, RegisterID)    { PPC64_UNIMPLEMENTED(); }
    void not32(RegisterID, RegisterID)                                     { PPC64_UNIMPLEMENTED(); }

    // 4-arg branchAdd32 (ResultCondition, src1, src2, dest).

    // Imm64 overloads — Imm64 is privately derived from TrustedImm64, so callers
    // pass it explicitly for "untrusted" 64-bit immediates (range-check checked).
    Jump branch64(RelationalCondition cond, RegisterID left, Imm64 right)
    {
        return branch64(cond, left, right.asTrustedImm64());
    }

    // FP arithmetic — DFGSpeculativeJIT.

    // Negate-with-branch-on-overflow.
    Jump branchNeg32(ResultCondition, RegisterID)                          { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchNeg64(ResultCondition, RegisterID)                          { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchMul64(ResultCondition, RegisterID, RegisterID, RegisterID)  { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchMul64(ResultCondition, RegisterID, RegisterID)              { PPC64_UNIMPLEMENTED(); return Jump(); }

    // FP truncate / bitwise — DFGSpeculativeJIT FP paths.

    // branch32 / branch64 with memory operand source — DFG.
    // (branch64(RelationalCondition, Address, RegisterID) is already declared above.)

    // branchAdd32 with Address operand — DFG.
    Jump branchAdd32(ResultCondition cond, Address address, RegisterID dest)
    {
        load32(address, dataTempRegister);
        return branchAdd32(cond, dataTempRegister, dest, dest);
    }

    // transfer64 / transferVector / storePair32 — memory shuffles.
    void transferVector(Address, Address)                                  { PPC64_UNIMPLEMENTED(); }
    void transferVector(BaseIndex, BaseIndex)                              { PPC64_UNIMPLEMENTED(); }
    void storePair32(RegisterID, TrustedImm32, Address)                    { PPC64_UNIMPLEMENTED(); }
    void storePair32(TrustedImm32, RegisterID, Address)                    { PPC64_UNIMPLEMENTED(); }
    void storePair32(TrustedImm32, TrustedImm32, Address)                  { PPC64_UNIMPLEMENTED(); }

    // Sign-extend 8/16-bit values; byte-swap halfword.
    void signExtend8To32(RegisterID, RegisterID)                           { PPC64_UNIMPLEMENTED(); }
    void signExtend16To32(RegisterID, RegisterID)                          { PPC64_UNIMPLEMENTED(); }
    void byteSwap16(RegisterID)                                            { PPC64_UNIMPLEMENTED(); }
    void byteSwap32(RegisterID)                                            { PPC64_UNIMPLEMENTED(); }
    void byteSwap64(RegisterID)                                            { PPC64_UNIMPLEMENTED(); }

    // Atomic CAS — Atomics typed-array operations in DFG.
    Jump branchAtomicWeakCAS8(StatusCondition, RegisterID, RegisterID, Address)   { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS8(StatusCondition, RegisterID, RegisterID, BaseIndex) { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS16(StatusCondition, RegisterID, RegisterID, Address)  { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS16(StatusCondition, RegisterID, RegisterID, BaseIndex){ PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS32(StatusCondition, RegisterID, RegisterID, Address)  { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS32(StatusCondition, RegisterID, RegisterID, BaseIndex){ PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS64(StatusCondition, RegisterID, RegisterID, Address)  { PPC64_UNIMPLEMENTED(); return Jump(); }
    Jump branchAtomicWeakCAS64(StatusCondition, RegisterID, RegisterID, BaseIndex){ PPC64_UNIMPLEMENTED(); return Jump(); }

    // sub32 with memory destination — DFGSpeculativeJIT64.cpp:4332/5858.

    // branchPtr with BaseIndex — DFGSpeculativeJIT64.cpp:5830.

    // FP↔int bit-pattern moves (NaN-boxing).  PPC64LE has no native 16-bit
    // float register; these stubs document the surface and crash if invoked.
    void move16ToFloat16(RegisterID, FPRegisterID)                         { PPC64_UNIMPLEMENTED(); }
    void moveFloat16To16(FPRegisterID, RegisterID)                         { PPC64_UNIMPLEMENTED(); }

    // moveDoubleConditionallyDouble — FP-conditional FP-move.
    void moveDoubleConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID) { PPC64_UNIMPLEMENTED(); }
    void moveDoubleConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID) { PPC64_UNIMPLEMENTED(); }

    // 64-bit int ops on FP registers — used for NaN-boxing arithmetic in
    // DFGSpeculativeJIT64.  Implementing these properly will need VSX/VMX
    // (Power ISA v2.07B Book I §6) — out of scope for Phase 1.
    void sub64(FPRegisterID, FPRegisterID, FPRegisterID)                   { PPC64_UNIMPLEMENTED(); }
    void add64(FPRegisterID, FPRegisterID, FPRegisterID)                   { PPC64_UNIMPLEMENTED(); }

    // sub32 with Address source — DFG.

    // 4-arg storePair32 (RegisterID, RegisterID, RegisterID baseGPR, TrustedImm32 offset)
    // is the same as ARM64's pre-indexed pair store with a base+offset operand.

    // store32 to BaseIndex with immediate source.
    void store32(TrustedImm32, BaseIndex)                                  { PPC64_UNIMPLEMENTED(); }
    // storeDouble to absolute pointer.
    // and32 with Address source.

    // moveConditionally — 32-bit and test-64 forms.

    // pushPair / popPair — frame management. ARM64 atomically pushes two regs;
    // PPC64LE has no equivalent atomic — Phase 1 stub.
    void pushPair(RegisterID src1, RegisterID src2)
    {
        m_assembler.stdu(src1, -16, stackPointerRegister);
        m_assembler.std(src2, 8, stackPointerRegister);
    }
    void popPair(RegisterID dest1, RegisterID dest2)
    {
        m_assembler.ld(dest1, 0, stackPointerRegister);
        m_assembler.ld(dest2, 8, stackPointerRegister);
        m_assembler.addi(stackPointerRegister, stackPointerRegister, 16);
    }

    // Memory-source variants of FP arithmetic — DFG/IC.

    // 16-bit memory branch and shift-add.

    // xor32 with Address source — AssemblyHelpers.

    // not64 — bitwise NOT.

    // Atomic 64-bit load — AssemblyHelpers.
    void atomicLoad64(Address, RegisterID)                                 { PPC64_UNIMPLEMENTED(); }
    void atomicLoad64(BaseIndex, RegisterID)                               { PPC64_UNIMPLEMENTED(); }
    void atomicLoad64(const void*, RegisterID)                             { PPC64_UNIMPLEMENTED(); }

    // 64-bit shifts with memory source / 3-arg forms.
    void lshift64(Address, RegisterID, RegisterID)                         { PPC64_UNIMPLEMENTED(); }

    // farJump with TrustedImmPtr target — LLIntThunks.

    // branchAdd32 with TrustedImm32 + Address — JITOpcodes.
    // In-memory add: [address] += imm, then branch on the 32-bit result. The
    // store MUST be emitted before the branch. Assumes an in-range (non-scratch
    // base) Address, which is the DFG/baseline norm; memoryTempRegister is then
    // free to hold the immediate.
    Jump branchAdd32(ResultCondition cond, TrustedImm32 imm, Address address)
    {
        ResolvedAddress r = resolveAddress(address, memoryTempRegister);
        m_assembler.lwz(dataTempRegister, r.offset, r.base);
        m_assembler.extsw(dataTempRegister, dataTempRegister);
        moveImmToScratch(imm.m_value, memoryTempRegister);
        m_assembler.add(dataTempRegister, dataTempRegister, memoryTempRegister);
        m_assembler.stw(dataTempRegister, r.offset, r.base);
        if (cond == Overflow) {
            m_assembler.extsw(memoryTempRegister, dataTempRegister);
            m_assembler.cmpd(0, dataTempRegister, memoryTempRegister);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        }
        m_assembler.rldicl(dataTempRegister, dataTempRegister, 0, 32);
        return branchTestImpl32(resultConditionForArith(cond), dataTempRegister);
    }
    Jump branchAdd32(ResultCondition cond, TrustedImm32 imm, AbsoluteAddress address)
    {
        moveToAbsolute(address.m_ptr);                          // memoryTemp = &address
        m_assembler.lwz(dataTempRegister, 0, memoryTempRegister);
        m_assembler.extsw(dataTempRegister, dataTempRegister);  // dataTemp = value
        moveImmToScratch(imm.m_value, memoryTempRegister);      // memoryTemp = imm
        m_assembler.add(dataTempRegister, dataTempRegister, memoryTempRegister);
        moveToAbsolute(address.m_ptr);                          // re-materialize constant addr
        m_assembler.stw(dataTempRegister, 0, memoryTempRegister);
        if (cond == Overflow) {
            m_assembler.extsw(memoryTempRegister, dataTempRegister);
            m_assembler.cmpd(0, dataTempRegister, memoryTempRegister);
            return Jump(m_assembler.emitUnlinkedBranch(4, 2));
        }
        m_assembler.rldicl(dataTempRegister, dataTempRegister, 0, 32);
        return branchTestImpl32(resultConditionForArith(cond), dataTempRegister);
    }

    // convertInt32ToFloat — Wasm float conversion.
    void convertInt32ToFloat(RegisterID, FPRegisterID)                     { PPC64_UNIMPLEMENTED(); }
    void convertUInt32ToFloat(RegisterID, FPRegisterID)                    { PPC64_UNIMPLEMENTED(); }
    void convertInt64ToFloat(RegisterID, FPRegisterID)                     { PPC64_UNIMPLEMENTED(); }
    void convertUInt64ToFloat(RegisterID, FPRegisterID)                    { PPC64_UNIMPLEMENTED(); }

    // branchTest8 with ExtendedAddress — YarrJIT.
    Jump branchTest8(ResultCondition, ExtendedAddress, TrustedImm32 = TrustedImm32(-1)) { PPC64_UNIMPLEMENTED(); return Jump(); }

    // Unaligned 16-bit load — YarrJIT.
    void load16Unaligned(Address, RegisterID)                              { PPC64_UNIMPLEMENTED(); }
    void load16Unaligned(BaseIndex, RegisterID)                            { PPC64_UNIMPLEMENTED(); }
    void load32WithUnalignedHalfWords(BaseIndex, RegisterID)               { PPC64_UNIMPLEMENTED(); }

    // Sign-extending loads to 64.

    // loadPair32 / loadPair64 — paired loads.

    // Bitfield extract — YarrJIT BoyerMoore SIMD path.
    void extractUnsignedBitfield32(RegisterID, TrustedImm32, TrustedImm32, RegisterID) { PPC64_UNIMPLEMENTED(); }
    void extractUnsignedBitfield64(RegisterID, TrustedImm32, TrustedImm32, RegisterID) { PPC64_UNIMPLEMENTED(); }

    // sub32 3-arg form (RegisterID,RegisterID,RegisterID) — DFG.

    // moveConditionally64 — conditional move based on 64-bit compare.

    // transferPtr Address → Address (additional overload; BaseIndex form already above).

    // 64-bit logical 3-arg form — JITInlines.h. (or64 RegisterID×3 already declared above.)

    // storePtrWithPatch / storePtr-with-patch + patch label — CallLinkInfo.cpp.
    DataLabelPtr storePtrWithPatch(TrustedImmPtr initialValue, Address address)
    {
        DataLabelPtr label = moveWithPatch(initialValue, dataTempRegister);
        store64(dataTempRegister, address);
        return label;
    }
    DataLabelPtr storePtrWithPatch(Address address)
    {
        return storePtrWithPatch(TrustedImmPtr(nullptr), address);
    }

    // Static patch helpers — JIT compile-time stubs (Phase 1 placeholders).
    template<PtrTag startTag, PtrTag destTag>
    static void replaceWithJump(CodeLocationLabel<startTag> instructionStart, CodeLocationLabel<destTag> destination)
    {
        PPC64Assembler::replaceWithJump(instructionStart.dataLocation(), destination.dataLocation());
    }
    template<PtrTag startTag>
    static void replaceWithNops(CodeLocationLabel<startTag> instructionStart, size_t memoryToFillWithNopsInBytes)
    {
        PPC64Assembler::fillNops(instructionStart.dataLocation(), memoryToFillWithNopsInBytes);
    }
    template<PtrTag callTag, PtrTag destTag>
    static void repatchCall(CodeLocationCall<callTag> call, CodeLocationLabel<destTag> destination)
    {
        PPC64Assembler::relinkCall(call.dataLocation(), destination.taggedPtr());
    }
    template<PtrTag callTag, PtrTag destTag>
    static void repatchCall(CodeLocationCall<callTag> call, CodePtr<destTag> destination)
    {
        PPC64Assembler::relinkCall(call.dataLocation(), destination.taggedPtr());
    }

    // Required by LinkBuffer — Phase 1 stub.
    friend class LinkBuffer;
    template<PtrTag tag>
    static void linkCall(void* code, Call call, CodePtr<tag> function)
    {
        // Every call() / nearCall() emits the same 8-insn emitUnlinkedCall slot, so
        // it must be linked with applyCallSlot (which writes the target + mtctr + bctrl,
        // or a bl when in range). Only a tail call is a jump. The Near flag merely
        // distinguishes location/repatch routing (locationOf vs locationOfNearCall) —
        // it does NOT change how the slot is linked. The old `!Near → linkPointer`
        // branch was dead while call() wrongly returned LinkableNear; once call()
        // became Linkable it wrote a bctrl-less li64 pointer into an 8-insn call slot
        // (→ fall-through to pc=0). Route all non-tail calls through applyCallSlot.
        if (call.isFlagSet(Call::Tail))
            PPC64Assembler::linkJump(code, call.m_label, function.untaggedPtr());
        else
            PPC64Assembler::linkCall(code, call.m_label, function.untaggedPtr());
    }
};

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
