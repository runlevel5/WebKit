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

#include "AssemblerBuffer.h"
#include "AssemblerCommon.h"
#include "PPC64Registers.h"

// Power ISA v2.07B (POWER8) is the baseline for every instruction encoded by
// this assembler. Instructions are 32 bits wide and stored in memory as
// little-endian bytes on PPC64LE (the native byte order). This file adds one
// opcode at a time; every addition must be verified byte-for-byte against GNU
// `as` output for the same mnemonic. See PLAN.md Phase 1 §Verification.

namespace JSC {

namespace PPC64Registers {

typedef enum : int8_t {
#define REGISTER_ID(id, name, r, cs) id,
    FOR_EACH_GP_REGISTER(REGISTER_ID)
#undef REGISTER_ID

#define REGISTER_ALIAS(id, name, alias) id = alias,
    FOR_EACH_REGISTER_ALIAS(REGISTER_ALIAS)
#undef REGISTER_ALIAS

    InvalidGPRReg = -1,
} RegisterID;

typedef enum : int8_t {
#define REGISTER_ID(id, name) id,
    FOR_EACH_SP_REGISTER(REGISTER_ID)
#undef REGISTER_ID

    InvalidSPReg = -1,
} SPRegisterID;

typedef enum : int8_t {
#define REGISTER_ID(id, name, r, cs) id,
    FOR_EACH_FP_REGISTER(REGISTER_ID)
#undef REGISTER_ID

    InvalidFPRReg = -1,
} FPRegisterID;

// VMX vector registers v0-v31. Distinct enum from FPRegisterID because
// although both share the underlying VSR file (v0..v31 ≡ VSR32..VSR63,
// f0..f31 ≡ VSR0..VSR31's high half), the encodings live in different
// instruction-form slots and the calling-convention save/restore rules
// differ. JSC's PPC64 backend will track Simd128 values in this set.
typedef enum : int8_t {
#define REGISTER_ID(id, name, r, cs) id,
    FOR_EACH_VR_REGISTER(REGISTER_ID)
#undef REGISTER_ID

    InvalidVRReg = -1,
} VRegisterID;

} // namespace PPC64Registers

class PPC64Assembler {
public:
    using RegisterID = PPC64Registers::RegisterID;
    using SPRegisterID = PPC64Registers::SPRegisterID;
    using FPRegisterID = PPC64Registers::FPRegisterID;
    using VRegisterID = PPC64Registers::VRegisterID;

    static constexpr RegisterID firstRegister() { return PPC64Registers::r0; }
    static constexpr RegisterID lastRegister() { return PPC64Registers::r31; }
    static constexpr unsigned numberOfRegisters() { return lastRegister() - firstRegister() + 1; }

    static constexpr SPRegisterID firstSPRegister() { return PPC64Registers::pc; }
    static constexpr SPRegisterID lastSPRegister() { return PPC64Registers::pc; }
    static constexpr unsigned numberOfSPRegisters() { return lastSPRegister() - firstSPRegister() + 1; }

    static constexpr FPRegisterID firstFPRegister() { return PPC64Registers::f0; }
    static constexpr FPRegisterID lastFPRegister() { return PPC64Registers::f31; }
    static constexpr unsigned numberOfFPRegisters() { return lastFPRegister() - firstFPRegister() + 1; }

    static constexpr VRegisterID firstVRegister() { return PPC64Registers::v0; }
    static constexpr VRegisterID lastVRegister() { return PPC64Registers::v31; }
    static constexpr unsigned numberOfVRegisters() { return lastVRegister() - firstVRegister() + 1; }

    static ASCIILiteral gprName(RegisterID id)
    {
        ASSERT(id >= firstRegister() && id <= lastRegister());
        static constexpr ASCIILiteral nameForRegister[numberOfRegisters()] = {
#define REGISTER_NAME(id, name, r, cs) name,
            FOR_EACH_GP_REGISTER(REGISTER_NAME)
#undef REGISTER_NAME
        };
        return nameForRegister[id];
    }

    static ASCIILiteral sprName(SPRegisterID id)
    {
        ASSERT(id >= firstSPRegister() && id <= lastSPRegister());
        static constexpr ASCIILiteral nameForRegister[numberOfSPRegisters()] = {
#define REGISTER_NAME(id, name) name,
            FOR_EACH_SP_REGISTER(REGISTER_NAME)
#undef REGISTER_NAME
        };
        return nameForRegister[id];
    }

    static ASCIILiteral fprName(FPRegisterID id)
    {
        ASSERT(id >= firstFPRegister() && id <= lastFPRegister());
        static constexpr ASCIILiteral nameForRegister[numberOfFPRegisters()] = {
#define REGISTER_NAME(id, name, r, cs) name,
            FOR_EACH_FP_REGISTER(REGISTER_NAME)
#undef REGISTER_NAME
        };
        return nameForRegister[id];
    }

    static ASCIILiteral vrName(VRegisterID id)
    {
        ASSERT(id >= firstVRegister() && id <= lastVRegister());
        static constexpr ASCIILiteral nameForRegister[numberOfVRegisters()] = {
#define REGISTER_NAME(id, name, r, cs) name,
            FOR_EACH_VR_REGISTER(REGISTER_NAME)
#undef REGISTER_NAME
        };
        return nameForRegister[id];
    }

    PPC64Assembler() = default;

    AssemblerBuffer& buffer() LIFETIME_BOUND { return m_buffer; }

    AssemblerLabel label()
    {
        return m_buffer.label();
    }

    AssemblerLabel labelIgnoringWatchpoints()
    {
        return m_buffer.label();
    }

    size_t codeSize() const { return m_buffer.codeSize(); }

    // ===================================================================
    // Opcodes. Each instruction is 32 bits; encoding matches Power ISA
    // v2.07B Book I unless noted. Instructions are added one-by-one, each
    // verified byte-for-byte against GNU `as` on PPC64LE.
    // ===================================================================

    // ori — Or Immediate. Power ISA v2.07B §3.3.9, D-form, opcode 24.
    //   Encoding: [op(6)=24 | RS(5) | RA(5) | UI(16)]
    //   Semantics: RA <- RS | zero_extend(UI). Does not set CR.
    // Verified 2026-04-24 on POWER9 / GCC as:
    //   ori 3,4,0x1234 → 0x60831234 (bytes 34 12 83 60)
    //   ori 0,0,0      → 0x60000000 (bytes 00 00 00 60)
    void ori(RegisterID ra, RegisterID rs, uint16_t ui)
    {
        insn(dForm(24, rs, ra, ui));
    }

    // ===================================================================
    // Multiply (XO-form for register×register, D-form for ×immediate).
    // Power ISA v2.07B §3.3.8. The "low" variants store the low N bits
    // of the product into RT (where N = 32 for mullw, 64 for mulld).
    // The "high" variants store the high N bits of a 2N-bit product;
    // they come in signed (mulh{w,d}) and unsigned (mulh{w,d}u) forms.
    // ===================================================================

