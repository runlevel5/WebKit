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
        // Large offset: fold it in.
        m_assembler.addis(scratch, scratch, int16_t((address.offset + 0x8000) >> 16));
        return { scratch, int16_t(address.offset & 0xFFFF) };
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
        if (isUnsignedCondition(cond)) {
            if (isUInt16(uint32_t(right.m_value)))
                m_assembler.cmplwi(0, left, uint16_t(right.m_value));
            else {
                moveImmToScratch(uint32_t(right.m_value), dataTempRegister);
                m_assembler.cmplw(0, left, dataTempRegister);
            }
        } else {
            if (isInt16(right.m_value))
                m_assembler.cmpwi(0, left, int16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, dataTempRegister);
                m_assembler.cmpw(0, left, dataTempRegister);
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
        if (isUnsignedCondition(cond)) {
            if (isUInt16(right.m_value))
                m_assembler.cmpldi(0, left, uint16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, dataTempRegister);
                m_assembler.cmpld(0, left, dataTempRegister);
            }
        } else {
            if (isInt16(right.m_value))
                m_assembler.cmpdi(0, left, int16_t(right.m_value));
            else {
                moveImmToScratch(right.m_value, dataTempRegister);
                m_assembler.cmpd(0, left, dataTempRegister);
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

    void farJump(RegisterID, PtrTag)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void farJump(Address, PtrTag)                                          { UNREACHABLE_FOR_PLATFORM(); }
    void farJump(BaseIndex, PtrTag)                                        { UNREACHABLE_FOR_PLATFORM(); }
    void farJump(AbsoluteAddress, PtrTag)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void farJump(RegisterID target, RegisterID)                            { farJump(target, NoPtrTag); }
    void farJump(Address address, RegisterID)                              { farJump(address, NoPtrTag); }

    // Stores (stub) — store64 with Address is implemented above.
    void store32(RegisterID, Address)                { UNREACHABLE_FOR_PLATFORM(); }
    void store32(TrustedImm32, Address)              { UNREACHABLE_FOR_PLATFORM(); }

    // FP / SIMD moves (stub)
    void move32ToFloat(RegisterID, FPRegisterID)     { UNREACHABLE_FOR_PLATFORM(); }
    void moveDouble(FPRegisterID, FPRegisterID)      { UNREACHABLE_FOR_PLATFORM(); }
    void move64ToDouble(RegisterID, FPRegisterID)    { UNREACHABLE_FOR_PLATFORM(); }
    void moveVector(FPRegisterID, FPRegisterID)      { UNREACHABLE_FOR_PLATFORM(); }
    void convertInt32ToDouble(RegisterID, FPRegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // Push (stub) — TrustedImm32 overload required by MacroAssembler.h wrappers.
    void push(TrustedImm32)                          { UNREACHABLE_FOR_PLATFORM(); }

    // 32-bit / float / double / vector loads and stores (stub)
    void load32(Address, RegisterID)                 { UNREACHABLE_FOR_PLATFORM(); }
    void store32(RegisterID, BaseIndex)              { UNREACHABLE_FOR_PLATFORM(); }
    void loadDouble(Address, FPRegisterID)           { UNREACHABLE_FOR_PLATFORM(); }
    void storeDouble(FPRegisterID, Address)          { UNREACHABLE_FOR_PLATFORM(); }
    void storeDouble(FPRegisterID, BaseIndex)        { UNREACHABLE_FOR_PLATFORM(); }
    void loadFloat(Address, FPRegisterID)            { UNREACHABLE_FOR_PLATFORM(); }
    void loadFloat(TrustedImmPtr, FPRegisterID)      { UNREACHABLE_FOR_PLATFORM(); }
    void negateFloat(FPRegisterID, FPRegisterID)     { UNREACHABLE_FOR_PLATFORM(); }
    void storeFloat(FPRegisterID, Address)           { UNREACHABLE_FOR_PLATFORM(); }
    void storeFloat(FPRegisterID, BaseIndex)         { UNREACHABLE_FOR_PLATFORM(); }
    void loadVector(Address, FPRegisterID)           { UNREACHABLE_FOR_PLATFORM(); }
    void storeVector(FPRegisterID, Address)          { UNREACHABLE_FOR_PLATFORM(); }

    // Additional branch32 overloads (stub)
    Jump branch32(RelationalCondition, AbsoluteAddress, RegisterID)         { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch32(RelationalCondition, RegisterID, Address)                  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch32(RelationalCondition, Address, TrustedImm32)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch8(RelationalCondition, Address, TrustedImm32)                 { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch16(RelationalCondition, Address, TrustedImm32)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // Patchable branch stubs
    Jump branchPtrWithPatch(RelationalCondition, Address, DataLabelPtr&, TrustedImmPtr = TrustedImmPtr(nullptr)) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch32WithPatch(RelationalCondition, Address, DataLabel32&, TrustedImm32 = TrustedImm32(0))           { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // Abort (stub) — called by MacroAssembler::oops() via abortWithReason(B3Oops).
    void abortWithReason(AbortReason)                { UNREACHABLE_FOR_PLATFORM(); }
    void abortWithReason(AbortReason, intptr_t)      { UNREACHABLE_FOR_PLATFORM(); }

    // Additional add64 overloads required by MacroAssembler.h addPtr wrappers.
    void add64(Address, RegisterID)                              { UNREACHABLE_FOR_PLATFORM(); }
    void add64(TrustedImm64, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void add64(TrustedImm64, RegisterID, RegisterID)             { UNREACHABLE_FOR_PLATFORM(); }
    void add64(TrustedImm32, Address)                            { UNREACHABLE_FOR_PLATFORM(); }
    void add64(AbsoluteAddress, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void add64(TrustedImm32, AbsoluteAddress)                    { UNREACHABLE_FOR_PLATFORM(); }

    // and64 TrustedImmPtr overload (MacroAssembler::andPtr uses it).
    void and64(TrustedImmPtr, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }

    // 64-bit shifts (MacroAssembler::lshiftPtr / rshiftPtr / urshiftPtr).
    void lshift64(TrustedImm32, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void lshift64(RegisterID, TrustedImm32, RegisterID)          { UNREACHABLE_FOR_PLATFORM(); }
    void lshift64(TrustedImm32, RegisterID, RegisterID)          { UNREACHABLE_FOR_PLATFORM(); }
    void rshift64(TrustedImm32, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void rshift64(RegisterID, TrustedImm32, RegisterID)          { UNREACHABLE_FOR_PLATFORM(); }
    void urshift64(TrustedImm32, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void urshift64(RegisterID, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }
    void urshift64(RegisterID, TrustedImm32, RegisterID)         { UNREACHABLE_FOR_PLATFORM(); }

    // neg64 (MacroAssembler::negPtr)
    void neg64(RegisterID)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void neg64(RegisterID, RegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }

    // Additional or64 overloads (MacroAssembler::orPtr)
    void or64(TrustedImm32, RegisterID, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); }

    // rotateRight64 (MacroAssembler::rotateRightPtr)
    void rotateRight64(TrustedImm32, RegisterID)                 { UNREACHABLE_FOR_PLATFORM(); }

    // Additional sub64 overloads (MacroAssembler::subPtr)
    void sub64(RegisterID, TrustedImm32, RegisterID)             { UNREACHABLE_FOR_PLATFORM(); }
    void sub64(TrustedImm64, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }

    // Additional xor64 overloads (MacroAssembler::xorPtr)
    void xor64(Address, RegisterID)                              { UNREACHABLE_FOR_PLATFORM(); }
    void xor64(RegisterID, Address)                              { UNREACHABLE_FOR_PLATFORM(); }
    void xor64(TrustedImm64, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }

    // Additional load64 overloads (MacroAssembler::loadPtr)
    void load64(BaseIndex, RegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void load64(const void*, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }

    // loadPair64 (MacroAssembler::loadPairPtr)
    void loadPair64(RegisterID, RegisterID, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void loadPair64(RegisterID, TrustedImm32, RegisterID, RegisterID)        { UNREACHABLE_FOR_PLATFORM(); }
    void loadPair64(Address, RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }

    // Additional store64 overloads (MacroAssembler::storePtr)
    void store64(RegisterID, BaseIndex)                          { UNREACHABLE_FOR_PLATFORM(); }
    void store64(RegisterID, void*)                              { UNREACHABLE_FOR_PLATFORM(); }
    void store64(TrustedImm64, Address)                          { UNREACHABLE_FOR_PLATFORM(); }
    void store64(TrustedImm32, Address)                          { UNREACHABLE_FOR_PLATFORM(); }
    void store64(TrustedImm64, BaseIndex)                        { UNREACHABLE_FOR_PLATFORM(); }
    void store64(TrustedImm32, BaseIndex)                        { UNREACHABLE_FOR_PLATFORM(); }

    // storePair64 (MacroAssembler::storePairPtr)
    void storePair64(RegisterID, RegisterID, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void storePair64(RegisterID, RegisterID, RegisterID, TrustedImm32)      { UNREACHABLE_FOR_PLATFORM(); }
    void storePair64(RegisterID, RegisterID, Address)                       { UNREACHABLE_FOR_PLATFORM(); }

    // test64 (MacroAssembler::testPtr)
    void test64(ResultCondition, RegisterID, TrustedImm32, RegisterID)      { UNREACHABLE_FOR_PLATFORM(); }
    void test64(ResultCondition, RegisterID, RegisterID, RegisterID)        { UNREACHABLE_FOR_PLATFORM(); }

    // Additional branch64 overloads (MacroAssembler::branchPtr)
    Jump branch64(RelationalCondition, RegisterID, Address)                  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch64(RelationalCondition, Address, RegisterID)                  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch64(RelationalCondition, AbsoluteAddress, RegisterID)          { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch64(RelationalCondition, Address, TrustedImm64)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // branchTest64 (MacroAssembler::branchTestPtr)
    Jump branchTest64(ResultCondition, Address, TrustedImm32 = TrustedImm32(-1))                  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchTest64(ResultCondition, Address, RegisterID)                                       { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchTest64(ResultCondition, BaseIndex, TrustedImm32 = TrustedImm32(-1))                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchTest64(ResultCondition, AbsoluteAddress, TrustedImm32 = TrustedImm32(-1))          { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // branchAdd64 / branchSub64 (MacroAssembler::branchAddPtr/branchSubPtr)
    Jump branchAdd64(ResultCondition, TrustedImm32, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAdd64(ResultCondition, RegisterID, RegisterID)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchSub64(ResultCondition, TrustedImm32, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchSub64(ResultCondition, RegisterID, RegisterID)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchSub64(ResultCondition, RegisterID, TrustedImm32, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // move(TrustedImm64) — used by blinding helpers in MacroAssembler.
    void move(TrustedImm64, RegisterID)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // convertInt32ToDouble(TrustedImm32) — blinding path in MacroAssembler.
    void convertInt32ToDouble(TrustedImm32, FPRegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }

    // Additional and64 / xor64 / or64 / sub64 / compare64 overloads.
    void and64(TrustedImm32, RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void and64(TrustedImm64, RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void xor64(TrustedImm64, RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void or64(TrustedImm64, RegisterID, RegisterID)                          { UNREACHABLE_FOR_PLATFORM(); }
    void sub64(RegisterID, TrustedImm64, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void compare64(RelationalCondition, RegisterID, TrustedImm64, RegisterID){ UNREACHABLE_FOR_PLATFORM(); }

    // move32ToFloat / move64ToDouble with immediate forms.
    void move32ToFloat(TrustedImm32, FPRegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void move64ToDouble(TrustedImm64, FPRegisterID)                          { UNREACHABLE_FOR_PLATFORM(); }

    // branchDouble — used by MacroAssembler::compareDouble on non-X86/ARM64.
    Jump branchDouble(DoubleCondition, FPRegisterID, FPRegisterID)           { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // 3-operand 32-bit forms (MacroAssembler blinding helpers + lea32).
    void sub32(RegisterID, TrustedImm32, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }

    // 3-operand 32-bit shifts.
    void lshift32(TrustedImm32, RegisterID, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void rshift32(TrustedImm32, RegisterID, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void urshift32(TrustedImm32, RegisterID, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }

    // Additional branchAdd32 / branchMul32 / branchSub32 overloads.
    Jump branchAdd32(ResultCondition, RegisterID, TrustedImm32, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchMul32(ResultCondition, RegisterID, TrustedImm32, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchSub32(ResultCondition, RegisterID, TrustedImm32, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // nearCall / nearTailCall — required by MacroAssembler::nearCallThunk/nearTailCallThunk.
    Call nearCall()     { UNREACHABLE_FOR_PLATFORM(); return Call(); }
    Call nearTailCall() { UNREACHABLE_FOR_PLATFORM(); return Call(); }

    // call() overloads — required by JIT.h. PtrTag is used for jump-tag/PAC on
    // ARM64E; on PPC64LE we do not authenticate, so the tag is ignored.
    Call call(PtrTag)                                       { UNREACHABLE_FOR_PLATFORM(); return Call(); }
    Call call(RegisterID, PtrTag)                           { UNREACHABLE_FOR_PLATFORM(); return Call(); }
    Call call(Address, PtrTag)                              { UNREACHABLE_FOR_PLATFORM(); return Call(); }
    Call call(RegisterID callTag)                           { UNUSED_PARAM(callTag); return call(NoPtrTag); }
    Call call(RegisterID target, RegisterID callTag)        { UNUSED_PARAM(callTag); return call(target, NoPtrTag); }
    Call call(Address address, RegisterID callTag)          { UNUSED_PARAM(callTag); return call(address, NoPtrTag); }

    // patchableJumpSize — used by JITMathIC for size computation.
    static ptrdiff_t patchableJumpSize() { return Assembler::patchableJumpSize(); }

    // xor64(TrustedImm32, src, dst) — AssemblyHelpers::branchIfBoolean.
    void xor64(TrustedImm32, RegisterID, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }

    // branchTest64 with TrustedImm64 mask — AssemblyHelpers::isStrictInt52.
    Jump branchTest64(ResultCondition, RegisterID, TrustedImm64)            { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // FP <-> GPR bit-cast moves.
    void moveDoubleTo64(FPRegisterID, RegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void truncateDoubleToInt64(FPRegisterID, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void convertInt64ToDouble(RegisterID, FPRegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }

    // Sign/zero extension.
    void signExtend32ToPtr(TrustedImm32, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }
    void signExtend32ToPtr(RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void zeroExtend32ToWord(RegisterID, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }

    // byte loads — AssemblyHelpers::barrierBranch and load8SignedExtendTo32.
    void load8(Address, RegisterID)                                        { UNREACHABLE_FOR_PLATFORM(); }
    void load8(BaseIndex, RegisterID)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // branch8 with AbsoluteAddress — AssemblyHelpers::barrierBranchWithoutFence.
    Jump branch8(RelationalCondition, AbsoluteAddress, TrustedImm32)       { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // branchTest8 with AbsoluteAddress — AssemblyHelpers::jumpIfMutatorFenceNotNeeded.
    Jump branchTest8(ResultCondition, AbsoluteAddress, TrustedImm32 = TrustedImm32(-1)) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // load8 with raw pointer — AssemblyHelpers::barrierBranch(VM&, JSCell*, GPRReg).
    void load8(const void*, RegisterID)                                    { UNREACHABLE_FOR_PLATFORM(); }

    // or32 with Address — AssemblyHelpers::nukeStructureAndStoreButterfly.
    void or32(RegisterID, Address)                                         { UNREACHABLE_FOR_PLATFORM(); }
    void or32(TrustedImm32, Address)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void or32(RegisterID, AbsoluteAddress)                                 { UNREACHABLE_FOR_PLATFORM(); }
    void or32(TrustedImm32, AbsoluteAddress)                               { UNREACHABLE_FOR_PLATFORM(); }

    // Memory barrier instructions — AssemblyHelpers::barrierStoreLoadFence/mutatorFence.
    void memoryFence() { UNREACHABLE_FOR_PLATFORM(); }
    void storeFence()  { UNREACHABLE_FOR_PLATFORM(); }
    void loadFence()   { UNREACHABLE_FOR_PLATFORM(); }

    // Count-leading-zeros — AssemblyHelpers::emitComputeButterflyIndexingMask.
    void countLeadingZeros32(RegisterID, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }

    // Register swap — CCallHelpers::setupArgumentsWithExecState.
    void swap(RegisterID, RegisterID)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // store64(TrustedImmPtr, Address) — CCallHelpers::storeWasmCalleeToCalleeCallFrame.
    void store64(TrustedImmPtr, Address)                                   { UNREACHABLE_FOR_PLATFORM(); }

    // transferPtr(BaseIndex, BaseIndex) — CCallHelpers tail-call frame copy.
    void transferPtr(BaseIndex, BaseIndex)                                 { UNREACHABLE_FOR_PLATFORM(); }

    // Phase 2 stubs surfaced by InlineCacheCompiler / DFGSpeculativeJIT compilation.
    // All UNREACHABLE_FOR_PLATFORM(); the JIT does not run yet on PPC64LE.

    // 32-bit negate
    void neg32(RegisterID, RegisterID)                                     { UNREACHABLE_FOR_PLATFORM(); }

    // load8SignedExtendTo32 / load16 / load16SignedExtendTo32
    void load8SignedExtendTo32(Address, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }
    void load8SignedExtendTo32(BaseIndex, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void load8SignedExtendTo32(const void*, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void load16(Address, RegisterID)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void load16(BaseIndex, RegisterID)                                     { UNREACHABLE_FOR_PLATFORM(); }
    void load16(const void*, RegisterID)                                   { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo32(Address, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo32(BaseIndex, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo32(const void*, RegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }

    // FP conversions
    void convertUInt32ToDouble(RegisterID, FPRegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }
    void convertUInt32ToDouble(RegisterID, FPRegisterID, FPRegisterID)     { UNREACHABLE_FOR_PLATFORM(); }
    void convertFloatToDouble(FPRegisterID, FPRegisterID)                  { UNREACHABLE_FOR_PLATFORM(); }
    void convertInt32ToDouble(BaseIndex, FPRegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void loadFloat16(Address, FPRegisterID)                                { UNREACHABLE_FOR_PLATFORM(); }
    void loadFloat16(BaseIndex, FPRegisterID)                              { UNREACHABLE_FOR_PLATFORM(); }
    void convertFloat16ToDouble(FPRegisterID, FPRegisterID)                { UNREACHABLE_FOR_PLATFORM(); }

    // FP branches
    Jump branchDoubleNonZero(FPRegisterID, FPRegisterID)                   { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchDoubleZeroOrNaN(FPRegisterID, FPRegisterID)                 { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // 32-bit arithmetic — 3-arg forms (DFGSpeculativeJIT.h:641-679)

    // add32 with TrustedImm32 + Address — DFG/IC use.
    void add32(TrustedImm32, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // store32 with absolute pointer — DFGSpeculativeJIT.cpp:511 abortWithReason.
    void store32(RegisterID, void*)                                        { UNREACHABLE_FOR_PLATFORM(); }
    void store32(TrustedImm32, void*)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // branchTest32 with Address operand
    Jump branchTest32(ResultCondition, Address, TrustedImm32 = TrustedImm32(-1)) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // branchPtr with two Address operands — DFGJITCompiler.h:369
    Jump branchPtr(RelationalCondition, Address, Address)                  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // getEffectiveAddress — InlineCacheCompiler.cpp:2840
    void getEffectiveAddress(BaseIndex, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }

    // BaseIndex / TrustedImmPtr variants of FP load/store not already declared.
    void load32(BaseIndex, RegisterID)                                     { UNREACHABLE_FOR_PLATFORM(); }
    void loadFloat(BaseIndex, FPRegisterID)                                { UNREACHABLE_FOR_PLATFORM(); }
    void loadDouble(BaseIndex, FPRegisterID)                               { UNREACHABLE_FOR_PLATFORM(); }
    void loadDouble(TrustedImmPtr, FPRegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    Jump branchIfNaN(FPRegisterID)                                         { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // Byte/half store, FP type conversion, FP16, and 32-bit transfers.
    void store8(RegisterID, Address)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void store8(RegisterID, BaseIndex)                                     { UNREACHABLE_FOR_PLATFORM(); }
    void store8(TrustedImm32, Address)                                     { UNREACHABLE_FOR_PLATFORM(); }
    void store8(TrustedImm32, BaseIndex)                                   { UNREACHABLE_FOR_PLATFORM(); }
    void store16(RegisterID, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void store16(RegisterID, BaseIndex)                                    { UNREACHABLE_FOR_PLATFORM(); }
    void convertDoubleToFloat(FPRegisterID, FPRegisterID)                  { UNREACHABLE_FOR_PLATFORM(); }
    void convertDoubleToFloat16(FPRegisterID, FPRegisterID)                { UNREACHABLE_FOR_PLATFORM(); }
    void storeFloat16(FPRegisterID, Address)                               { UNREACHABLE_FOR_PLATFORM(); }
    void storeFloat16(FPRegisterID, BaseIndex)                             { UNREACHABLE_FOR_PLATFORM(); }
    void transfer32(Address, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void transfer32(BaseIndex, BaseIndex)                                  { UNREACHABLE_FOR_PLATFORM(); }

    // callOperation — InlineCacheCompiler invokes this for slow-path C calls.
    template<PtrTag tag>
    void callOperation(const CodePtr<tag>) { UNREACHABLE_FOR_PLATFORM(); }

    // test32 — DFG/IC test-and-result. Mirrors RISCV64.
    void test32(ResultCondition, RegisterID, TrustedImm32, RegisterID)     { UNREACHABLE_FOR_PLATFORM(); }
    void test32(ResultCondition, RegisterID, RegisterID, RegisterID)       { UNREACHABLE_FOR_PLATFORM(); }
    void test32(ResultCondition, Address, TrustedImm32, RegisterID)        { UNREACHABLE_FOR_PLATFORM(); }

    // or16 / add8 — small-width arithmetic with memory destination (typed array writes).
    void or8(TrustedImm32, AbsoluteAddress)                                { UNREACHABLE_FOR_PLATFORM(); }
    void or16(TrustedImm32, AbsoluteAddress)                               { UNREACHABLE_FOR_PLATFORM(); }
    void or16(TrustedImm32, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void or16(RegisterID, AbsoluteAddress)                                 { UNREACHABLE_FOR_PLATFORM(); }
    void add8(TrustedImm32, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void add8(TrustedImm32, BaseIndex)                                    { UNREACHABLE_FOR_PLATFORM(); }
    void add8(RegisterID, BaseIndex)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // Stores to absolute (raw void*) addresses — DFGOSRExit, DFGJITCompiler.
    // (store64(RegisterID, void*) and store32(RegisterID, void*) already declared above.)
    void store8(TrustedImm32, void*)                                       { UNREACHABLE_FOR_PLATFORM(); }
    void store64(TrustedImm64, void*)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // add32 with TrustedImm32 + AbsoluteAddress — DFGOSRExitCompilerCommon.cpp:52
    void add32(TrustedImm32, AbsoluteAddress)                              { UNREACHABLE_FOR_PLATFORM(); }

    // moveWithPatch — DFGLazyJSValue.cpp:252.  Returns DataLabelPtr / DataLabel32.
    DataLabelPtr moveWithPatch(TrustedImmPtr, RegisterID)                  { UNREACHABLE_FOR_PLATFORM(); return DataLabelPtr(); }
    DataLabel32 moveWithPatch(TrustedImm32, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); return DataLabel32(); }

    // load32 with absolute pointer — DFGSpeculativeJIT.cpp:511.
    void load32(const void*, RegisterID)                                   { UNREACHABLE_FOR_PLATFORM(); }

    // branchTest32 with AbsoluteAddress — DFGSpeculativeJIT.cpp:2460
    Jump branchTest32(ResultCondition, AbsoluteAddress, TrustedImm32 = TrustedImm32(-1)) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // 3-arg add32 — DFGSpeculativeJIT.cpp:2693.

    // FP truncate / convert / zero-to-double — DFGSpeculativeJIT.cpp.
    enum BranchTruncateType { BranchIfTruncateFailed, BranchIfTruncateSuccessful };
    Jump branchTruncateDoubleToInt32(FPRegisterID, RegisterID, BranchTruncateType = BranchIfTruncateFailed) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    void branchConvertDoubleToInt32(FPRegisterID, RegisterID, JumpList&, FPRegisterID, bool = true) { UNREACHABLE_FOR_PLATFORM(); }
    void moveZeroToDouble(FPRegisterID)                                    { UNREACHABLE_FOR_PLATFORM(); }

    // FP rounding / arithmetic — DFGSpeculativeJIT.
    void roundTowardNearestIntDouble(FPRegisterID, FPRegisterID)           { UNREACHABLE_FOR_PLATFORM(); }
    void roundTowardZeroDouble(FPRegisterID, FPRegisterID)                 { UNREACHABLE_FOR_PLATFORM(); }
    void truncateDoubleToInt32(FPRegisterID, RegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }
    void addDouble(FPRegisterID, FPRegisterID, FPRegisterID)               { UNREACHABLE_FOR_PLATFORM(); }
    void addDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }
    void absDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }

    // Byte-width test/compare and bitwise NOT.
    void test8(ResultCondition, Address, TrustedImm32, RegisterID)         { UNREACHABLE_FOR_PLATFORM(); }
    void compare8(RelationalCondition, Address, TrustedImm32, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); }
    void compare8(RelationalCondition, Address, RegisterID, RegisterID)    { UNREACHABLE_FOR_PLATFORM(); }
    void not32(RegisterID, RegisterID)                                     { UNREACHABLE_FOR_PLATFORM(); }

    // 4-arg branchAdd32 (ResultCondition, src1, src2, dest).

    // Imm64 overloads — Imm64 is privately derived from TrustedImm64, so callers
    // pass it explicitly for "untrusted" 64-bit immediates (range-check checked).
    Jump branch64(RelationalCondition cond, RegisterID left, Imm64 right)
    {
        return branch64(cond, left, right.asTrustedImm64());
    }

    // FP arithmetic — DFGSpeculativeJIT.
    void subDouble(FPRegisterID, FPRegisterID, FPRegisterID)               { UNREACHABLE_FOR_PLATFORM(); }
    void subDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }
    void mulDouble(FPRegisterID, FPRegisterID, FPRegisterID)               { UNREACHABLE_FOR_PLATFORM(); }
    void mulDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }
    void divDouble(FPRegisterID, FPRegisterID, FPRegisterID)               { UNREACHABLE_FOR_PLATFORM(); }
    void divDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }
    void negateDouble(FPRegisterID, FPRegisterID)                          { UNREACHABLE_FOR_PLATFORM(); }
    void floorDouble(FPRegisterID, FPRegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void ceilDouble(FPRegisterID, FPRegisterID)                            { UNREACHABLE_FOR_PLATFORM(); }
    void sqrtDouble(FPRegisterID, FPRegisterID)                            { UNREACHABLE_FOR_PLATFORM(); }

    // Negate-with-branch-on-overflow.
    Jump branchNeg32(ResultCondition, RegisterID)                          { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchNeg64(ResultCondition, RegisterID)                          { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchMul64(ResultCondition, RegisterID, RegisterID, RegisterID)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchMul64(ResultCondition, RegisterID, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // FP truncate / bitwise — DFGSpeculativeJIT FP paths.
    void truncDouble(FPRegisterID, FPRegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void orDouble(FPRegisterID, FPRegisterID, FPRegisterID)                { UNREACHABLE_FOR_PLATFORM(); }
    void orDouble(FPRegisterID, FPRegisterID)                              { UNREACHABLE_FOR_PLATFORM(); }
    void andDouble(FPRegisterID, FPRegisterID, FPRegisterID)               { UNREACHABLE_FOR_PLATFORM(); }
    void andDouble(FPRegisterID, FPRegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }

    // branch32 / branch64 with memory operand source — DFG.
    // (branch64(RelationalCondition, Address, RegisterID) is already declared above.)
    Jump branch32(RelationalCondition, Address, RegisterID)                { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch32(RelationalCondition, BaseIndex, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branch64(RelationalCondition, BaseIndex, RegisterID)              { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // branchAdd32 with Address operand — DFG.
    Jump branchAdd32(ResultCondition, Address, RegisterID)                 { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // transfer64 / transferVector / storePair32 — memory shuffles.
    void transfer64(Address, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void transfer64(BaseIndex, BaseIndex)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void transferVector(Address, Address)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void transferVector(BaseIndex, BaseIndex)                              { UNREACHABLE_FOR_PLATFORM(); }
    void storePair32(RegisterID, RegisterID, Address)                      { UNREACHABLE_FOR_PLATFORM(); }
    void storePair32(RegisterID, RegisterID, BaseIndex)                    { UNREACHABLE_FOR_PLATFORM(); }
    void storePair32(RegisterID, TrustedImm32, Address)                    { UNREACHABLE_FOR_PLATFORM(); }
    void storePair32(TrustedImm32, RegisterID, Address)                    { UNREACHABLE_FOR_PLATFORM(); }
    void storePair32(TrustedImm32, TrustedImm32, Address)                  { UNREACHABLE_FOR_PLATFORM(); }

    // Sign-extend 8/16-bit values; byte-swap halfword.
    void signExtend8To32(RegisterID, RegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }
    void signExtend16To32(RegisterID, RegisterID)                          { UNREACHABLE_FOR_PLATFORM(); }
    void byteSwap16(RegisterID)                                            { UNREACHABLE_FOR_PLATFORM(); }
    void byteSwap32(RegisterID)                                            { UNREACHABLE_FOR_PLATFORM(); }
    void byteSwap64(RegisterID)                                            { UNREACHABLE_FOR_PLATFORM(); }

    // Atomic CAS — Atomics typed-array operations in DFG.
    Jump branchAtomicWeakCAS8(StatusCondition, RegisterID, RegisterID, Address)   { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS8(StatusCondition, RegisterID, RegisterID, BaseIndex) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS16(StatusCondition, RegisterID, RegisterID, Address)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS16(StatusCondition, RegisterID, RegisterID, BaseIndex){ UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS32(StatusCondition, RegisterID, RegisterID, Address)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS32(StatusCondition, RegisterID, RegisterID, BaseIndex){ UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS64(StatusCondition, RegisterID, RegisterID, Address)  { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAtomicWeakCAS64(StatusCondition, RegisterID, RegisterID, BaseIndex){ UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // sub32 with memory destination — DFGSpeculativeJIT64.cpp:4332/5858.
    void sub32(TrustedImm32, Address)                                      { UNREACHABLE_FOR_PLATFORM(); }
    void sub32(TrustedImm32, AbsoluteAddress)                              { UNREACHABLE_FOR_PLATFORM(); }

    // branchPtr with BaseIndex — DFGSpeculativeJIT64.cpp:5830.
    Jump branchPtr(RelationalCondition, BaseIndex, RegisterID)             { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // FP↔int bit-pattern moves (NaN-boxing).  PPC64LE has no native 16-bit
    // float register; these stubs document the surface and crash if invoked.
    void move16ToFloat16(RegisterID, FPRegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void moveFloat16To16(FPRegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void moveFloatTo32(FPRegisterID, RegisterID)                           { UNREACHABLE_FOR_PLATFORM(); }

    // moveDoubleConditionallyDouble — FP-conditional FP-move.
    void moveDoubleConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveDoubleConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, FPRegisterID, FPRegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // 64-bit int ops on FP registers — used for NaN-boxing arithmetic in
    // DFGSpeculativeJIT64.  Implementing these properly will need VSX/VMX
    // (Power ISA v2.07B Book I §6) — out of scope for Phase 1.
    void sub64(FPRegisterID, FPRegisterID, FPRegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }
    void add64(FPRegisterID, FPRegisterID, FPRegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }

    // sub32 with Address source — DFG.
    void sub32(Address, RegisterID)                                        { UNREACHABLE_FOR_PLATFORM(); }

    // 4-arg storePair32 (RegisterID, RegisterID, RegisterID baseGPR, TrustedImm32 offset)
    // is the same as ARM64's pre-indexed pair store with a base+offset operand.
    void storePair32(RegisterID, RegisterID, RegisterID, TrustedImm32)     { UNREACHABLE_FOR_PLATFORM(); }

    // store32 to BaseIndex with immediate source.
    void store32(TrustedImm32, BaseIndex)                                  { UNREACHABLE_FOR_PLATFORM(); }
    // storeDouble to absolute pointer.
    void storeDouble(FPRegisterID, TrustedImmPtr)                          { UNREACHABLE_FOR_PLATFORM(); }
    // and32 with Address source.
    void and32(Address, RegisterID)                                        { UNREACHABLE_FOR_PLATFORM(); }
    void and32(BaseIndex, RegisterID)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // moveConditionally — 32-bit and test-64 forms.
    void moveConditionally32(RelationalCondition, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally32(RelationalCondition, RegisterID, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally32(RelationalCondition, RegisterID, TrustedImm32, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally32(RelationalCondition, RegisterID, TrustedImm32, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally32(RelationalCondition, RegisterID, TrustedImm32, TrustedImm32, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyDouble(DoubleCondition, FPRegisterID, FPRegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyTest64(ResultCondition, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyTest64(ResultCondition, RegisterID, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyTest64(ResultCondition, RegisterID, TrustedImm32, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionallyTest64(ResultCondition, RegisterID, TrustedImm32, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // pushPair / popPair — frame management. ARM64 atomically pushes two regs;
    // PPC64LE has no equivalent atomic — Phase 1 stub.
    void pushPair(RegisterID, RegisterID)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void popPair(RegisterID, RegisterID)                                   { UNREACHABLE_FOR_PLATFORM(); }

    // Memory-source variants of FP arithmetic — DFG/IC.
    void mulDouble(Address, FPRegisterID)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void mulDouble(BaseIndex, FPRegisterID)                                { UNREACHABLE_FOR_PLATFORM(); }
    void addDouble(Address, FPRegisterID)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void subDouble(Address, FPRegisterID)                                  { UNREACHABLE_FOR_PLATFORM(); }
    void divDouble(Address, FPRegisterID)                                  { UNREACHABLE_FOR_PLATFORM(); }

    // 16-bit memory branch and shift-add.
    Jump branch32WithMemory16(RelationalCondition, Address, RegisterID)    { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    void addUnsignedRightShift32(RegisterID, RegisterID, TrustedImm32, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // xor32 with Address source — AssemblyHelpers.
    void xor32(Address, RegisterID)                                        { UNREACHABLE_FOR_PLATFORM(); }
    void xor32(BaseIndex, RegisterID)                                      { UNREACHABLE_FOR_PLATFORM(); }

    // not64 — bitwise NOT.
    void not64(RegisterID)                                                 { UNREACHABLE_FOR_PLATFORM(); }
    void not64(RegisterID, RegisterID)                                     { UNREACHABLE_FOR_PLATFORM(); }

    // Atomic 64-bit load — AssemblyHelpers.
    void atomicLoad64(Address, RegisterID)                                 { UNREACHABLE_FOR_PLATFORM(); }
    void atomicLoad64(BaseIndex, RegisterID)                               { UNREACHABLE_FOR_PLATFORM(); }
    void atomicLoad64(const void*, RegisterID)                             { UNREACHABLE_FOR_PLATFORM(); }

    // 64-bit shifts with memory source / 3-arg forms.
    void lshift64(Address, RegisterID, RegisterID)                         { UNREACHABLE_FOR_PLATFORM(); }
    void lshift64(RegisterID, RegisterID, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void rshift64(RegisterID, RegisterID, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void urshift64(RegisterID, RegisterID, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }

    // farJump with TrustedImmPtr target — LLIntThunks.
    void farJump(TrustedImmPtr, PtrTag)                                    { UNREACHABLE_FOR_PLATFORM(); }

    // branchAdd32 with TrustedImm32 + Address — JITOpcodes.
    Jump branchAdd32(ResultCondition, TrustedImm32, Address)               { UNREACHABLE_FOR_PLATFORM(); return Jump(); }
    Jump branchAdd32(ResultCondition, TrustedImm32, AbsoluteAddress)       { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // convertInt32ToFloat — Wasm float conversion.
    void convertInt32ToFloat(RegisterID, FPRegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void convertUInt32ToFloat(RegisterID, FPRegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void convertInt64ToFloat(RegisterID, FPRegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void convertUInt64ToFloat(RegisterID, FPRegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }

    // branchTest8 with ExtendedAddress — YarrJIT.
    Jump branchTest8(ResultCondition, ExtendedAddress, TrustedImm32 = TrustedImm32(-1)) { UNREACHABLE_FOR_PLATFORM(); return Jump(); }

    // Unaligned 16-bit load — YarrJIT.
    void load16Unaligned(Address, RegisterID)                              { UNREACHABLE_FOR_PLATFORM(); }
    void load16Unaligned(BaseIndex, RegisterID)                            { UNREACHABLE_FOR_PLATFORM(); }
    void load32WithUnalignedHalfWords(BaseIndex, RegisterID)               { UNREACHABLE_FOR_PLATFORM(); }

    // Sign-extending loads to 64.
    void load8SignedExtendTo64(Address, RegisterID)                        { UNREACHABLE_FOR_PLATFORM(); }
    void load8SignedExtendTo64(BaseIndex, RegisterID)                      { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo64(Address, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo64(BaseIndex, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void load32SignedExtendTo64(Address, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }
    void load32SignedExtendTo64(BaseIndex, RegisterID)                     { UNREACHABLE_FOR_PLATFORM(); }
    void load8SignedExtendTo64(const void*, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void load16SignedExtendTo64(const void*, RegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }
    void load32SignedExtendTo64(const void*, RegisterID)                   { UNREACHABLE_FOR_PLATFORM(); }

    // loadPair32 / loadPair64 — paired loads.
    void loadPair32(RegisterID, RegisterID, RegisterID, RegisterID)        { UNREACHABLE_FOR_PLATFORM(); }
    void loadPair32(RegisterID, RegisterID, RegisterID)                    { UNREACHABLE_FOR_PLATFORM(); }
    void loadPair32(RegisterID, TrustedImm32, RegisterID, RegisterID)     { UNREACHABLE_FOR_PLATFORM(); }
    void loadPair32(Address, RegisterID, RegisterID)                       { UNREACHABLE_FOR_PLATFORM(); }

    // Bitfield extract — YarrJIT BoyerMoore SIMD path.
    void extractUnsignedBitfield32(RegisterID, TrustedImm32, TrustedImm32, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void extractUnsignedBitfield64(RegisterID, TrustedImm32, TrustedImm32, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // sub32 3-arg form (RegisterID,RegisterID,RegisterID) — DFG.

    // moveConditionally64 — conditional move based on 64-bit compare.
    void moveConditionally64(RelationalCondition, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally64(RelationalCondition, RegisterID, RegisterID, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally64(RelationalCondition, RegisterID, TrustedImm32, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }
    void moveConditionally64(RelationalCondition, RegisterID, TrustedImm32, RegisterID, RegisterID, RegisterID) { UNREACHABLE_FOR_PLATFORM(); }

    // transferPtr Address → Address (additional overload; BaseIndex form already above).
    void transferPtr(Address, Address)                                    { UNREACHABLE_FOR_PLATFORM(); }

    // 64-bit logical 3-arg form — JITInlines.h. (or64 RegisterID×3 already declared above.)

    // storePtrWithPatch / storePtr-with-patch + patch label — CallLinkInfo.cpp.
    DataLabelPtr storePtrWithPatch(TrustedImmPtr, Address)                { UNREACHABLE_FOR_PLATFORM(); return DataLabelPtr(); }
    DataLabelPtr storePtrWithPatch(Address)                               { UNREACHABLE_FOR_PLATFORM(); return DataLabelPtr(); }

    // Static patch helpers — JIT compile-time stubs (Phase 1 placeholders).
    template<PtrTag startTag, PtrTag destTag>
    static void replaceWithJump(CodeLocationLabel<startTag>, CodeLocationLabel<destTag>) { UNREACHABLE_FOR_PLATFORM(); }
    template<PtrTag startTag>
    static void replaceWithNops(CodeLocationLabel<startTag>, size_t)      { UNREACHABLE_FOR_PLATFORM(); }
    template<PtrTag callTag, PtrTag destTag>
    static void repatchCall(CodeLocationCall<callTag>, CodeLocationLabel<destTag>) { UNREACHABLE_FOR_PLATFORM(); }
    template<PtrTag callTag, PtrTag destTag>
    static void repatchCall(CodeLocationCall<callTag>, CodePtr<destTag>)  { UNREACHABLE_FOR_PLATFORM(); }

    // Required by LinkBuffer — Phase 1 stub.
    friend class LinkBuffer;
    template<PtrTag tag>
    static void linkCall(void* code, Call call, CodePtr<tag> function)
    {
        if (!call.isFlagSet(Call::Near))
            PPC64Assembler::linkPointer(code, call.m_label.labelAtOffset(0), function.taggedPtr());
        else
            PPC64Assembler::linkCall(code, call.m_label, function.untaggedPtr());
    }
};

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
