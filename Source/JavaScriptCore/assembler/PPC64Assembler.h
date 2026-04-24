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