    // mullw RT, RA, RB — opcode 31, XO=235. RT[low32] <- (RA*RB)[low32]
    //                    (high 32 of RT undefined per §3.3.8).
    // Verified: mullw 3,4,5 → 0x7c6429d6 (bytes d6 29 64 7c)
    void mullw(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 235, /*Rc*/ 0));
    }

    // mulld RT, RA, RB — opcode 31, XO=233. RT <- (RA*RB)[low64].
    // Verified: mulld 3,4,5 → 0x7c6429d2
    void mulld(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 233, /*Rc*/ 0));
    }

    // mulhw / mulhwu — high 32 bits of 32×32 → 64-bit product (signed/unsigned).
    //   XO=75 (signed), XO=11 (unsigned). Verified:
    //   mulhw  3,4,5 → 0x7c642896
    //   mulhwu 3,4,5 → 0x7c642816
    void mulhw(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 75, /*Rc*/ 0));
    }

    void mulhwu(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 11, /*Rc*/ 0));
    }

    // mulhd / mulhdu — high 64 bits of 64×64 → 128-bit product (signed/unsigned).
    //   XO=73 (signed), XO=9 (unsigned). Verified:
    //   mulhd  3,4,5 → 0x7c642892
    //   mulhdu 3,4,5 → 0x7c642812
    void mulhd(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 73, /*Rc*/ 0));
    }

    void mulhdu(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 9, /*Rc*/ 0));
    }

    // mulli RT, RA, SI — D-form, opcode 7. Multiply Low Immediate (16-bit
    //   signed). RT <- (RA * sign_extend(SI))[low64].
    // Verified: mulli 3,4,100 → 0x1c640064, mulli 3,4,-1 → 0x1c64ffff.
    void mulli(RegisterID rt, RegisterID ra, int16_t si)
    {
        insn(dForm(7, rt, ra, static_cast<uint16_t>(si)));
    }

    // ===================================================================
    // Divide (XO-form). Power ISA v2.07B §3.3.8. Signed / unsigned
    // variants at 32-bit (divw / divwu) and 64-bit (divd / divdu).
    // On divide-by-zero or signed-overflow the result is undefined
    // unless OE=1, in which case XER[OV] is set and the caller must
    // check — we emit OE=0 here.
    // ===================================================================

    // divw  RT, RA, RB — opcode 31, XO=491 (signed 32-bit: RA/RB).
    // Verified: divw 3,4,5 → 0x7c642bd6 (bytes d6 2b 64 7c).
    void divw(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 491, /*Rc*/ 0));
    }

    // divwu RT, RA, RB — opcode 31, XO=459 (unsigned 32-bit).
    // Verified: divwu 3,4,5 → 0x7c642b96.
    void divwu(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 459, /*Rc*/ 0));
    }

    // divd  RT, RA, RB — opcode 31, XO=489 (signed 64-bit).
    // Verified: divd 3,4,5 → 0x7c642bd2.
    void divd(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 489, /*Rc*/ 0));
    }

    // divdu RT, RA, RB — opcode 31, XO=457 (unsigned 64-bit).
    // Verified: divdu 3,4,5 → 0x7c642b92.
    void divdu(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 457, /*Rc*/ 0));
    }

    // subf — Subtract From. Power ISA v2.07B §3.3.8, XO-form, opcode 31, XO=40.
    //   Encoding: [op(6)=31 | RT(5) | RA(5) | RB(5) | OE(1)=0 | XO(9)=40 | Rc(1)=0]
    //   Semantics: RT <- RB - RA   (**reverse operand order** vs. most ISAs).
    //   The "sub RT, RA, RB" simplified mnemonic maps to `subf RT, RB, RA`.
    // Verified 2026-04-25 on POWER9:
    //   subf 3,4,5 → 0x7c642850 (bytes 50 28 64 7c)
    //   subf 0,1,2 → 0x7c011050 (bytes 50 10 01 7c)
    void subf(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 40, /*Rc*/ 0));
    }

    // neg — Negate. Power ISA v2.07B §3.3.8, XO-form, opcode 31, XO=104.
    //   Encoding: [op(6)=31 | RT(5) | RA(5) | RB(5)=0 | OE(1)=0 | XO(9)=104 | Rc(1)=0]
    //   Semantics: RT <- -RA (two's complement).
    // Verified 2026-04-25 on POWER9:
    //   neg 3,4 → 0x7c6400d0 (bytes d0 00 64 7c)
    void neg(RegisterID rt, RegisterID ra)
    {
        insn(xoForm(31, rt, ra, PPC64Registers::r0, /*OE*/ 0, /*XO*/ 104, /*Rc*/ 0));
    }

    // addis — Add Immediate Shifted. Power ISA v2.07B §3.3.8, D-form, opcode 15.
    //   Encoding: [op(6)=15 | RT(5) | RA(5) | SI(16)]
    //   Semantics: if RA == 0, RT <- sign_extend(SI) << 16
    //              else         RT <- RA + (sign_extend(SI) << 16)
    //   The lis RT, SI simplified mnemonic is addis RT, 0, SI — loads a
    //   16-bit signed immediate into the high half of RT with low half 0.
    // Verified 2026-04-25 on POWER9:
    //   lis   3,0x1234   → 0x3c601234 (bytes 34 12 60 3c)
    //   lis   5,-1       → 0x3ca0ffff (bytes ff ff a0 3c)
    //   addis 6,7,0x8000 → 0x3cc78000 (bytes 00 80 c7 3c)
    void addis(RegisterID rt, RegisterID ra, int16_t si)
    {
        insn(dForm(15, rt, ra, static_cast<uint16_t>(si)));
    }

    // lis — Load Immediate Shifted. Simplified mnemonic for `addis RT, 0, SI`.
    void lis(RegisterID rt, int16_t si)
    {
        addis(rt, PPC64Registers::r0, si);
    }

    // add — Add. Power ISA v2.07B §3.3.8, XO-form, opcode 31, XO=266.
    //   Encoding: [op(6)=31 | RT(5) | RA(5) | RB(5) | OE(1)=0 | XO(9)=266 | Rc(1)=0]
    //   Semantics: RT <- RA + RB. Does not set CR and does not record overflow.
    //   Sibling mnemonics add. (Rc=1) and addo (OE=1) will be added when needed.
    // Verified 2026-04-24 on POWER9 / GCC as:
    //   add 3,4,5 → 0x7c642a14 (bytes 14 2a 64 7c)
    //   add 0,1,2 → 0x7c011214 (bytes 14 12 01 7c)
    void add(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xoForm(31, rt, ra, rb, /*OE*/ 0, /*XO*/ 266, /*Rc*/ 0));
    }

    // addi — Add Immediate. Power ISA v2.07B §3.3.8, D-form, opcode 14.
    //   Encoding: [op(6)=14 | RT(5) | RA(5) | SI(16)]
    //   Semantics: if RA == 0, RT <- sign_extend(SI); else RT <- RA + sign_extend(SI).
    //   Note: RA=0 is a literal-zero encoding, NOT the value of r0 — see PLAN.md
    //   "Scratch-register discipline" for why r0 is non-allocatable as a base.
    // Verified 2026-04-24 on POWER9 / GCC as:
    //   addi 5,0,100   → 0x38a00064 (bytes 64 00 a0 38, disasm as `li 5,100`)
    //   addi 5,0,-100  → 0x38a0ff9c (bytes 9c ff a0 38, disasm as `li 5,-100`)
    //   addi 3,0,42    → 0x3860002a (bytes 2a 00 60 38, disasm as `li 3,42`)
    void addi(RegisterID rt, RegisterID ra, int16_t si)
    {
        insn(dForm(14, rt, ra, static_cast<uint16_t>(si)));
    }

    // ===================================================================
    // Narrow load/store instructions (D-form). Power ISA v2.07B §3.3.2.
    // These use the full 16-bit signed byte displacement (no DS-form
    // alignment constraint). Zero-extending loads write zeros into the
    // high bits of the 64-bit GPR; lha sign-extends a halfword; lwa
    // (algebraic word) is DS-form and added separately when needed.
    // ===================================================================

    // lwz — Load Word Zero-extended (32-bit). Opcode 32.
    // Verified 2026-04-25:
    //   lwz 3,0(4)    → 0x80640000 (bytes 00 00 64 80)
    //   lwz 3,100(4)  → 0x80640064 (bytes 64 00 64 80)
    //   lwz 3,-4(4)   → 0x8064fffc (bytes fc ff 64 80)
    void lwz(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(32, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    // stw — Store Word (32-bit). Opcode 36.
    // Verified:
    //   stw 3,0(4)  → 0x90640000
    //   stw 5,16(1) → 0x90a10010
    void stw(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(36, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // lbz — Load Byte Zero-extended. Opcode 34.
    // Verified: lbz 3,0(4) → 0x88640000
    void lbz(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(34, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    // stb — Store Byte. Opcode 38.
    // Verified: stb 5,1(1) → 0x98a10001
    void stb(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(38, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // lhz — Load Halfword Zero-extended (16-bit). Opcode 40.
    // Verified: lhz 3,2(4) → 0xa0640002
    void lhz(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(40, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    // lha — Load Halfword Algebraic (sign-extended 16-bit). Opcode 42.
    // Verified: lha 3,4(4) → 0xa8640004
    void lha(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(42, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    // sth — Store Halfword. Opcode 44.
    // Verified: sth 3,6(4) → 0xb0640006
    void sth(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(44, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // ===================================================================
    // Sign-extend (X-form). Power ISA v2.07B §3.3.14. Replicates the
    // sign bit of a narrow source into the high bits of the 64-bit RA.
    // Useful after narrow loads (lbz/lhz) or 32-bit multiplies (which
    // leave the high half of RT undefined).
    //   extsb RA, RS — opcode 31, XO=954 (byte → 64-bit)
    //   extsh RA, RS — opcode 31, XO=922 (halfword → 64-bit)
    //   extsw RA, RS — opcode 31, XO=986 (word → 64-bit)
    // Verified on POWER9:
    //   extsb 3,4 → 0x7c830774 (bytes 74 07 83 7c)
    //   extsh 3,4 → 0x7c830734 (bytes 34 07 83 7c)
    //   extsw 3,4 → 0x7c8307b4 (bytes b4 07 83 7c)
    // ===================================================================
    void extsb(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 954, /*Rc*/ 0));
    }

    void extsh(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 922, /*Rc*/ 0));
    }

    void extsw(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 986, /*Rc*/ 0));
    }

    // ===================================================================
    // Floating-point load/store (D-form with FRT/FRS in the RT/RS slot).
    // Power ISA v2.07B §3.3.3. The bit layout is identical to GPR D-form
    // — 16-bit signed byte displacement in the low half — but the 5-bit
    // slot at bits 6-10 holds an FPR number, interpreted by the opcode.
    //
    //   lfd  FRT, D(RA) — opcode 50 (Load Floating Double)
    //   stfd FRS, D(RA) — opcode 54 (Store Floating Double)
    //   lfs  FRT, D(RA) — opcode 48 (Load Floating Single → converted to double on load)
    //   stfs FRS, D(RA) — opcode 52 (Store Floating Single ← round-to-single on store)
    //
    // Verified on POWER9:
    //   lfd  3,0(4)  → 0xc8640000
    //   stfd 3,0(4)  → 0xd8640000
    //   lfs  3,8(4)  → 0xc0640008
    //   stfs 3,16(4) → 0xd0640010
    //   lfd  31,-8(1)→ 0xcbe1fff8
    // ===================================================================
    void lfd(FPRegisterID frt, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(50, frt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stfd(FPRegisterID frs, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(54, frs, ra, static_cast<uint16_t>(byteOffset)));
    }

    void lfs(FPRegisterID frt, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(48, frt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stfs(FPRegisterID frs, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(52, frs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // ===================================================================
    // Floating-point arithmetic (A-form). Power ISA v2.07B §3.3.4.
    // A-form layout: [op(6) | FRT | FRA | FRB | FRC | XO(5) | Rc]
    // Note the extra 5-bit FRC slot at bits 21-25, which is used by
    // fmul/fmuls (as the second source) and by the 4-operand fused
    // multiply-add family. For fadd/fsub/fdiv it's unused (set to 0).
    //
    // opcode 63 = double-precision, opcode 59 = single-precision.
    // Single-precision ops round to single after computing in double.
    //
    //   fadd  / fadds   XO=21  FRT, FRA, FRB    (FRC unused)
    //   fsub  / fsubs   XO=20  FRT, FRA, FRB    (FRC unused)
    //   fmul  / fmuls   XO=25  FRT, FRA, FRC    (FRB unused)
    //   fdiv  / fdivs   XO=18  FRT, FRA, FRB    (FRC unused)
    //   fmadd           XO=29  FRT, FRA, FRC, FRB  (fused multiply-add)
    //
    // Verified on POWER9:
    //   fadd  f3,f4,f5     → 0xfc64282a
    //   fsub  f3,f4,f5     → 0xfc642828
    //   fmul  f3,f4,f5     → 0xfc640172  (FRB=0, FRC=5)
    //   fdiv  f3,f4,f5     → 0xfc642824
    //   fadds f3,f4,f5     → 0xec64282a
    //   fsubs f3,f4,f5     → 0xec642828
    //   fmuls f3,f4,f5     → 0xec640172
    //   fdivs f3,f4,f5     → 0xec642824
    //   fmadd f3,f4,f5,f6  → 0xfc64317a  (FRA=4, FRB=6, FRC=5)
    // ===================================================================

    void fadd(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(63, frt, fra, frb, PPC64Registers::f0, /*XO*/ 21, /*Rc*/ 0));
    }

    void fsub(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(63, frt, fra, frb, PPC64Registers::f0, /*XO*/ 20, /*Rc*/ 0));
    }

    void fmul(FPRegisterID frt, FPRegisterID fra, FPRegisterID frc)
    {
        insn(aForm(63, frt, fra, PPC64Registers::f0, frc, /*XO*/ 25, /*Rc*/ 0));
    }

    void fdiv(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(63, frt, fra, frb, PPC64Registers::f0, /*XO*/ 18, /*Rc*/ 0));
    }

    void fadds(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(59, frt, fra, frb, PPC64Registers::f0, /*XO*/ 21, /*Rc*/ 0));
    }

    void fsubs(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(59, frt, fra, frb, PPC64Registers::f0, /*XO*/ 20, /*Rc*/ 0));
    }

    void fmuls(FPRegisterID frt, FPRegisterID fra, FPRegisterID frc)
    {
        insn(aForm(59, frt, fra, PPC64Registers::f0, frc, /*XO*/ 25, /*Rc*/ 0));
    }

    void fdivs(FPRegisterID frt, FPRegisterID fra, FPRegisterID frb)
    {
        insn(aForm(59, frt, fra, frb, PPC64Registers::f0, /*XO*/ 18, /*Rc*/ 0));
    }

    // fmadd  FRT, FRA, FRC, FRB  → FRT <- (FRA × FRC) + FRB (fused).
    void fmadd(FPRegisterID frt, FPRegisterID fra, FPRegisterID frc, FPRegisterID frb)
    {
        insn(aForm(63, frt, fra, frb, frc, /*XO*/ 29, /*Rc*/ 0));
    }

    // ===================================================================
    // Floating-point unary ops (X-form, opcode 63). FRA slot is unused
    // (encoded as f0). Power ISA v2.07B §3.3.5.
    //   fneg FRT, FRB — XO=40   (FRT <- -FRB)
    //   fabs FRT, FRB — XO=264  (FRT <- |FRB|)
    //   fmr  FRT, FRB — XO=72   (FRT <- FRB — move)
    // Verified on POWER9:
    //   fneg 3,4 → 0xfc602050
    //   fabs 3,4 → 0xfc602210
    //   fmr  3,4 → 0xfc602090
    // ===================================================================
    void fneg(FPRegisterID frt, FPRegisterID frb)
    {
        insn(xFormFp(63, frt, PPC64Registers::f0, frb, /*XO*/ 40, /*Rc*/ 0));
    }

    void fabs(FPRegisterID frt, FPRegisterID frb)
    {
        insn(xFormFp(63, frt, PPC64Registers::f0, frb, /*XO*/ 264, /*Rc*/ 0));
    }

    void fmr(FPRegisterID frt, FPRegisterID frb)
    {
        insn(xFormFp(63, frt, PPC64Registers::f0, frb, /*XO*/ 72, /*Rc*/ 0));
    }

    // ===================================================================
    // Floating-point compare (X-form variant with 3-bit BF). Power ISA
    // v2.07B §3.3.6.
    //   fcmpu BF, FRA, FRB — opcode 63, XO=0  (Unordered: QNaN → FPCC, no VXSNAN)
    //   fcmpo BF, FRA, FRB — opcode 63, XO=32 (Ordered: signaling on SNaN and QNaN)
    // Both set CR[BF] to reflect LT/GT/EQ/UN of the FP compare.
    // Verified on POWER9:
    //   fcmpu 0,3,4 → 0xfc032000
    //   fcmpu 7,3,4 → 0xff832000
    //   fcmpo 0,3,4 → 0xfc032040
    // ===================================================================
    void fcmpu(uint32_t bf, FPRegisterID fra, FPRegisterID frb)
    {
        insn(fpCmpXForm(63, bf, fra, frb, /*XO*/ 0));
    }

    void fcmpo(uint32_t bf, FPRegisterID fra, FPRegisterID frb)
    {
        insn(fpCmpXForm(63, bf, fra, frb, /*XO*/ 32));
    }

    // ===================================================================
    // Floating-point ↔ integer conversion (X-form, opcode 63, FRA=0).
    // Power ISA v2.07B §3.3.6.
    //
    // Signed:
    //   fcfid   FRT, FRB — XO=846  (Convert From Int Doubleword → double)
    //   fctid   FRT, FRB — XO=814  (Convert To   Int Doubleword,   round per FPSCR)
    //   fctidz  FRT, FRB — XO=815  (  ... truncate toward zero)
    //   fctiw   FRT, FRB — XO=14   (Convert To   Int Word,         round per FPSCR)
    //   fctiwz  FRT, FRB — XO=15   (  ... truncate toward zero)
    // Unsigned (Power ISA v2.06+; all on POWER8):
    //   fcfidu  FRT, FRB — XO=974
    //   fctidu  FRT, FRB — XO=942
    //   fctiduz FRT, FRB — XO=943
    //   fctiwu  FRT, FRB — XO=142
    //   fctiwuz FRT, FRB — XO=143
    //
    // All verified on POWER9 with FRT=3, FRB=4 — see encoding test.
    // ===================================================================
    void fcfid(FPRegisterID frt, FPRegisterID frb)   { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 846, 0)); }
    void fctid(FPRegisterID frt, FPRegisterID frb)   { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 814, 0)); }
    void fctidz(FPRegisterID frt, FPRegisterID frb)  { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 815, 0)); }
    void fctiw(FPRegisterID frt, FPRegisterID frb)   { insn(xFormFp(63, frt, PPC64Registers::f0, frb,  14, 0)); }
    void fctiwz(FPRegisterID frt, FPRegisterID frb)  { insn(xFormFp(63, frt, PPC64Registers::f0, frb,  15, 0)); }
    void fcfidu(FPRegisterID frt, FPRegisterID frb)  { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 974, 0)); }
    void fctidu(FPRegisterID frt, FPRegisterID frb)  { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 942, 0)); }
    void fctiduz(FPRegisterID frt, FPRegisterID frb) { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 943, 0)); }
    void fctiwu(FPRegisterID frt, FPRegisterID frb)  { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 142, 0)); }
    void fctiwuz(FPRegisterID frt, FPRegisterID frb) { insn(xFormFp(63, frt, PPC64Registers::f0, frb, 143, 0)); }

    // ===================================================================
    // Indexed load/store instructions (X-form). Power ISA v2.07B §3.3.2.
    // Effective address is (RA == 0 ? 0 : RA) + RB — the offset comes
    // from a register (RB) rather than an immediate. Opcode 31 with
    // distinct XO values per operation.
    // ===================================================================

    // ldx   RT, RA, RB — opcode 31, XO=21  (Load Doubleword Indexed).
    // Verified: ldx 3,4,5 → 0x7c64282a (bytes 2a 28 64 7c)
    void ldx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 21, /*Rc*/ 0));
    }

    // stdx  RS, RA, RB — opcode 31, XO=149 (Store Doubleword Indexed).
    // Verified: stdx 3,4,5 → 0x7c64292a
    void stdx(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 149, /*Rc*/ 0));
    }

    // lwzx / stwx — 32-bit word indexed (XO=23 / XO=151).
    // Verified: lwzx 3,4,5 → 0x7c64282e, stwx 3,4,5 → 0x7c64292e
    void lwzx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 23, /*Rc*/ 0));
    }

    void stwx(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 151, /*Rc*/ 0));
    }

    // lbzx / stbx — byte indexed (XO=87 / XO=215).
    // Verified: lbzx 3,4,5 → 0x7c6428ae, stbx 3,4,5 → 0x7c6429ae
    void lbzx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 87, /*Rc*/ 0));
    }

    void stbx(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 215, /*Rc*/ 0));
    }

    // lhzx / lhax / sthx — halfword indexed (XO=279 zero-ext / XO=343
    // sign-ext / XO=407 store).
    // Verified: lhzx → 0x7c642a2e, lhax → 0x7c642aae, sthx → 0x7c642b2e
    void lhzx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 279, /*Rc*/ 0));
    }

    void lhax(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 343, /*Rc*/ 0));
    }

    void sthx(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 407, /*Rc*/ 0));
    }

    // ===================================================================
    // Load/store with update (auto-update RA = effective address). Power
    // ISA v2.07B §3.3.2. Useful for stack push/pop and for stepping
    // through arrays without a separate addi after every access.
    //
    // Pattern: opcode = base_opcode + 1 for D-form variants. DS-form uses
    // the same opcode as ld/std but sets XO=1 instead of 0. Indexed
    // variants use base XO + 32 (X-form, opcode 31).
    //
    // The semantics: load/store completes, AND THEN RA <- effective_addr.
    // Trap if RA == 0 or RA == RT (it's a programmer error to alias).
    // ===================================================================

    // DS-form update: opcode 58/62, XO=1.
    //   ldu  RT, DS(RA), stdu RS, DS(RA)
    // Verified:
    //   ldu  3,8(4)   → 0xe8640009
    //   stdu 3,-8(1)  → 0xf861fff9
    void ldu(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dsForm(58, rt, ra, byteOffset, /*XO*/ 1));
    }

    void stdu(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dsForm(62, rs, ra, byteOffset, /*XO*/ 1));
    }

    // D-form update (opcode = base + 1):
    //   lwzu  RT, D(RA) — opcode 33   stwu  RS, D(RA) — opcode 37
    //   lbzu  RT, D(RA) — opcode 35   stbu  RS, D(RA) — opcode 39
    //   lhzu  RT, D(RA) — opcode 41   lhau  RT, D(RA) — opcode 43
    //   sthu  RS, D(RA) — opcode 45
    // Verified on POWER9 — see encoding test.
    void lwzu(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(33, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stwu(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(37, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    void lbzu(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(35, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stbu(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(39, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    void lhzu(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(41, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void lhau(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(43, rt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void sthu(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dForm(45, rs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // X-form indexed update (opcode 31, XO = base + 32).
    //   ldux  RT, RA, RB — XO=53     stdux RS, RA, RB — XO=181
    //   lwzux RT, RA, RB — XO=55     stwux RS, RA, RB — XO=183
    //   lbzux RT, RA, RB — XO=119    stbux RS, RA, RB — XO=247
    //   lhzux RT, RA, RB — XO=311    lhaux RT, RA, RB — XO=375
    //   sthux RS, RA, RB — XO=439
    // Verified on POWER9 — see encoding test.
    void ldux(RegisterID rt, RegisterID ra, RegisterID rb)   { insn(xForm(31, rt, ra, rb,  53, 0)); }
    void stdux(RegisterID rs, RegisterID ra, RegisterID rb)  { insn(xForm(31, rs, ra, rb, 181, 0)); }
    void lwzux(RegisterID rt, RegisterID ra, RegisterID rb)  { insn(xForm(31, rt, ra, rb,  55, 0)); }
    void stwux(RegisterID rs, RegisterID ra, RegisterID rb)  { insn(xForm(31, rs, ra, rb, 183, 0)); }
    void lbzux(RegisterID rt, RegisterID ra, RegisterID rb)  { insn(xForm(31, rt, ra, rb, 119, 0)); }
    void stbux(RegisterID rs, RegisterID ra, RegisterID rb)  { insn(xForm(31, rs, ra, rb, 247, 0)); }
    void lhzux(RegisterID rt, RegisterID ra, RegisterID rb)  { insn(xForm(31, rt, ra, rb, 311, 0)); }
    void lhaux(RegisterID rt, RegisterID ra, RegisterID rb)  { insn(xForm(31, rt, ra, rb, 375, 0)); }
    void sthux(RegisterID rs, RegisterID ra, RegisterID rb)  { insn(xForm(31, rs, ra, rb, 439, 0)); }

    // ===================================================================
    // FP indexed load/store (X-form) and FP update variants. Power ISA
    // v2.07B §3.3.3. Indexed = effective addr from RA + RB; update = also
    // writes effective addr back to RA after the access.
    //
    // Indexed (X-form, opcode 31):
    //   lfdx FRT, RA, RB — XO=599    stfdx FRS, RA, RB — XO=727
    //   lfsx FRT, RA, RB — XO=535    stfsx FRS, RA, RB — XO=663
    //
    // Indexed update (XO = indexed + 32):
    //   lfdux  FRT, RA, RB — XO=631  stfdux FRS, RA, RB — XO=759
    //   lfsux  FRT, RA, RB — XO=567  stfsux FRS, RA, RB — XO=695
    //
    // D-form FP update (opcode = base FP + 1):
    //   lfdu  FRT, D(RA) — opcode 51    stfdu FRS, D(RA) — opcode 55
    //   lfsu  FRT, D(RA) — opcode 49    stfsu FRS, D(RA) — opcode 53
    // ===================================================================
    void lfdx(FPRegisterID frt, RegisterID ra, RegisterID rb)   { insn(xFormFpMem(31, frt, ra, rb, 599)); }
    void stfdx(FPRegisterID frs, RegisterID ra, RegisterID rb)  { insn(xFormFpMem(31, frs, ra, rb, 727)); }
    void lfsx(FPRegisterID frt, RegisterID ra, RegisterID rb)   { insn(xFormFpMem(31, frt, ra, rb, 535)); }
    void stfsx(FPRegisterID frs, RegisterID ra, RegisterID rb)  { insn(xFormFpMem(31, frs, ra, rb, 663)); }
    void lfdux(FPRegisterID frt, RegisterID ra, RegisterID rb)  { insn(xFormFpMem(31, frt, ra, rb, 631)); }
    void stfdux(FPRegisterID frs, RegisterID ra, RegisterID rb) { insn(xFormFpMem(31, frs, ra, rb, 759)); }
    void lfsux(FPRegisterID frt, RegisterID ra, RegisterID rb)  { insn(xFormFpMem(31, frt, ra, rb, 567)); }
    void stfsux(FPRegisterID frs, RegisterID ra, RegisterID rb) { insn(xFormFpMem(31, frs, ra, rb, 695)); }

    void lfdu(FPRegisterID frt, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(51, frt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stfdu(FPRegisterID frs, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(55, frs, ra, static_cast<uint16_t>(byteOffset)));
    }

    void lfsu(FPRegisterID frt, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(49, frt, ra, static_cast<uint16_t>(byteOffset)));
    }

    void stfsu(FPRegisterID frs, int16_t byteOffset, RegisterID ra)
    {
        insn(dFormFp(53, frs, ra, static_cast<uint16_t>(byteOffset)));
    }

    // ld — Load Doubleword. Power ISA v2.07B §3.3.2, DS-form, opcode 58, XO=0.
    //   Encoding: [op(6)=58 | RT(5) | RA(5) | DS(14) | XO(2)=0]
    //   Semantics: RT <- MEM(sign_extend(DS || 0b00) + (RA==0 ? 0 : RA), 8)
    //   Byte offset must be a multiple of 4 (the low 2 bits become XO).
    // Verified 2026-04-24 on POWER9:
    //   ld 3,0(4)    → 0xe8640000 (bytes 00 00 64 e8)
    //   ld 3,8(4)    → 0xe8640008 (bytes 08 00 64 e8)
    //   ld 3,-16(4)  → 0xe864fff0 (bytes f0 ff 64 e8)
    //   ld 3,0(0)    → 0xe8600000 (bytes 00 00 60 e8) — RA=0 literal-zero form
    void ld(RegisterID rt, int16_t byteOffset, RegisterID ra)
    {
        insn(dsForm(58, rt, ra, byteOffset, /*XO*/ 0));
    }

    // std — Store Doubleword. Power ISA v2.07B §3.3.2, DS-form, opcode 62, XO=0.
    //   Encoding: [op(6)=62 | RS(5) | RA(5) | DS(14) | XO(2)=0]
    //   Semantics: MEM(sign_extend(DS || 0b00) + (RA==0 ? 0 : RA), 8) <- RS
    // Verified 2026-04-24 on POWER9:
    //   std 3,0(4)   → 0xf8640000 (bytes 00 00 64 f8)
    //   std 5,16(1)  → 0xf8a10010 (bytes 10 00 a1 f8)
    //   std 3,-24(1) → 0xf861ffe8 (bytes e8 ff 61 f8)
    void std(RegisterID rs, int16_t byteOffset, RegisterID ra)
    {
        insn(dsForm(62, rs, ra, byteOffset, /*XO*/ 0));
    }

    // Special-Purpose Register numbers used by mfspr/mtspr (Power ISA v2.07B
    // Book III §4.4.4 / Book I Appendix E). Only the SPRs we actually emit are
    // listed; more will be added as needed.
    static constexpr uint32_t SPR_XER = 1;
    static constexpr uint32_t SPR_LR  = 8;
    static constexpr uint32_t SPR_CTR = 9;

    // mfspr — Move From Special-Purpose Register. Power ISA v2.07B §3.3.15,
    //   XFX-form, opcode 31, XO=339.
    //   Encoding: [op(6)=31 | RT(5) | spr(10) | XO(10)=339 | 0(1)]
    //   The 10-bit spr field stores the SPR number with its two 5-bit halves
    //   swapped: sprField = (spr_num[0:4] << 5) | spr_num[5:9]. For SPR#8 (LR)
    //   this gives sprField = 0x100 (= 256); for SPR#9 (CTR), sprField = 0x120
    //   (= 288).
    // Verified 2026-04-24 on POWER9:
    //   mflr  3 → mfspr 3,8 → 0x7c6802a6 (bytes a6 02 68 7c)
    //   mfctr 3 → mfspr 3,9 → 0x7c6902a6 (bytes a6 02 69 7c)
    void mfspr(RegisterID rt, uint32_t sprNum)
    {
        insn(xfxForm(31, rt, sprNum, /*XO*/ 339));
    }

    // mtspr — Move To Special-Purpose Register. Power ISA v2.07B §3.3.15,
    //   XFX-form, opcode 31, XO=467. Same spr-field encoding as mfspr.
    // Verified 2026-04-24 on POWER9:
    //   mtlr  3 → mtspr 8,3 → 0x7c6803a6 (bytes a6 03 68 7c)
    //   mtctr 3 → mtspr 9,3 → 0x7c6903a6 (bytes a6 03 69 7c)
    void mtspr(uint32_t sprNum, RegisterID rs)
    {
        insn(xfxForm(31, rs, sprNum, /*XO*/ 467));
    }

    // Convenience wrappers for the SPRs JSC uses most.
    void mflr(RegisterID rt)  { mfspr(rt, SPR_LR); }
    void mtlr(RegisterID rs)  { mtspr(SPR_LR, rs); }
    void mfctr(RegisterID rt) { mfspr(rt, SPR_CTR); }
    void mtctr(RegisterID rs) { mtspr(SPR_CTR, rs); }

    // ===================================================================
    // Logical instructions (X-form). Power ISA v2.07B §3.3.9. Note: the
    // assembler mnemonics `and`, `or`, `xor` are C++ alternative-operator
    // tokens (reserved keywords), so our methods use trailing underscores.
    // In Power ISA X-form notation the 5-bit slot at bits 6-10 is the
    // source register (RS), and bits 11-15 hold the destination (RA) —
    // the reverse of arithmetic XO-form where bits 6-10 are RT (dest).
    // ===================================================================

    // and_ — AND. Opcode 31, XO=28, Rc=0.
    //   Encoding: [op=31 | RS(5) | RA(5) | RB(5) | XO=28 | Rc=0]
    //   Semantics: RA <- RS & RB.
    // Verified 2026-04-25: and 3,4,5 → 0x7c832838 (bytes 38 28 83 7c)
    void and_(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 28, /*Rc*/ 0));
    }

    // or_ — OR. Opcode 31, XO=444, Rc=0.
    // Verified: or 3,4,5 → 0x7c832b78 (bytes 78 2b 83 7c)
    void or_(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 444, /*Rc*/ 0));
    }

    // xor_ — XOR. Opcode 31, XO=316, Rc=0.
    // Verified: xor 3,4,5 → 0x7c832a78 (bytes 78 2a 83 7c)
    void xor_(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 316, /*Rc*/ 0));
    }

    // mr — Move Register. Simplified mnemonic for `or_ RA, RS, RS`.
    //   Semantics: RA <- RS.
    // Verified: mr 7,8 → 0x7d074378 (bytes 78 43 07 7d)
    void mr(RegisterID ra, RegisterID rs)
    {
        or_(ra, rs, rs);
    }

    // ===================================================================
    // Shift-by-register (X-form). Power ISA v2.07B §3.3.11. The shift
    // count comes from the low 7 bits of RB; counts ≥ XLEN produce 0
    // (logical shifts) or the sign bit (sraw / srad). This differs from
    // x86's mask-to-{5,6}-bits semantics — callers that expect x86-like
    // behavior must mask RB first.
    //
    //   slw  RA, RS, RB — opcode 31, XO=24  (32-bit left)
    //   srw  RA, RS, RB — opcode 31, XO=536 (32-bit right, logical)
    //   sraw RA, RS, RB — opcode 31, XO=792 (32-bit right, arithmetic)
    //   sld  RA, RS, RB — opcode 31, XO=27  (64-bit left)
    //   srd  RA, RS, RB — opcode 31, XO=539 (64-bit right, logical)
    //   srad RA, RS, RB — opcode 31, XO=794 (64-bit right, arithmetic)
    //
    // Verified on POWER9:
    //   slw 3,4,5  → 0x7c832830 (bytes 30 28 83 7c)
    //   srw 3,4,5  → 0x7c832c30 (bytes 30 2c 83 7c)
    //   sraw 3,4,5 → 0x7c832e30 (bytes 30 2e 83 7c)
    //   sld 3,4,5  → 0x7c832836 (bytes 36 28 83 7c)
    //   srd 3,4,5  → 0x7c832c36 (bytes 36 2c 83 7c)
    //   srad 3,4,5 → 0x7c832e34 (bytes 34 2e 83 7c)
    // ===================================================================
    void slw(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 24, /*Rc*/ 0));
    }

    void srw(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 536, /*Rc*/ 0));
    }

    void sraw(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 792, /*Rc*/ 0));
    }

    void sld(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 27, /*Rc*/ 0));
    }

    void srd(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 539, /*Rc*/ 0));
    }

    void srad(RegisterID ra, RegisterID rs, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 794, /*Rc*/ 0));
    }

    // ===================================================================
    // Rotate-and-mask (MD-form). Power ISA v2.07B §3.3.13. Four fused
    // ops that rotate a 64-bit RS left by an immediate amount and then
    // mask the result — the workhorses for 64-bit immediate shifts,
    // field extraction, and field insertion.
    //
    //   rldicl RA, RS, SH, MB — opcode 30, XO=0 (rotate then clear left)
    //   rldicr RA, RS, SH, ME — opcode 30, XO=1 (rotate then clear right)
    //   rldic  RA, RS, SH, MB — opcode 30, XO=2 (rotate, clear left, clear bits outside SH)
    //   rldimi RA, RS, SH, MB — opcode 30, XO=3 (rotate then mask-insert)
    //
    // Simplified mnemonics callers will actually use:
    //   sldi  RA, RS, n = rldicr RA, RS, n,    63-n   (shift left  doubleword imm)
    //   srdi  RA, RS, n = rldicl RA, RS, 64-n, n      (shift right doubleword imm, logical)
    //   clrldi RA, RS, n = rldicl RA, RS, 0,   n      (clear high n bits)
    //   clrrdi RA, RS, n = rldicr RA, RS, 0,   63-n   (clear low  n bits)
    //
    // Verified on POWER9 (see ppc64_encoding_test.cpp for exact hex).
    // ===================================================================

    void rldicl(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t mb)
    {
        insn(mdForm(30, rs, ra, sh, mb, /*XO*/ 0, /*Rc*/ 0));
    }

    void rldicr(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t me)
    {
        insn(mdForm(30, rs, ra, sh, me, /*XO*/ 1, /*Rc*/ 0));
    }

    void rldic(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t mb)
    {
        insn(mdForm(30, rs, ra, sh, mb, /*XO*/ 2, /*Rc*/ 0));
    }

    void rldimi(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t mb)
    {
        insn(mdForm(30, rs, ra, sh, mb, /*XO*/ 3, /*Rc*/ 0));
    }

    // Simplified mnemonics.
    void sldi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 64);
        rldicr(ra, rs, n, 63 - n);
    }

    void srdi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 64);
        rldicl(ra, rs, (64 - n) % 64, n);
    }

    void clrldi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 64);
        rldicl(ra, rs, 0, n);
    }

    void clrrdi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 64);
        rldicr(ra, rs, 0, 63 - n);
    }

    // ===================================================================
    // 32-bit rotate-and-mask (M-form). Power ISA v2.07B §3.3.13.
    //
    //   rlwinm RA, RS, SH, MB, ME — opcode 21 (rotate left imm then AND mask)
    //   rlwimi RA, RS, SH, MB, ME — opcode 20 (rotate left imm then mask-insert)
    //   rlwnm  RA, RS, RB, MB, ME — opcode 23 (rotate left by RB then AND mask)
    //
    // MB/ME are plain 5-bit fields at bits 21-25 and 26-30 — NO
    // bit-swap encoding (unlike MD-form's 6-bit field). SH is a plain
    // 5-bit immediate in rlwinm/rlwimi (range 0-31).
    //
    // Simplified mnemonics that JSC's MacroAssembler will actually use:
    //   slwi   RA, RS, n = rlwinm RA, RS, n,     0,     31-n
    //   srwi   RA, RS, n = rlwinm RA, RS, 32-n,  n,     31
    //   clrlwi RA, RS, n = rlwinm RA, RS, 0,     n,     31
    //   rotlwi RA, RS, n = rlwinm RA, RS, n,     0,     31
    //
    // Verified on POWER9 (see test; all self-assert + disassemble to the
    // simplified mnemonic GNU as recognizes).
    // ===================================================================
    void rlwinm(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t mb, uint32_t me)
    {
        insn(mFormImm(21, rs, ra, sh, mb, me, /*Rc*/ 0));
    }

    void rlwimi(RegisterID ra, RegisterID rs, uint32_t sh, uint32_t mb, uint32_t me)
    {
        insn(mFormImm(20, rs, ra, sh, mb, me, /*Rc*/ 0));
    }

    void rlwnm(RegisterID ra, RegisterID rs, RegisterID rb, uint32_t mb, uint32_t me)
    {
        insn(mFormReg(23, rs, ra, rb, mb, me, /*Rc*/ 0));
    }

    // 32-bit simplified mnemonics.
    void slwi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 32);
        rlwinm(ra, rs, n, 0, 31 - n);
    }

    void srwi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 32);
        rlwinm(ra, rs, (32 - n) % 32, n, 31);
    }

    void clrlwi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 32);
        rlwinm(ra, rs, 0, n, 31);
    }

    void rotlwi(RegisterID ra, RegisterID rs, uint32_t n)
    {
        ASSERT(n < 32);
        rlwinm(ra, rs, n, 0, 31);
    }

    // ===================================================================
    // Shift-right-algebraic immediate (signed arithmetic right shift).
    // Power ISA v2.07B §3.3.11. Sets XER[CA] based on the shifted-out
    // bits — MacroAssembler::rshift32/rshift64 will typically use these
    // and then check the carry when it cares about the sign bit.
    //
    //   srawi RA, RS, SH — X-form,  opcode 31, XO=824, SH in RB slot (imm)
    //                      SH is 5 bits (0-31).
    //   sradi RA, RS, SH — XS-form, opcode 31, XO=413
    //                      SH is 6 bits (0-63); split into low-5 at bits
    //                      16-20 and high-1 at bit 30.
    //
    // Verified on POWER9:
    //   srawi 3,4,8  → 0x7c834670
    //   srawi 3,4,31 → 0x7c83fe70
    //   sradi 3,4,8  → 0x7c834674
    //   sradi 3,4,32 → 0x7c830676
    //   sradi 3,4,63 → 0x7c83fe76
    // ===================================================================
    void srawi(RegisterID ra, RegisterID rs, uint32_t sh)
    {
        ASSERT(sh < 32);
        // X-form with SH in the RB slot as an immediate.
        insn((31u << 26)
           | (registerValue(rs) << 21)
           | (registerValue(ra) << 16)
           | (sh << 11)
           | (824u << 1));
    }

    void sradi(RegisterID ra, RegisterID rs, uint32_t sh)
    {
        ASSERT(sh < 64);
        insn(xsForm(31, rs, ra, sh, /*XO*/ 413, /*Rc*/ 0));
    }

    // ===================================================================
    // Count-leading-zeros and popcount (X-form, opcode 31, RB slot unused).
    // Power ISA v2.07B §3.3.14. POWER8 supports all of these natively.
    //
    //   cntlzw  RA, RS — XO=26   (count leading zeros,  32-bit)
    //   cntlzd  RA, RS — XO=58   (count leading zeros,  64-bit)
    //   popcntb RA, RS — XO=122  (popcount per byte, v2.05+)
    //   popcntw RA, RS — XO=378  (popcount per word, v2.06+)
    //   popcntd RA, RS — XO=506  (popcount of full doubleword, v2.06+)
    //
    // Verified on POWER9:
    //   cntlzw  3,4 → 0x7c830034
    //   cntlzd  3,4 → 0x7c830074
    //   popcntw 3,4 → 0x7c8302f4
    //   popcntd 3,4 → 0x7c8303f4
    //   popcntb 3,4 → 0x7c8300f4
    // ===================================================================
    void cntlzw(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 26, /*Rc*/ 0));
    }

    void cntlzd(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 58, /*Rc*/ 0));
    }

    void popcntw(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 378, /*Rc*/ 0));
    }

    void popcntd(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 506, /*Rc*/ 0));
    }

    void popcntb(RegisterID ra, RegisterID rs)
    {
        insn(xForm(31, rs, ra, PPC64Registers::r0, /*XO*/ 122, /*Rc*/ 0));
    }

    // subfic RT, RA, SI — D-form, opcode 8 (Subtract From Immediate Carrying).
    //   RT <- sign_extend(SI) - RA. Sets XER[CA] based on the result.
    //   Useful for "immediate - register" patterns (addi can only do
    //   "register + immediate") and for negating via subfic RT, RA, 0.
    // Verified on POWER9:
    //   subfic 3,4,100 → 0x20640064
    //   subfic 3,4,-1  → 0x2064ffff
    void subfic(RegisterID rt, RegisterID ra, int16_t si)
    {
        insn(dForm(8, rt, ra, static_cast<uint16_t>(si)));
    }

    // ===================================================================
    // Compare instructions. Power ISA v2.07B §3.3.10. The X-form and
    // D-form compare encodings diverge from the normal X/D shapes: the
    // 5-bit slot at bits 6-10 is split into BF(3) at 6-8, a reserved
    // bit at 9, and L(1) at 10. L=0 for 32-bit (cmpw/cmpwi), L=1 for
    // 64-bit (cmpd/cmpdi). cmp/cmpl share opcode 31 (XO=0 / XO=32);
    // cmpi/cmpli use opcodes 11 / 10 in D-form.
    // ===================================================================

    // cmpd — Compare signed 64-bit. Opcode 31, XO=0, L=1.
    //   Sets CR field BF from the signed comparison (RA ? RB).
    // Verified 2026-04-25 on POWER9:
    //   cmpd 0,3,4  → 0x7c232000 (bytes 00 20 23 7c)
    //   cmpd 7,3,4  → 0x7fa32000 (bytes 00 20 a3 7f)
    void cmpd(uint32_t bf, RegisterID ra, RegisterID rb)
    {
        insn(cmpXForm(31, bf, /*L*/ 1, ra, rb, /*XO*/ 0));
    }

    // cmpw — Compare signed 32-bit. Opcode 31, XO=0, L=0.
    // Verified: cmpw 0,3,4 → 0x7c032000 (bytes 00 20 03 7c).
    void cmpw(uint32_t bf, RegisterID ra, RegisterID rb)
    {
        insn(cmpXForm(31, bf, /*L*/ 0, ra, rb, /*XO*/ 0));
    }

    // cmpld — Compare logical (unsigned) 64-bit. Opcode 31, XO=32, L=1.
    // Verified: cmpld 0,3,4 → 0x7c232040 (bytes 40 20 23 7c).
    void cmpld(uint32_t bf, RegisterID ra, RegisterID rb)
    {
        insn(cmpXForm(31, bf, /*L*/ 1, ra, rb, /*XO*/ 32));
    }

    // cmplw — Compare logical (unsigned) 32-bit. Opcode 31, XO=32, L=0.
    void cmplw(uint32_t bf, RegisterID ra, RegisterID rb)
    {
        insn(cmpXForm(31, bf, /*L*/ 0, ra, rb, /*XO*/ 32));
    }

    // cmpdi — Compare signed 64-bit immediate. Opcode 11, L=1.
    //   SI is a 16-bit signed immediate.
    // Verified 2026-04-25 on POWER9:
    //   cmpdi 0,3,0    → 0x2c230000 (bytes 00 00 23 2c)
    //   cmpdi 0,3,-1   → 0x2c23ffff (bytes ff ff 23 2c)
    //   cmpdi 7,3,100  → 0x2fa30064 (bytes 64 00 a3 2f)
    void cmpdi(uint32_t bf, RegisterID ra, int16_t si)
    {
        insn(cmpDForm(11, bf, /*L*/ 1, ra, static_cast<uint16_t>(si)));
    }

    // cmpwi — Compare signed 32-bit immediate. Opcode 11, L=0.
    // Verified: cmpwi 0,3,42 → 0x2c03002a (bytes 2a 00 03 2c).
    void cmpwi(uint32_t bf, RegisterID ra, int16_t si)
    {
        insn(cmpDForm(11, bf, /*L*/ 0, ra, static_cast<uint16_t>(si)));
    }

    // cmpldi — Compare logical (unsigned) 64-bit immediate. Opcode 10, L=1.
    //   UI is a 16-bit unsigned immediate (zero-extended for the compare).
    // Verified: cmpldi 0,3,100 → 0x28230064 (bytes 64 00 23 28).
    void cmpldi(uint32_t bf, RegisterID ra, uint16_t ui)
    {
        insn(cmpDForm(10, bf, /*L*/ 1, ra, ui));
    }

    // cmplwi — Compare logical (unsigned) 32-bit immediate. Opcode 10, L=0.
    void cmplwi(uint32_t bf, RegisterID ra, uint16_t ui)
    {
        insn(cmpDForm(10, bf, /*L*/ 0, ra, ui));
    }

    // Branch BO-field encodings (Power ISA v2.07B §3.3.6 Table 11). Only the
    // hint-0 "branch-unconditional" and "branch-if-condition-{true,false}"
    // values JSC needs right now.
    static constexpr uint32_t BO_ALWAYS        = 20; // 0b10100 — "branch always"
    static constexpr uint32_t BO_IF_TRUE       = 12; // 0b01100 — "branch if CR[BI]=1"
    static constexpr uint32_t BO_IF_FALSE      =  4; // 0b00100 — "branch if CR[BI]=0"

    // Bits within a CR field (§3.3.10): LT, GT, EQ, SO. Callers pass
    // BI = 4*crField + {LT,GT,EQ,SO} to address a specific CR field bit.
    static constexpr uint32_t CR_LT = 0;
    static constexpr uint32_t CR_GT = 1;
    static constexpr uint32_t CR_EQ = 2;
    static constexpr uint32_t CR_SO = 3;

    // b — Branch. Power ISA v2.07B §3.3.6, I-form, opcode 18.
    //   Encoding: [op(6)=18 | LI(24) | AA(1) | LK(1)]
    //   Semantics: NIA ← (AA ? sign_extend(LI||0b00) : CIA + sign_extend(LI||0b00))
    //   LK=1 records return address into LR ("bl"). Byte offset range is
    //   ±32MB (24-bit signed word offset).
    // Verified 2026-04-24 on POWER9:
    //   `b .` (self)        → 0x48000000
    //   `b .+4` (next)      → 0x48000004
    //   `bl <-8 bytes back>`→ 0x4bfffff9
    void b(int32_t byteOffset)
    {
        insn(iForm(18, byteOffset, /*AA*/ 0, /*LK*/ 0));
    }

    void bl(int32_t byteOffset)
    {
        insn(iForm(18, byteOffset, /*AA*/ 0, /*LK*/ 1));
    }

    // bc — Branch Conditional. Power ISA v2.07B §3.3.6, B-form, opcode 16.
    //   Encoding: [op(6)=16 | BO(5) | BI(5) | BD(14) | AA(1) | LK(1)]
    //   BO selects the branch-operation (decrement/no-decrement × cond);
    //   BI selects the CR bit to test. Byte offset range is ±32KB
    //   (14-bit signed word offset).
    // Verified 2026-04-24 on POWER9:
    //   `beq .-12` (bc 12, 2, -12) → 0x4182fff4
    void bc(uint32_t bo, uint32_t bi, int32_t byteOffset)
    {
        insn(bForm(16, bo, bi, byteOffset, /*AA*/ 0, /*LK*/ 0));
    }

    // bclr / bcctr — Branch Conditional to LR / CTR. Power ISA v2.07B §3.3.6,
    //   XL-form, opcode 19, XO=16 (bclr) or 528 (bcctr).
    //   Encoding: [op(6)=19 | BO(5) | BI(5) | BH(3) | //(2) | XO(10) | LK(1)]
    //   BH is a branch-prediction hint (usually 0). LK=1 makes it a subroutine
    //   call (blrl / bcctrl) that also writes the return address to LR.
    // Verified 2026-04-24 on POWER9:
    //   blr   → bclr 20,0,0    → 0x4e800020
    //   bctr  → bcctr 20,0,0   → 0x4e800420
    //   bctrl → bcctr 20,0,0,1 → 0x4e800421
    //   beqlr → bclr 12,2,0    → 0x4d820020
    //   bnelr → bclr  4,2,0    → 0x4c820020
    void bclr(uint32_t bo, uint32_t bi, uint32_t bh = 0, uint32_t lk = 0)
    {
        insn(xlForm(19, bo, bi, bh, /*XO*/ 16, lk));
    }

    void bcctr(uint32_t bo, uint32_t bi, uint32_t bh = 0, uint32_t lk = 0)
    {
        insn(xlForm(19, bo, bi, bh, /*XO*/ 528, lk));
    }

    // Convenience wrappers: unconditional indirect branches and return-from-call.
    void blr()   { bclr(BO_ALWAYS, 0, 0, 0); }
    void blrl()  { bclr(BO_ALWAYS, 0, 0, 1); }
    void bctr()  { bcctr(BO_ALWAYS, 0, 0, 0); }
    void bctrl() { bcctr(BO_ALWAYS, 0, 0, 1); }

    // nop — Power ISA v2.07B §3.3.1.1 defines the preferred nop as
    //   `ori 0, 0, 0` → 0x60000000. Verified: `echo "nop" | as` emits
    //   the same bytes (00 00 00 60).
    void nop()
    {
        ori(PPC64Registers::r0, PPC64Registers::r0, 0);
    }

    // tw — Trap Word (32-bit compare trap). Power ISA v2.07B §3.3.12,
    //   X-form, opcode 31, XO=4.
    //   Encoding: [op=31 | TO(5) | RA(5) | RB(5) | XO=4 | /(1)]
    //   TO is a 5-bit mask selecting which compare conditions trap
    //   (LT=16, GT=8, EQ=4, LGT=2, LLT=1; OR them; 31 = unconditional).
    // Verified 2026-04-25: tw 31,0,0 → 0x7fe00008 (bytes 08 00 e0 7f).
    void tw(uint32_t to, RegisterID ra, RegisterID rb)
    {
        ASSERT(to < 32);
        // xForm's rtOrRs slot is bits 6-10, same slot used by TO here.
        // We can't pass `to` directly because xForm takes RegisterID;
        // rather than adding a new helper for a single instruction,
        // assemble the encoding inline.
        insn((31 << 26)
            | (to << 21)
            | (registerValue(ra) << 16)
            | (registerValue(rb) << 11)
            | (4 << 1));
    }

    // trap — Simplified mnemonic for `tw 31, 0, 0`: always-trap (0x7fe00008).
    //   This is the canonical PPC breakpoint / debugger trap; the kernel
    //   delivers SIGTRAP to the process when this instruction executes.
    void trap()
    {
        tw(31, PPC64Registers::r0, PPC64Registers::r0);
    }

    // ===================================================================
    // Memory barriers. Power ISA v2.07B §3.3.1.3.
    //
    //   sync L   — opcode 31, XO=598. L at Power bits 9-10 (shift 21).
    //              L=0 heavyweight (sync/hwsync): full orderings of
    //                   memory accesses and I/O.
    //              L=1 lightweight (lwsync): orders loads/stores but
    //                   not I/O; cheaper; acquire/release barrier.
    //              L=2 page-table entry (ptesync).
    //   isync    — opcode 19, XO=150, XL-form. Instruction sync; flushes
    //              the prefetch pipeline and re-fetches after any
    //              preceding store that could modify subsequent code.
    //   eieio    — opcode 31, XO=854. Enforce I/O ordering (for MMIO).
    //
    // JSC's writeBarrier and concurrent-JIT paths will primarily use
    // lwsync for acquire/release and sync (L=0) only for seq-cst.
    //
    // Verified on POWER9:
    //   sync 0 (hwsync) → 0x7c0004ac
    //   sync 1 (lwsync) → 0x7c2004ac
    //   sync 2 (ptesync)→ 0x7c4004ac
    //   isync           → 0x4c00012c
    //   eieio           → 0x7c0006ac
    // ===================================================================
    void sync(uint32_t l = 0)
    {
        ASSERT(l < 4);
        insn((31u << 26) | (l << 21) | (598u << 1));
    }

    void lwsync() { sync(1); }
    void hwsync() { sync(0); }
    void ptesync() { sync(2); }

    void isync()
    {
        // XL-form, opcode 19, XO=150, all register slots 0, LK=0.
        insn((19u << 26) | (150u << 1));
    }

    void eieio()
    {
        insn((31u << 26) | (854u << 1));
    }

    // ===================================================================
    // Cache block management (X-form, opcode 31, RT/RS slot = 0).
    // Power ISA v2.07B §3.3.1.1. The core icache-flush primitives; JSC
    // needs these after emitting self-modifying code, including every
    // linkJump / linkCall patch.
    //
    //   dcbst RA, RB — XO=54    (Data Cache Block Store — write back
    //                            dirty line to memory; doesn't invalidate)
    //   dcbf  RA, RB — XO=86    (Data Cache Block Flush — write back
    //                            and invalidate)
    //   icbi  RA, RB — XO=982   (Instruction Cache Block Invalidate)
    //   dcbz  RA, RB — XO=1014  (Data Cache Block set to Zero)
    //
    // Effective address is (RA==0 ? 0 : RA) + RB.
    //
    // Standard JIT icache-flush sequence after code patching:
    //     dcbst 0, addr      ; flush dcache line to memory
    //     sync               ; barrier
    //     icbi  0, addr      ; invalidate icache line
    //     isync              ; flush prefetch pipeline
    // GCC's __builtin___clear_cache expands to this on PPC — we emit
    // the same sequence by hand from our patching paths.
    //
    // Verified on POWER9:
    //   dcbst 0,3 → 0x7c00186c    dcbf 0,3 → 0x7c0018ac
    //   icbi  0,3 → 0x7c001fac    dcbz 0,3 → 0x7c001fec
    // ===================================================================
    void dcbst(RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, PPC64Registers::r0, ra, rb, /*XO*/ 54, /*Rc*/ 0));
    }

    void dcbf(RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, PPC64Registers::r0, ra, rb, /*XO*/ 86, /*Rc*/ 0));
    }

    void icbi(RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, PPC64Registers::r0, ra, rb, /*XO*/ 982, /*Rc*/ 0));
    }

    void dcbz(RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, PPC64Registers::r0, ra, rb, /*XO*/ 1014, /*Rc*/ 0));
    }

    // ===================================================================
    // VMX (Altivec) logical instructions — VX-form, opcode 4.
    // Power ISA v2.07B Book I §6.9. All operate on full 128-bit vectors;
    // the lane interpretation doesn't matter for bitwise ops.
    //
    //   vand   VRT, VRA, VRB — XO=1028 (VRT <- VRA AND VRB)
    //   vor    VRT, VRA, VRB — XO=1156 (VRT <- VRA OR  VRB)
    //   vxor   VRT, VRA, VRB — XO=1220 (VRT <- VRA XOR VRB)
    //   vnor   VRT, VRA, VRB — XO=1284 (VRT <- ~(VRA OR VRB))
    //   vandc  VRT, VRA, VRB — XO=1092 (VRT <- VRA AND ~VRB)
    //
    // POWER9 future-stub: veqv (XO=1668) and vorc (XO=1348) are POWER8
    // additions on POWER ISA v2.07B but already supported by us as
    // "v2.07B baseline" — we will add them here when MacroAssembler asks.
    // POWER10 v3.1 adds prefixed/MMA forms that won't be relevant.
    //
    // Verified on POWER9:
    //   vand  3,4,5 → 0x10642c04   vor   3,4,5 → 0x10642c84
    //   vxor  3,4,5 → 0x10642cc4   vnor  3,4,5 → 0x10642d04
    //   vandc 3,4,5 → 0x10642c44
    // ===================================================================
    void vand(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)
    {
        insn(vxForm(4, vrt, vra, vrb, /*XO*/ 1028));
    }

    void vor(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)
    {
        insn(vxForm(4, vrt, vra, vrb, /*XO*/ 1156));
    }

    void vxor(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)
    {
        insn(vxForm(4, vrt, vra, vrb, /*XO*/ 1220));
    }

    void vnor(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)
    {
        insn(vxForm(4, vrt, vra, vrb, /*XO*/ 1284));
    }

    void vandc(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)
    {
        insn(vxForm(4, vrt, vra, vrb, /*XO*/ 1092));
    }

    // ===================================================================
    // VMX integer add/sub modulo (wrap on overflow). Power ISA v2.07B
    // §6.10. The "modulo" suffix means wrap-on-overflow, in contrast to
    // the saturating variants (vaddubs / vsububs / etc.) which clamp.
    // Unsigned vs signed make no difference for modulo arithmetic at
    // these widths — there's no separate vaddsbm / vaddsbs (that's
    // why the Power ISA only documents the unsigned mnemonics).
    //
    //   vaddubm VRT, VRA, VRB — XO=0    (8-bit  lane add)
    //   vadduhm VRT, VRA, VRB — XO=64   (16-bit lane add)
    //   vadduwm VRT, VRA, VRB — XO=128  (32-bit lane add)
    //   vaddudm VRT, VRA, VRB — XO=192  (64-bit lane add, POWER8+)
    //   vsububm VRT, VRA, VRB — XO=1024 (8-bit  lane sub)
    //   vsubuhm VRT, VRA, VRB — XO=1088 (16-bit lane sub)
    //   vsubuwm VRT, VRA, VRB — XO=1152 (32-bit lane sub)
    //   vsubudm VRT, VRA, VRB — XO=1216 (64-bit lane sub, POWER8+)
    //
    // POWER9 future-stubs: vmul10cuq (XO=1) and vmul10euq (XO=65) are
    // BCD helpers we won't need for JSC. POWER9 also adds vmuluwm /
    // vmulosw / vmulouw for SIMD multiply (none in v2.07B).
    //
    // Verified on POWER9 (all 8; see encoding test).
    // ===================================================================
    void vaddubm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 0));    }
    void vadduhm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 64));   }
    void vadduwm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 128));  }
    void vaddudm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 192));  }
    void vsububm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1024)); }
    void vsubuhm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1088)); }
    void vsubuwm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1152)); }
    void vsubudm(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1216)); }

    // ===================================================================
    // VMX load/store (X-form, opcode 31). Power ISA v2.07B §6.6.
    //
    //   lvx   VRT, RA, RB — XO=103  (load 16 bytes; addr forced to 16B align)
    //   stvx  VRS, RA, RB — XO=231  (store 16 bytes; addr forced to 16B align)
    //   lvebx VRT, RA, RB — XO=7    (load 1 byte into the lane addressed by EA[60:63])
    //   lvehx VRT, RA, RB — XO=39   (load 2 bytes into halfword lane)
    //   lvewx VRT, RA, RB — XO=71   (load 4 bytes into word lane)
    //   stvebx VRS, RA, RB — XO=135 (store byte from lane)
    //   stvehx VRS, RA, RB — XO=167 (store halfword from lane)
    //   stvewx VRS, RA, RB — XO=199 (store word from lane)
    //
    // Important quirk: lvx/stvx silently mask the low 4 bits of the
    // effective address to enforce 16-byte alignment. Callers that need
    // unaligned vector loads must use lxvd2x/stxvd2x (VSX, separate
    // helper) or lvsl + permute. PLAN.md "Lessons" §SIMD calls out
    // unaligned VMX loads as a footgun.
    //
    // POWER9 future-stubs: lxv (VSX scalar load 128b, opcode 61), stxv
    // (opcode 61), lxvb16x / lxvh8x / lxvw4x / lxvd2x for VSX byte-swap
    // big-endian loads. We will add when MacroAssembler asks.
    //
    // Verified on POWER9 (8 cases; see encoding test).
    // ===================================================================
    void lvx(VRegisterID vrt, RegisterID ra, RegisterID rb)    { insn(xFormVrMem(31, vrt, ra, rb, 103)); }
    void stvx(VRegisterID vrs, RegisterID ra, RegisterID rb)   { insn(xFormVrMem(31, vrs, ra, rb, 231)); }
    void lvebx(VRegisterID vrt, RegisterID ra, RegisterID rb)  { insn(xFormVrMem(31, vrt, ra, rb,   7)); }
    void lvehx(VRegisterID vrt, RegisterID ra, RegisterID rb)  { insn(xFormVrMem(31, vrt, ra, rb,  39)); }
    void lvewx(VRegisterID vrt, RegisterID ra, RegisterID rb)  { insn(xFormVrMem(31, vrt, ra, rb,  71)); }
    void stvebx(VRegisterID vrs, RegisterID ra, RegisterID rb) { insn(xFormVrMem(31, vrs, ra, rb, 135)); }
    void stvehx(VRegisterID vrs, RegisterID ra, RegisterID rb) { insn(xFormVrMem(31, vrs, ra, rb, 167)); }
    void stvewx(VRegisterID vrs, RegisterID ra, RegisterID rb) { insn(xFormVrMem(31, vrs, ra, rb, 199)); }

    // ===================================================================
    // VMX vector compares (VC-form, opcode 4). Power ISA v2.07B §6.10.
    // Each lane is set to all-ones (true) or all-zeros (false). The Rc=1
    // variants additionally update CR6 with summary bits ([all-true,
    // -, all-false, -]); we expose Rc=0 by default and add `Dot`
    // variants for callers that need the CR update.
    //
    // Equality (XO):
    //   vcmpequb XO=6   vcmpequh XO=70   vcmpequw XO=134
    //   vcmpequd XO=199 (POWER8+)
    // Greater-than unsigned (XO):
    //   vcmpgtub XO=518 vcmpgtuh XO=582  vcmpgtuw XO=646
    //   vcmpgtud XO=711 (POWER8+)
    // Greater-than signed (XO):
    //   vcmpgtsb XO=774 vcmpgtsh XO=838  vcmpgtsw XO=902
    //   vcmpgtsd XO=967 (POWER8+)
    //
    // POWER9 future-stubs: v3.0 adds vcmpneb/vcmpneh/vcmpnew (not-equal)
    // and vcmpnezb/.../vcmpnezw (not-equal-or-zero) — useful for some
    // string/permute fast paths but not in v2.07B.
    //
    // Verified on POWER9; see encoding test for hex values.
    // ===================================================================
    void vcmpequb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0,  6)); }
    void vcmpequh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 70)); }
    void vcmpequw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 134)); }
    void vcmpequd(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 199)); }
    void vcmpgtub(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 518)); }
    void vcmpgtuh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 582)); }
    void vcmpgtuw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 646)); }
    void vcmpgtud(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 711)); }
    void vcmpgtsb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 774)); }
    void vcmpgtsh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 838)); }
    void vcmpgtsw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 902)); }
    void vcmpgtsd(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vcForm(4, vrt, vra, vrb, 0, 967)); }

    // ===================================================================
    // VMX splats — broadcast a single value to all lanes. Power ISA
    // v2.07B §6.7. Two flavors:
    //
    // (1) Splat from a vector lane (vspltb / vsplth / vspltw): the source
    //     vector is in VRB; the lane index UIMM goes in the VRA slot
    //     (bits 11-15) and selects which lane of VRB to broadcast.
    //
    // (2) Splat-immediate (vspltisb / vspltish / vspltisw): a 5-bit
    //     signed immediate (range [-16, 15]) is sign-extended to the
    //     lane width and stored in every lane. SIMM goes in the VRA
    //     slot; VRB is unused (zero).
    //
    //   vspltb   VRT, VRB, UIMM  — XO=524, UIMM 0-15 selects byte lane
    //   vsplth   VRT, VRB, UIMM  — XO=588, UIMM 0-7  selects halfword lane
    //   vspltw   VRT, VRB, UIMM  — XO=652, UIMM 0-3  selects word lane
    //   vspltisb VRT, SIMM       — XO=780
    //   vspltish VRT, SIMM       — XO=844
    //   vspltisw VRT, SIMM       — XO=908
    //
    // **PPC64LE lane numbering trap** (PLAN.md SIMD lessons): on PPC64LE
    // the "high" / "low" of a vector is reversed relative to BE. UIMM
    // values that look natural in big-endian asm need a "wasm-lane-
    // direction" mental model. Document at every callsite.
    //
    // POWER9 future-stub: v3.0 adds vspltisw without the UIMM/SIMM
    // limitation via prefixed instructions, but for v2.07B only 5-bit
    // immediates are available.
    //
    // Verified on POWER9 (see encoding test for hex).
    // ===================================================================
    void vspltb(VRegisterID vrt, VRegisterID vrb, uint32_t uimm)
    {
        ASSERT(uimm < 16);
        insn((4u << 26) | (vrValue(vrt) << 21) | (uimm << 16) | (vrValue(vrb) << 11) | 524u);
    }

    void vsplth(VRegisterID vrt, VRegisterID vrb, uint32_t uimm)
    {
        ASSERT(uimm < 8);
        insn((4u << 26) | (vrValue(vrt) << 21) | (uimm << 16) | (vrValue(vrb) << 11) | 588u);
    }

    void vspltw(VRegisterID vrt, VRegisterID vrb, uint32_t uimm)
    {
        ASSERT(uimm < 4);
        insn((4u << 26) | (vrValue(vrt) << 21) | (uimm << 16) | (vrValue(vrb) << 11) | 652u);
    }

    void vspltisb(VRegisterID vrt, int32_t simm)
    {
        ASSERT(simm >= -16 && simm < 16);
        insn((4u << 26) | (vrValue(vrt) << 21) | ((static_cast<uint32_t>(simm) & 0x1F) << 16) | 780u);
    }

    void vspltish(VRegisterID vrt, int32_t simm)
    {
        ASSERT(simm >= -16 && simm < 16);
        insn((4u << 26) | (vrValue(vrt) << 21) | ((static_cast<uint32_t>(simm) & 0x1F) << 16) | 844u);
    }

    void vspltisw(VRegisterID vrt, int32_t simm)
    {
        ASSERT(simm >= -16 && simm < 16);
        insn((4u << 26) | (vrValue(vrt) << 21) | ((static_cast<uint32_t>(simm) & 0x1F) << 16) | 908u);
    }

    // ===================================================================
    // VMX merge — interleave lanes from two source vectors. Power ISA
    // v2.07B §6.8. Foundation for shuffle / unpack patterns.
    //
    //   vmrghb VRT, VRA, VRB — XO=12   merge HIGH bytes
    //   vmrghh VRT, VRA, VRB — XO=76   merge HIGH halfwords
    //   vmrghw VRT, VRA, VRB — XO=140  merge HIGH words
    //   vmrglb VRT, VRA, VRB — XO=268  merge LOW  bytes
    //   vmrglh VRT, VRA, VRB — XO=332  merge LOW  halfwords
    //   vmrglw VRT, VRA, VRB — XO=396  merge LOW  words
    //   vmrgew VRT, VRA, VRB — XO=1932 merge EVEN words (POWER8+)
    //   vmrgow VRT, VRA, VRB — XO=1676 merge ODD  words (POWER8+)
    //
    // **PPC64LE TRAP**: VMX naming uses big-endian "high"/"low". On
    // PPC64LE, vmrghb actually combines what wasm-lane-direction code
    // would call the LOW bytes. Always reason in WASM-lane terms in
    // JSC source comments — see PLAN.md SIMD lessons.
    //
    // Verified on POWER9 (8 cases).
    // ===================================================================
    void vmrghb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,   12)); }
    void vmrghh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,   76)); }
    void vmrghw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  140)); }
    void vmrglb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  268)); }
    void vmrglh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  332)); }
    void vmrglw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  396)); }
    void vmrgew(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1932)); }
    void vmrgow(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb, 1676)); }

    // ===================================================================
    // VMX lane-wise shifts and rotates. Power ISA v2.07B §6.8/§6.10.
    // Shift count comes from each lane of VRB modulo lane width.
    //
    // Left shift:
    //   vslb XO=260   vslh XO=324   vslw XO=388   vsld XO=1476 (POWER8+)
    // Right shift logical:
    //   vsrb XO=516   vsrh XO=580   vsrw XO=644   vsrd XO=1732 (POWER8+)
    // Right shift arithmetic:
    //   vsrab XO=772  vsrah XO=836  vsraw XO=900  vsrad XO=964 (POWER8+)
    // Rotate left:
    //   vrlb XO=4     vrlh XO=68    vrlw XO=132   vrld XO=196  (POWER8+)
    //
    // POWER9 future-stubs: v3.0 adds vbpermd (bit-permute by doubleword)
    // and vrlwnm/vrldnm (rotate-then-mask vector); not in v2.07B.
    //
    // Verified on POWER9 (16 cases).
    // ===================================================================
    void vslb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  260)); }
    void vslh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  324)); }
    void vslw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  388)); }
    void vsld(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb, 1476)); }
    void vsrb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  516)); }
    void vsrh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  580)); }
    void vsrw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  644)); }
    void vsrd(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb, 1732)); }
    void vsrab(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  772)); }
    void vsrah(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  836)); }
    void vsraw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  900)); }
    void vsrad(VRegisterID vrt, VRegisterID vra, VRegisterID vrb) { insn(vxForm(4, vrt, vra, vrb,  964)); }
    void vrlb(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,    4)); }
    void vrlh(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,   68)); }
    void vrlw(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  132)); }
    void vrld(VRegisterID vrt, VRegisterID vra, VRegisterID vrb)  { insn(vxForm(4, vrt, vra, vrb,  196)); }

    // ===================================================================
    // Atomic Load-Reserved / Store-Conditional (X-form). Power ISA v2.07B
    // §3.3.1.1 (lwarx/ldarx) and §3.3.1.2 (stwcx./stdcx.). The building
    // blocks for every atomic on PPC: compare-exchange, fetch-add,
    // fetch-or, etc. All take indexed addressing (RA + RB, where RA=0
    // means literal zero). The store-conditional variants always set
    // Rc=1 (hence the trailing "." in the mnemonic) — CR0[EQ] is set to
    // 1 if the conditional store succeeded, 0 if the reservation was
    // lost.
    //
    //   lwarx   RT, RA, RB — opcode 31, XO=20   (32-bit)
    //   ldarx   RT, RA, RB — opcode 31, XO=84   (64-bit)
    //   lbarx   RT, RA, RB — opcode 31, XO=52   (8-bit,  POWER8+)
    //   lharx   RT, RA, RB — opcode 31, XO=116  (16-bit, POWER8+)
    //   stwcx.  RS, RA, RB — opcode 31, XO=150, Rc=1
    //   stdcx.  RS, RA, RB — opcode 31, XO=214, Rc=1
    //   stbcx.  RS, RA, RB — opcode 31, XO=694, Rc=1 (POWER8+)
    //   sthcx.  RS, RA, RB — opcode 31, XO=726, Rc=1 (POWER8+)
    //
    // Important: the fail path of a reservation sequence LEAKS the
    // reservation until it is cleared by a subsequent stwcx./stdcx. or
    // an interrupt. PLAN.md "Lessons from SpiderMonkey" §Concurrency
    // calls out this as an open hazard. Compare-exchange macros must
    // route the fail path through a dummy store-conditional or equivalent.
    //
    // Verified on POWER9:
    //   lwarx  3,4,5 → 0x7c642828     ldarx  3,4,5 → 0x7c6428a8
    //   stwcx. 3,4,5 → 0x7c64292d     stdcx. 3,4,5 → 0x7c6429ad
    //   lbarx  3,4,5 → 0x7c642868     lharx  3,4,5 → 0x7c6428e8
    //   stbcx. 3,4,5 → 0x7c642d6d     sthcx. 3,4,5 → 0x7c642dad
    // ===================================================================
    void lwarx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 20, /*Rc*/ 0));
    }

    void ldarx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 84, /*Rc*/ 0));
    }

    void lbarx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 52, /*Rc*/ 0));
    }

    void lharx(RegisterID rt, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rt, ra, rb, /*XO*/ 116, /*Rc*/ 0));
    }

    void stwcx_(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 150, /*Rc*/ 1));
    }

    void stdcx_(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 214, /*Rc*/ 1));
    }

    void stbcx_(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 694, /*Rc*/ 1));
    }

    void sthcx_(RegisterID rs, RegisterID ra, RegisterID rb)
    {
        insn(xForm(31, rs, ra, rb, /*XO*/ 726, /*Rc*/ 1));
    }

