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

} // namespace PPC64Registers

class PPC64Assembler {
public:
    using RegisterID = PPC64Registers::RegisterID;
    using SPRegisterID = PPC64Registers::SPRegisterID;
    using FPRegisterID = PPC64Registers::FPRegisterID;

    static constexpr RegisterID firstRegister() { return PPC64Registers::r0; }
    static constexpr RegisterID lastRegister() { return PPC64Registers::r31; }
    static constexpr unsigned numberOfRegisters() { return lastRegister() - firstRegister() + 1; }

    static constexpr SPRegisterID firstSPRegister() { return PPC64Registers::pc; }
    static constexpr SPRegisterID lastSPRegister() { return PPC64Registers::pc; }
    static constexpr unsigned numberOfSPRegisters() { return lastSPRegister() - firstSPRegister() + 1; }

    static constexpr FPRegisterID firstFPRegister() { return PPC64Registers::f0; }
    static constexpr FPRegisterID lastFPRegister() { return PPC64Registers::f31; }
    static constexpr unsigned numberOfFPRegisters() { return lastFPRegister() - firstFPRegister() + 1; }

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

private:
    AssemblerBuffer m_buffer;
};

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(PPC64LE)
