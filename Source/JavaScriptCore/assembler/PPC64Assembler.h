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