protected:
    void insn(uint32_t instruction)
    {
        m_buffer.putInt(instruction);
    }

    // D-form: [op(6) | RT/RS(5) | RA(5) | D/SI/UI(16)]
    // See Power ISA v2.07B Book I §1.6.1 Figure 3.
    static constexpr uint32_t dForm(uint32_t opcode, RegisterID rtOrRs, RegisterID ra, uint16_t imm)
    {
        ASSERT(opcode < 64);
        return (opcode << 26)
             | (registerValue(rtOrRs) << 21)
             | (registerValue(ra) << 16)
             | imm;
    }

    // XO-form: [op(6) | RT(5) | RA(5) | RB(5) | OE(1) | XO(9) | Rc(1)]
    // See Power ISA v2.07B Book I §1.6.1 Figure 3. Bit positions (MSB=0):
    //   OE at bit 21 (shift 10 from LSB), XO at bits 22-30 (shift 1 from LSB),
    //   Rc at bit 31 (shift 0).
    // DS-form: [op(6) | RT/RS(5) | RA(5) | DS(14) | XO(2)]
    // See Power ISA v2.07B Book I §1.6.1 Figure 3. DS is a 14-bit signed word
    // displacement; the effective byte offset is sign_extend(DS || 0b00), i.e.
    // DS << 2. In the instruction, DS occupies bits 16-29 and XO occupies bits
    // 30-31; that is, the low 16 bits of the instruction hold
    // (byteOffset & 0xFFFC) | XO, provided byteOffset is a multiple of 4.
    static constexpr uint32_t dsForm(uint32_t opcode, RegisterID rtOrRs, RegisterID ra, int16_t byteOffset, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 4);
        ASSERT((byteOffset & 0x3) == 0);
        return (opcode << 26)
             | (registerValue(rtOrRs) << 21)
             | (registerValue(ra) << 16)
             | (static_cast<uint32_t>(static_cast<uint16_t>(byteOffset)) & 0xFFFC)
             | xo;
    }

    // M-form (32-bit rotate-and-mask):
    //   [op(6) | RS(5) | RA(5) | SH/RB(5) | MB(5) | ME(5) | Rc(1)]
    //
    // Power ISA v2.07B Book I §1.6.1. Much simpler than MD-form — the
    // shift is a 5-bit immediate (no bit-split) and MB/ME are plain
    // 5-bit fields (no rotation of encoding). Two variants: SH comes
    // from an immediate (rlwinm, rlwimi) or from RB (rlwnm).
    static constexpr uint32_t mFormImm(uint32_t opcode, RegisterID rs, RegisterID ra,
                                       uint32_t sh, uint32_t mb, uint32_t me, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(sh < 32);
        ASSERT(mb < 32);
        ASSERT(me < 32);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (registerValue(rs) << 21)
             | (registerValue(ra) << 16)
             | (sh << 11)
             | (mb << 6)
             | (me << 1)
             | rc;
    }

    static constexpr uint32_t mFormReg(uint32_t opcode, RegisterID rs, RegisterID ra,
                                       RegisterID rb, uint32_t mb, uint32_t me, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(mb < 32);
        ASSERT(me < 32);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (registerValue(rs) << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (mb << 6)
             | (me << 1)
             | rc;
    }

    // XS-form: [op(6) | RS(5) | RA(5) | sh[0:4](5) | XO(9) | sh2(1) | Rc(1)]
    //
    // Power ISA v2.07B Book I §1.6.1. Used only by sradi and siblings
    // (the 64-bit shift-right-algebraic-immediate). Like MD-form the
    // 6-bit shift is split — low 5 at instruction bits 16-20, high 1
    // at bit 30 — but unlike MD-form there is no mb field, so XO gets
    // 9 bits at instruction bits 21-29.
    static constexpr uint32_t xsForm(uint32_t opcode, RegisterID rs, RegisterID ra,
                                     uint32_t sh, uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(sh < 64);
        ASSERT(xo < 512);
        ASSERT(rc < 2);
        uint32_t shLow5 = sh & 0x1F;
        uint32_t sh2 = (sh >> 5) & 1;
        return (opcode << 26)
             | (registerValue(rs) << 21)
             | (registerValue(ra) << 16)
             | (shLow5 << 11)
             | (xo << 2)
             | (sh2 << 1)
             | rc;
    }

    // MD-form: [op(6) | RS(5) | RA(5) | sh[0:4](5) | mb/me(6) | XO(3) | sh2(1) | Rc(1)]
    //
    // Power ISA v2.07B Book I §1.6.1. The 6-bit shift is SPLIT: bits 0-4
    // (low 5) live at instruction bits 16-20, bit 5 (high) lives at
    // instruction bit 30 (the "sh2" slot).
    //
    // The mb/me 6-bit mask-boundary field at instruction bits 21-26 is
    // ALSO shuffled relative to a natural big-endian 6-bit value:
    //   encoded[21..26] = mb[1], mb[2], mb[3], mb[4], mb[5], mb[0]
    // where mb[0] is Power-MSB of the value. Equivalently the 6-bit
    // encoded field value is ROTL6(mb, 1). Empirically confirmed on
    // POWER9 with 8 distinct mb values — see the "MD-form probe"
    // block in ppc64_encoding_test.cpp.
    static constexpr uint32_t mdForm(uint32_t opcode, RegisterID rs, RegisterID ra,
                                     uint32_t sh, uint32_t mbOrMe, uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(sh < 64);
        ASSERT(mbOrMe < 64);
        ASSERT(xo < 8);
        ASSERT(rc < 2);
        uint32_t shLow5 = sh & 0x1F;
        uint32_t sh2 = (sh >> 5) & 1;
        // Encode mb/me with the low 5 bits at instruction bits 21-25 and
        // the high bit at bit 26 — i.e. a left-rotate-by-1 in the 6-bit
        // field. See the block comment above for the empirical evidence.
        uint32_t mbField = ((mbOrMe & 0x1F) << 1) | ((mbOrMe >> 5) & 1);
        return (opcode << 26)
             | (registerValue(rs) << 21)
             | (registerValue(ra) << 16)
             | (shLow5 << 11)
             | (mbField << 5)
             | (xo << 2)
             | (sh2 << 1)
             | rc;
    }

    // X-form: [op(6) | RT/RS(5) | RA(5) | RB(5) | XO(10) | Rc(1)]
    // Power ISA v2.07B Book I §1.6.1. Differs from XO-form in that bit 21
    // is part of the 10-bit XO field (no OE). The 5-bit slot at bits 6-10
    // is RT for loads (destination) and RS for logical ops (source);
    // semantics depend on the opcode, the bit layout is identical.
    static constexpr uint32_t xForm(uint32_t opcode, RegisterID rtOrRs, RegisterID ra, RegisterID rb, uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 1024);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (registerValue(rtOrRs) << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (xo << 1)
             | rc;
    }

    // Compare X-form: [op(6) | BF(3) | /(1) | L(1) | RA(5) | RB(5) | XO(10) | /(1)]
    // Power ISA v2.07B Book I §1.6.1. BF (3 bits, bits 6-8) selects which
    // CR field to set; L (bit 10) picks 32-bit (L=0) vs 64-bit (L=1) width.
    // Shift positions: BF at bits 6-8 → (BF << 23); L at bit 10 → (L << 21).
    static constexpr uint32_t cmpXForm(uint32_t opcode, uint32_t bf, uint32_t l,
                                       RegisterID ra, RegisterID rb, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(bf < 8);
        ASSERT(l < 2);
        ASSERT(xo < 1024);
        return (opcode << 26)
             | (bf << 23)
             | (l << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (xo << 1);
    }

    // Compare D-form: [op(6) | BF(3) | /(1) | L(1) | RA(5) | SI/UI(16)]
    static constexpr uint32_t cmpDForm(uint32_t opcode, uint32_t bf, uint32_t l,
                                       RegisterID ra, uint16_t imm)
    {
        ASSERT(opcode < 64);
        ASSERT(bf < 8);
        ASSERT(l < 2);
        return (opcode << 26)
             | (bf << 23)
             | (l << 21)
             | (registerValue(ra) << 16)
             | imm;
    }

    // I-form: [op(6) | LI(24) | AA(1) | LK(1)]
    // See Power ISA v2.07B Book I §1.6.1 Figure 3. LI is a 24-bit signed
    // word-displacement; effective byte offset is sign_extend(LI || 0b00).
    // Since the byte offset has its low 2 bits = 0, we can pack it directly
    // into bits 2-25 of the instruction (mask 0x03FFFFFC).
    static constexpr uint32_t iForm(uint32_t opcode, int32_t byteOffset, uint32_t aa, uint32_t lk)
    {
        ASSERT(opcode < 64);
        ASSERT(aa < 2);
        ASSERT(lk < 2);
        ASSERT((byteOffset & 0x3) == 0);
        // 26-bit signed byte range: [-2^25, 2^25 - 4].
        ASSERT(byteOffset >= -(1 << 25) && byteOffset < (1 << 25));
        return (opcode << 26)
             | (static_cast<uint32_t>(byteOffset) & 0x03FFFFFC)
             | (aa << 1)
             | lk;
    }

    // B-form: [op(6) | BO(5) | BI(5) | BD(14) | AA(1) | LK(1)]
    // BD is a 14-bit signed word-displacement.
    static constexpr uint32_t bForm(uint32_t opcode, uint32_t bo, uint32_t bi,
                                    int32_t byteOffset, uint32_t aa, uint32_t lk)
    {
        ASSERT(opcode < 64);
        ASSERT(bo < 32);
        ASSERT(bi < 32);
        ASSERT(aa < 2);
        ASSERT(lk < 2);
        ASSERT((byteOffset & 0x3) == 0);
        // 16-bit signed byte range: [-2^15, 2^15 - 4].
        ASSERT(byteOffset >= -(1 << 15) && byteOffset < (1 << 15));
        return (opcode << 26)
             | (bo << 21)
             | (bi << 16)
             | (static_cast<uint32_t>(byteOffset) & 0xFFFC)
             | (aa << 1)
             | lk;
    }

    // XL-form: [op(6) | BO(5) | BI(5) | BH(3) | //(2) | XO(10) | LK(1)]
    // BH is a 3-bit branch-prediction hint at bits 16-18; bits 19-20 are
    // reserved (must be 0). XO at bits 21-30 (shift 1 from LSB).
    static constexpr uint32_t xlForm(uint32_t opcode, uint32_t bo, uint32_t bi,
                                     uint32_t bh, uint32_t xo, uint32_t lk)
    {
        ASSERT(opcode < 64);
        ASSERT(bo < 32);
        ASSERT(bi < 32);
        ASSERT(bh < 8);
        ASSERT(xo < 1024);
        ASSERT(lk < 2);
        return (opcode << 26)
             | (bo << 21)
             | (bi << 16)
             | (bh << 13)
             | (xo << 1)
             | lk;
    }

    // XFX-form: [op(6) | RT/RS(5) | spr(10) | XO(10) | /(1)]
    // See Power ISA v2.07B Book I §1.6.1 Figure 3. The 10-bit spr field at
    // bits 11-20 encodes the SPR number with its two 5-bit halves swapped:
    //   sprField = ((sprNum & 0x1F) << 5) | ((sprNum >> 5) & 0x1F)
    // Bit 31 is reserved and must be 0.
    static constexpr uint32_t xfxForm(uint32_t opcode, RegisterID rtOrRs, uint32_t sprNum, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(sprNum < 1024);
        ASSERT(xo < 1024);
        uint32_t sprField = ((sprNum & 0x1F) << 5) | ((sprNum >> 5) & 0x1F);
        return (opcode << 26)
             | (registerValue(rtOrRs) << 21)
             | (sprField << 11)
             | (xo << 1);
    }

    static constexpr uint32_t xoForm(uint32_t opcode, RegisterID rt, RegisterID ra, RegisterID rb,
                                     uint32_t oe, uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(oe < 2);
        ASSERT(xo < 512);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (registerValue(rt) << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (oe << 10)
             | (xo << 1)
             | rc;
    }

    static constexpr uint32_t registerValue(RegisterID r)
    {
        ASSERT(r >= firstRegister() && r <= lastRegister());
        return static_cast<uint32_t>(r);
    }

    static constexpr uint32_t fprValue(FPRegisterID r)
    {
        ASSERT(r >= firstFPRegister() && r <= lastFPRegister());
        return static_cast<uint32_t>(r);
    }

    static constexpr uint32_t vrValue(VRegisterID r)
    {
        ASSERT(r >= firstVRegister() && r <= lastVRegister());
        return static_cast<uint32_t>(r);
    }

    // FP D-form: same bit layout as GPR D-form but the 5-bit RT/RS slot
    // holds an FPR number rather than a GPR.
    static constexpr uint32_t dFormFp(uint32_t opcode, FPRegisterID frtOrFrs, RegisterID ra, uint16_t imm)
    {
        ASSERT(opcode < 64);
        return (opcode << 26)
             | (fprValue(frtOrFrs) << 21)
             | (registerValue(ra) << 16)
             | imm;
    }

    // X-form FP indexed load/store: FPR in the first slot, GPRs for the
    // base (RA) and index (RB). Used by lfdx/stfdx/lfsx/stfsx and their
    // update variants.
    static constexpr uint32_t xFormFpMem(uint32_t opcode, FPRegisterID frtOrFrs,
                                         RegisterID ra, RegisterID rb, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 1024);
        return (opcode << 26)
             | (fprValue(frtOrFrs) << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (xo << 1);
    }

    // A-form (FP arithmetic):
    //   [op(6) | FRT(5) | FRA(5) | FRB(5) | FRC(5) | XO(5) | Rc(1)]
    // Power ISA v2.07B Book I §1.6.1. Differs from X-form in that the
    // 10-bit XO slot (bits 21-30) is split into a 5-bit FRC at 21-25
    // and a 5-bit XO at 26-30. This is the encoding used by every
    // multi-operand FP arithmetic instruction.
    static constexpr uint32_t aForm(uint32_t opcode, FPRegisterID frt, FPRegisterID fra,
                                    FPRegisterID frb, FPRegisterID frc,
                                    uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 32);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (fprValue(frt) << 21)
             | (fprValue(fra) << 16)
             | (fprValue(frb) << 11)
             | (fprValue(frc) << 6)
             | (xo << 1)
             | rc;
    }

    // FP X-form: same bit layout as GPR X-form, FPR indices in the
    // RT/RS/RA/RB slots.
    static constexpr uint32_t xFormFp(uint32_t opcode, FPRegisterID frtOrFrs, FPRegisterID fra,
                                      FPRegisterID frb, uint32_t xo, uint32_t rc)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 1024);
        ASSERT(rc < 2);
        return (opcode << 26)
             | (fprValue(frtOrFrs) << 21)
             | (fprValue(fra) << 16)
             | (fprValue(frb) << 11)
             | (xo << 1)
             | rc;
    }

    // X-form VMX indexed load/store: VR in RT/RS slot, GPRs for base+index.
    // Used by lvx/stvx and the per-element lv{e,h,w}{b,h,w}x variants.
    static constexpr uint32_t xFormVrMem(uint32_t opcode, VRegisterID vrtOrVrs,
                                         RegisterID ra, RegisterID rb, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 1024);
        return (opcode << 26)
             | (vrValue(vrtOrVrs) << 21)
             | (registerValue(ra) << 16)
             | (registerValue(rb) << 11)
             | (xo << 1);
    }

    // VC-form (VMX compare): [op(6)=4 | VRT(5) | VRA(5) | VRB(5) | Rc(1) | XO(10)]
    // Power ISA v2.07B Book I §1.6.1. Used by all vcmp* instructions.
    // Rc=1 (the assembler "." suffix) also writes a summary into CR6.
    static constexpr uint32_t vcForm(uint32_t opcode, VRegisterID vrt, VRegisterID vra,
                                     VRegisterID vrb, uint32_t rc, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(rc < 2);
        ASSERT(xo < 1024);
        return (opcode << 26)
             | (vrValue(vrt) << 21)
             | (vrValue(vra) << 16)
             | (vrValue(vrb) << 11)
             | (rc << 10)
             | xo;
    }

    // VX-form (VMX vector). Power ISA v2.07B Book I §6.1.
    //   [op(6)=4 | VRT(5) | VRA(5) | VRB(5) | XO(11)]
    // XO occupies the entire low 11 bits of the instruction (bits 21-31)
    // — no shift needed when packing.
    static constexpr uint32_t vxForm(uint32_t opcode, VRegisterID vrt, VRegisterID vra,
                                     VRegisterID vrb, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(xo < 2048);
        return (opcode << 26)
             | (vrValue(vrt) << 21)
             | (vrValue(vra) << 16)
             | (vrValue(vrb) << 11)
             | xo;
    }

    // FP compare X-form. Same shape as integer cmpXForm but always L=0
    // (the reserved bit 10) and operands are FPRs.
    //   [op=63 | BF(3) | //(2) | FRA(5) | FRB(5) | XO(10) | /(1)]
    static constexpr uint32_t fpCmpXForm(uint32_t opcode, uint32_t bf,
                                         FPRegisterID fra, FPRegisterID frb, uint32_t xo)
    {
        ASSERT(opcode < 64);
        ASSERT(bf < 8);
        ASSERT(xo < 1024);
        return (opcode << 26)
             | (bf << 23)
             | (fprValue(fra) << 16)
             | (fprValue(frb) << 11)
             | (xo << 1);
    }

private:
    AssemblerBuffer m_buffer;
};

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
