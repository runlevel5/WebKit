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
};

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
