# Copyright (C) 2026 Trung Lê and contributors. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

# PPC64LE (ELFv2) register conventions
#
# GPR assignments:
#   r0   => scratch (holds LR save during function prologue; never a named offlineasm reg)
#   r1   => sp
#   r2   => TOC pointer (reserved by ABI, not used by JSC)
#   r3   => t0, a0, wa0, r0 (return / 1st arg)
#   r4   => t1, a1, wa1, r1 (2nd arg)
#   r5   => t2, a2, wa2
#   r6   => t3, a3, wa3
#   r7   => t4, a4, wa4
#   r8   => t5, a5, wa5
#   r9   => t6, a6, wa6
#   r10  => t7, a7, wa7
#   r11  => ws0 (volatile scratch)
#   r12  => ws1 (volatile scratch)
#   r13  => TLS pointer (reserved)
#   r14  => csr0
#   r15  => csr1
#   r16  => csr2
#   r17  => csr3
#   r18  => csr4
#   r19  => csr5
#   r20  => csr6  (metadataTable)
#   r21  => csr7  (PB / jitData)
#   r22  => csr8  (numberTag)
#   r23  => csr9  (notCellMask)
#   r24  => csr10
#   r31  => cfr   (frame pointer, callee-saved)
#
# FPR assignments (ELFv2: f0 volatile scratch; f1-f13 volatile args; f14-f31 callee-save):
#   f0   => ft0
#   f1   => fa0, wfa0
#   f2   => fa1, wfa1
#   f3   => fa2, wfa2
#   f4   => fa3, wfa3
#   f5   => fa4, wfa4
#   f6   => fa5, wfa5
#   f7   => fa6, wfa6
#   f8   => fa7, wfa7
#   f9   => ft1
#   f10  => ft2
#   f11  => ft3
#   f12  => ft4
#   f13  => ft5
#   f14  => csfr0
#   f15  => csfr1
#   f16  => csfr2
#   f17  => csfr3
#   f18  => csfr4
#   f19  => csfr5
#   f20  => csfr6
#   f21  => csfr7

# Extra scratch GPRs available for Tmp allocation (r25-r30 are callee-save but
# not otherwise named in offlineasm; r0 is volatile ABI scratch).
PPC64LE_EXTRA_GPRS = [
    SpecialRegister.new("r25"),
    SpecialRegister.new("r26"),
    SpecialRegister.new("r27"),
    SpecialRegister.new("r28"),
    SpecialRegister.new("r29"),
    SpecialRegister.new("r30"),
]
PPC64LE_EXTRA_FPRS = [
    SpecialRegister.new("f22"),
    SpecialRegister.new("f23"),
    SpecialRegister.new("f24"),
    SpecialRegister.new("f25"),
    SpecialRegister.new("f26"),
    SpecialRegister.new("f27"),
]

# -------------------------------------------------------------------------
# RegisterID operand → PPC64LE register name string
# -------------------------------------------------------------------------
class RegisterID
    def ppc64leOperand
        case @name
        when "t0", "a0", "wa0", "r0"
            "r3"
        when "t1", "a1", "wa1", "r1"
            "r4"
        when "t2", "a2", "wa2"
            "r5"
        when "t3", "a3", "wa3"
            "r6"
        when "t4", "a4", "wa4"
            "r7"
        when "t5", "a5", "wa5"
            "r8"
        when "t6", "a6", "wa6"
            "r9"
        when "t7", "a7", "wa7"
            "r10"
        when "ws0"
            "r11"
        when "ws1"
            "r12"
        when "csr0"
            "r14"
        when "csr1"
            "r15"
        when "csr2"
            "r16"
        when "csr3"
            "r17"
        when "csr4"
            "r18"
        when "csr5"
            "r19"
        when "csr6"
            "r20"
        when "csr7"
            "r21"
        when "csr8"
            "r22"
        when "csr9"
            "r23"
        when "csr10"
            "r24"
        when "cfr"
            "r31"
        when "sp"
            "r1"
        when "lr"
            # lr is a Special Purpose Register on PPC64 — cannot be named as a
            # plain operand in arithmetic instructions.  Callers that know lr is
            # the operand must emit mflr/mtlr manually.
            raise "ppc64le: lr is a SPR; handle with mflr/mtlr at call site (#{codeOriginString})"
        else
            raise "ppc64le: unknown GPR name '#{@name}' at #{codeOriginString}"
        end
    end
end

# -------------------------------------------------------------------------
# FPRegisterID operand → PPC64LE FP register name string
# -------------------------------------------------------------------------
class FPRegisterID
    def ppc64leOperand
        case @name
        when "ft0"
            "f0"
        when "ft1"
            "f9"
        when "ft2"
            "f10"
        when "ft3"
            "f11"
        when "ft4"
            "f12"
        when "ft5"
            "f13"
        when "fa0", "wfa0"
            "f1"
        when "fa1", "wfa1"
            "f2"
        when "fa2", "wfa2"
            "f3"
        when "fa3", "wfa3"
            "f4"
        when "fa4", "wfa4"
            "f5"
        when "fa5", "wfa5"
            "f6"
        when "fa6", "wfa6"
            "f7"
        when "fa7", "wfa7"
            "f8"
        when "csfr0"
            "f14"
        when "csfr1"
            "f15"
        when "csfr2"
            "f16"
        when "csfr3"
            "f17"
        when "csfr4"
            "f18"
        when "csfr5"
            "f19"
        when "csfr6"
            "f20"
        when "csfr7"
            "f21"
        else
            raise "ppc64le: unknown FPR name '#{@name}' at #{codeOriginString}"
        end
    end
end

# SpecialRegister (e.g. PPC64LE_EXTRA_GPRS entries) passes through its name.
class SpecialRegister
    def ppc64leOperand
        @name
    end
end

# -------------------------------------------------------------------------
# Immediate — validate range and emit decimal value
# -------------------------------------------------------------------------

# DS-form instructions (ld, std, lwa, stdu, …) require the displacement
# to be a 16-bit signed multiple of 4.
def ppc64leValidateDSOffset(value, origin)
    unless (-32768..32764).include?(value) && (value % 4 == 0)
        raise "ppc64le: DS-form offset #{value} not in [-32768..32764] aligned to 4 at #{origin}"
    end
end

# D-form instructions (lwz, stw, lbz, stb, …) require a 16-bit signed displacement.
def ppc64leValidateDOffset(value, origin)
    unless (-32768..32767).include?(value)
        raise "ppc64le: D-form offset #{value} not in [-32768..32767] at #{origin}"
    end
end

class Immediate
    def ppc64leOperand
        "#{value}"
    end

    # True if the value fits in a 16-bit signed immediate (for addi/li/etc.)
    def ppc64le16BitSignedImmediate?
        (-32768..32767).include?(value)
    end

    # True if the value fits in a 16-bit unsigned immediate (for ori/andi./etc.)
    def ppc64le16BitUnsignedImmediate?
        (0..65535).include?(value)
    end
end

# -------------------------------------------------------------------------
# Address operand → "offset(base)" string
# DS-form check (for ld/std) is deferred to the instruction emitter.
# -------------------------------------------------------------------------
class Address
    def ppc64leOperand
        "#{offset.value}(#{base.ppc64leOperand})"
    end
end

class BaseIndex
    def ppc64leOperand
        raise "ppc64le: BaseIndex addressing not directly supported; must be lowered"
    end
end

# -------------------------------------------------------------------------
# Preprocessing pass — normalise forms that PPC64LE cannot encode directly.
# -------------------------------------------------------------------------
def ppc64leLowerMalformedAddresses(list)
    # TODO: split large offsets that exceed DS/D-form range into
    #   addis base, base, offset@ha; addi base, base, offset@l
    # For now, validate and let the assembler catch problems at build time.
    list
end

def ppc64leLowerLargeImmediates(list)
    # Convert any Immediate that doesn't fit in 16 bits into a pre-load via
    # a Tmp (scratch) register.  Only arithmetic "move imm, dest" paths need
    # this; load/store offsets are handled separately.
    newList = []
    list.each { |node|
        if node.is_a?(Instruction)
            case node.opcode
            when /^(move|movei|moveq|movep)$/
                src = node.operands[0]
                dst = node.operands[1]
                if src.is_a?(Immediate) && !src.ppc64le16BitSignedImmediate?
                    # Large immediate: use a ppc64le_li64 pseudo that the
                    # lowering stage expands to lis/ori/rldicr/oris/ori.
                    newList << Instruction.new(node.codeOrigin, "ppc64le_li64", node.operands)
                else
                    newList << node
                end
            else
                newList << node
            end
        else
            newList << node
        end
    }
    newList
end

def getModifiedListPPC64LE(list)
    list = ppc64leLowerLargeImmediates(list)
    list = ppc64leLowerMalformedAddresses(list)
    list
end

# -------------------------------------------------------------------------
# Helper: emit a 64-bit integer load into a named PPC64LE register.
# Uses the standard 5-instruction sequence:
#   lis   dest, value@highest
#   ori   dest, dest, value@higher
#   rldicr dest, dest, 32, 31
#   oris  dest, dest, value@h
#   ori   dest, dest, value@l
# -------------------------------------------------------------------------
def ppc64leEmitLI64(dest, value)
    $asm.puts "lis #{dest}, #{value}@highest"
    $asm.puts "ori #{dest}, #{dest}, #{value}@higher"
    $asm.puts "rldicr #{dest}, #{dest}, 32, 31"
    $asm.puts "oris #{dest}, #{dest}, #{value}@h"
    $asm.puts "ori #{dest}, #{dest}, #{value}@l"
end

# -------------------------------------------------------------------------
# Helper: emit a conditional branch.  Emits a compare then a bc instruction.
# size: :i (32-bit cmpw) or :q/:p (64-bit cmpd); signed: true/false
# -------------------------------------------------------------------------
def ppc64leEmitCompareAndBranch(cond, size, signed, ops)
    # ops: [src1, src2, label] or [src1, imm, label]
    ra  = ops[0].ppc64leOperand
    rbl = ops[1]
    lbl = ops[2].asmLabel

    if size == :i
        cmpOp  = signed ? "cmpwi"  : "cmplwi"
        cmpOpR = signed ? "cmpw"   : "cmplw"
    else  # :q or :p
        cmpOp  = signed ? "cmpdi"  : "cmpldi"
        cmpOpR = signed ? "cmpd"   : "cmpld"
    end

    if rbl.is_a?(Immediate)
        $asm.puts "#{cmpOp} #{ra}, #{rbl.value}"
    else
        $asm.puts "#{cmpOpR} #{ra}, #{rbl.ppc64leOperand}"
    end

    case cond
    when :eq  then $asm.puts "beq #{lbl}"
    when :neq then $asm.puts "bne #{lbl}"
    when :lt  then $asm.puts "blt #{lbl}"
    when :gt  then $asm.puts "bgt #{lbl}"
    when :le  then $asm.puts "ble #{lbl}"
    when :ge  then $asm.puts "bge #{lbl}"
    else
        raise "ppc64le: unknown compare condition #{cond}"
    end
end

# -------------------------------------------------------------------------
# Helper: 3-operand integer emit.
# offlineasm 2-op: "addp src, dest" → dest = dest + src → add dest, dest, src
# offlineasm 3-op: "addp src1, src2, dest" → dest = src1 + src2
# -------------------------------------------------------------------------
def ppc64leEmitArith(opcode3reg, opcode3imm, node)
    ops = node.operands
    case ops.length
    when 2
        dst = ops[1].ppc64leOperand
        src = ops[0]
        if src.is_a?(Immediate)
            $asm.puts "#{opcode3imm} #{dst}, #{dst}, #{src.value}"
        else
            $asm.puts "#{opcode3reg} #{dst}, #{dst}, #{src.ppc64leOperand}"
        end
    when 3
        dst = ops[2].ppc64leOperand
        s1  = ops[0].ppc64leOperand
        s2  = ops[1]
        if s2.is_a?(Immediate)
            $asm.puts "#{opcode3imm} #{dst}, #{s1}, #{s2.value}"
        else
            $asm.puts "#{opcode3reg} #{dst}, #{s1}, #{s2.ppc64leOperand}"
        end
    else
        raise "ppc64le: unexpected operand count #{ops.length} for #{node.opcode}"
    end
end

# -------------------------------------------------------------------------
# Instruction lowering — main dispatch
# -------------------------------------------------------------------------
class Instruction
    def lowerPPC64LE
        $asm.comment codeOriginString
        operands = self.operands
        case opcode

        # ------------------------------------------------------------------
        # Annotate / no-ops
        # ------------------------------------------------------------------
        when "tagReturnAddress", "untagReturnAddress", "untagReturnAddressWithoutMoving",
             "loadStoreFence", "memfence", "fence",
             "nop", "breakpoint"
            $asm.puts "nop"

        when "checkStackPointerAlignment"
            # Alignment check: NOP in optimised builds, crash on misalign.
            # For now always NOP; add assertion probe later if needed.

        # ------------------------------------------------------------------
        # Move
        # ------------------------------------------------------------------
        when "move", "movep", "moveq", "movei"
            src = operands[0]
            dst = operands[1]
            if src.is_a?(RegisterID) && src.name == "lr"
                $asm.puts "mflr #{dst.ppc64leOperand}"
            elsif dst.is_a?(RegisterID) && dst.name == "lr"
                $asm.puts "mtlr #{src.ppc64leOperand}"
            elsif src.is_a?(Immediate)
                v = src.value
                if src.ppc64le16BitSignedImmediate?
                    $asm.puts "li #{dst.ppc64leOperand}, #{v}"
                else
                    ppc64leEmitLI64(dst.ppc64leOperand, v)
                end
            else
                $asm.puts "mr #{dst.ppc64leOperand}, #{src.ppc64leOperand}"
            end

        when "ppc64le_li64"
            # Emitted by preprocessing for large immediates.
            dst = operands[1].ppc64leOperand
            val = operands[0].value
            ppc64leEmitLI64(dst, val)

        # ------------------------------------------------------------------
        # Arithmetic — add
        # ------------------------------------------------------------------
        when "addp", "addq", "addi"
            ppc64leEmitArith("add", "addi", self)

        when "addis"
            # addis is the immediate-shifted variant (value << 16)
            ppc64leEmitArith("add", "addis", self)

        # ------------------------------------------------------------------
        # Arithmetic — subtract
        # subp src, dest  →  dest = dest - src  →  subf dest, src, dest
        # subp A, B, dest →  dest = A - B       →  subf dest, B, A
        # ------------------------------------------------------------------
        when "subp", "subq", "subi"
            ops = operands
            case ops.length
            when 2
                dst = ops[1].ppc64leOperand
                src = ops[0]
                if src.is_a?(Immediate)
                    # dest -= imm  →  addi dest, dest, -imm
                    $asm.puts "addi #{dst}, #{dst}, #{-src.value}"
                else
                    $asm.puts "subf #{dst}, #{src.ppc64leOperand}, #{dst}"
                end
            when 3
                dst  = ops[2].ppc64leOperand
                src1 = ops[0].ppc64leOperand   # dest = src1 - src2
                src2 = ops[1]
                if src2.is_a?(Immediate)
                    $asm.puts "addi #{dst}, #{src1}, #{-src2.value}"
                else
                    $asm.puts "subf #{dst}, #{src2.ppc64leOperand}, #{src1}"
                end
            else
                raise "ppc64le: unexpected operand count for #{opcode}"
            end

        # ------------------------------------------------------------------
        # Arithmetic — multiply
        # mulq / mulp  →  mulld (64-bit multiply low)
        # muli          →  mullw or mulli (immediate)
        # ------------------------------------------------------------------
        when "mulp", "mulq"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "mulld #{dst}, #{dst}, #{src}"

        when "muli"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "mulli #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "mullw #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        # ------------------------------------------------------------------
        # Arithmetic — negate
        # ------------------------------------------------------------------
        when "negp", "negq", "negi"
            dst = operands[0].ppc64leOperand
            $asm.puts "neg #{dst}, #{dst}"

        # ------------------------------------------------------------------
        # Bitwise: and / or / xor / not
        # ------------------------------------------------------------------
        when "andp", "andq", "andi"
            ops = operands
            dst = ops.last.ppc64leOperand
            src = ops.first
            if src.is_a?(Immediate)
                # PPC andi. uses unsigned 16-bit immediate and sets CR0
                $asm.puts "andi. #{dst}, #{dst}, #{src.value}"
            else
                src2 = (ops.length == 3) ? ops[1].ppc64leOperand : dst
                $asm.puts "and #{dst}, #{src.ppc64leOperand}, #{src2}"
            end

        when "orp", "orq", "ori"
            ops = operands
            dst = ops.last.ppc64leOperand
            src = ops.first
            if src.is_a?(Immediate)
                $asm.puts "ori #{dst}, #{dst}, #{src.value}"
            else
                src2 = (ops.length == 3) ? ops[1].ppc64leOperand : dst
                $asm.puts "or #{dst}, #{src.ppc64leOperand}, #{src2}"
            end

        when "xorp", "xorq", "xori"
            ops = operands
            dst = ops.last.ppc64leOperand
            src = ops.first
            if src.is_a?(Immediate)
                $asm.puts "xori #{dst}, #{dst}, #{src.value}"
            else
                src2 = (ops.length == 3) ? ops[1].ppc64leOperand : dst
                $asm.puts "xor #{dst}, #{src.ppc64leOperand}, #{src2}"
            end

        when "notp"
            dst = operands[0].ppc64leOperand
            $asm.puts "nor #{dst}, #{dst}, #{dst}"

        # ------------------------------------------------------------------
        # Shifts
        # lshiftp/q/i src, dest  →  sldi/slwi dest, dest, src
        # rshiftp/q/i src, dest  →  srdi/srwi (logical) or srawi (arithmetic)
        # urshiftp/q/i src, dest →  srdi/srwi (logical unsigned)
        # ------------------------------------------------------------------
        when "lshiftp", "lshiftq"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "sldi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "sld #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "lshifti"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "slwi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "slw #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "rshiftp", "rshiftq"
            # Arithmetic (signed) right shift
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "sradi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "srad #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "rshifti"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "srawi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "sraw #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "urshiftp", "urshiftq"
            # Logical (unsigned) right shift
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "srdi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "srd #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "urshifti"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                $asm.puts "srwi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "srw #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        # ------------------------------------------------------------------
        # Bit-count
        # ------------------------------------------------------------------
        when "countLeadingZerosp", "countLeadingZerosq"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "cntlzd #{dst}, #{src}"

        when "countLeadingZerosi"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "cntlzw #{dst}, #{src}"

        when "countTrailingZerosp", "countTrailingZerosq"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "cnttzd #{dst}, #{src}"

        when "countTrailingZerosi"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "cnttzw #{dst}, #{src}"

        # ------------------------------------------------------------------
        # Zero/Sign extension
        # ------------------------------------------------------------------
        when "zxi2p", "zxi2q"
            # Zero-extend 32-bit → 64-bit: rldicl with SH=0, MB=32
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "rldicl #{dst}, #{src}, 0, 32"

        when "sxi2p", "sxi2q"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "extsw #{dst}, #{src}"

        when "sxb2i", "sxb2q", "sxb2p"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "extsb #{dst}, #{src}"

        when "sxh2i", "sxh2q", "sxh2p"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "extsh #{dst}, #{src}"

        # ------------------------------------------------------------------
        # Loads
        # ------------------------------------------------------------------
        when "loadp", "loadq"
            # 64-bit load (DS-form, offset must be 4-aligned)
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            if addr.is_a?(Address)
                ppc64leValidateDSOffset(addr.offset.value, codeOriginString)
                $asm.puts "ld #{dst}, #{addr.ppc64leOperand}"
            else
                raise "ppc64le: loadp/loadq requires Address operand at #{codeOriginString}"
            end

        when "loadi", "loadis"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            if addr.is_a?(Address)
                if opcode == "loadis"
                    # sign-extend 32-bit (DS-form)
                    ppc64leValidateDSOffset(addr.offset.value, codeOriginString)
                    $asm.puts "lwa #{dst}, #{addr.ppc64leOperand}"
                else
                    # zero-extend 32-bit (D-form, any 16-bit offset)
                    ppc64leValidateDOffset(addr.offset.value, codeOriginString)
                    $asm.puts "lwz #{dst}, #{addr.ppc64leOperand}"
                end
            else
                raise "ppc64le: loadi/loadis requires Address operand at #{codeOriginString}"
            end

        when "load8", "loadb"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lbz #{dst}, #{addr.ppc64leOperand}"

        when "loadbs", "load8SignedExtendTo32"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lbz #{dst}, #{addr.ppc64leOperand}"
            $asm.puts "extsb #{dst}, #{dst}"

        when "load16", "loadh"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lhz #{dst}, #{addr.ppc64leOperand}"

        when "loadhs", "load16SignedExtendTo32"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lha #{dst}, #{addr.ppc64leOperand}"

        # ------------------------------------------------------------------
        # Stores
        # ------------------------------------------------------------------
        when "storep", "storeq"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            if addr.is_a?(Address)
                ppc64leValidateDSOffset(addr.offset.value, codeOriginString)
                $asm.puts "std #{src}, #{addr.ppc64leOperand}"
            else
                raise "ppc64le: storep/storeq requires Address operand at #{codeOriginString}"
            end

        when "storei"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "stw #{src}, #{addr.ppc64leOperand}"

        when "store8", "storeb"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "stb #{src}, #{addr.ppc64leOperand}"

        when "store16", "storeh"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "sth #{src}, #{addr.ppc64leOperand}"

        # ------------------------------------------------------------------
        # Atomic loads/stores (for now: treated as regular loads/stores,
        # no lwsync/isync barriers added — revisit for full correctness)
        # ------------------------------------------------------------------
        when "loadLinkAcqp", "loadLinkAcqq"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDSOffset(addr.offset.value, codeOriginString)
            $asm.puts "ldarx #{dst}, 0, #{addr.base.ppc64leOperand}"

        when "storeCondRelp", "storeCondRelq"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            dst  = operands[2].ppc64leOperand
            $asm.puts "stdcx. #{src}, 0, #{addr.base.ppc64leOperand}"
            # dst receives 0 on success, 1 on failure: use mfcr + extract EQ bit
            $asm.puts "mfcr #{dst}"
            $asm.puts "rlwinm #{dst}, #{dst}, 3, 31, 31"  # EQ bit = CR0[EQ] = bit 2, shift to bit 31
            $asm.puts "xori #{dst}, #{dst}, 1"            # 0 = success, 1 = failure

        # ------------------------------------------------------------------
        # Load effective address
        # leap base + offset, dest
        # ------------------------------------------------------------------
        when "leap"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            if addr.is_a?(Address)
                off = addr.offset.value
                base = addr.base.ppc64leOperand
                if (-32768..32767).include?(off)
                    $asm.puts "addi #{dst}, #{base}, #{off}"
                else
                    raise "ppc64le: leap offset #{off} out of 16-bit range at #{codeOriginString}"
                end
            else
                raise "ppc64le: leap requires Address operand at #{codeOriginString}"
            end

        # ------------------------------------------------------------------
        # Stack: push and pop
        #
        # push reg           — single register (8-byte, may temporarily misalign)
        # push reg1, reg2    — paired push (16-byte, maintains alignment)
        #   "push cfr, lr"  — the common prologue form on PPC64LE:
        #       mflr r0 ; stdu r1, -16(r1) ; std r31, 0(r1) ; std r0, 8(r1)
        #   "push regA, regB" (no lr):
        #       stdu r1, -16(r1) ; std regA, 0(r1) ; std regB, 8(r1)
        #
        # pop reg1, reg2     — paired pop (reverse of push)
        # pop reg            — single register
        #
        # Frame layout after "push cfr, lr; move sp, cfr":
        #   cfr[0]  = old cfr  (CallerFrameAndPC::callerFrame)
        #   cfr[8]  = saved lr (CallerFrameAndPC::returnPC)
        # ------------------------------------------------------------------
        when "push"
            if operands.length == 1
                reg = operands[0]
                if reg.is_a?(RegisterID) && reg.name == "lr"
                    # push lr: save lr to r0, allocate 8 bytes, store
                    $asm.puts "mflr r0"
                    $asm.puts "stdu r1, -8(r1)"
                    $asm.puts "std r0, 0(r1)"
                else
                    $asm.puts "stdu r1, -8(r1)"
                    $asm.puts "std #{reg.ppc64leOperand}, 0(r1)"
                end
            elsif operands.length == 2
                r1op = operands[0]
                r2op = operands[1]
                lr1  = r1op.is_a?(RegisterID) && r1op.name == "lr"
                lr2  = r2op.is_a?(RegisterID) && r2op.name == "lr"
                if lr1 || lr2
                    # One of the operands is lr; save it into r0 first.
                    $asm.puts "mflr r0"
                    $asm.puts "stdu r1, -16(r1)"
                    $asm.puts "std #{lr1 ? 'r0' : r1op.ppc64leOperand}, 0(r1)"
                    $asm.puts "std #{lr2 ? 'r0' : r2op.ppc64leOperand}, 8(r1)"
                else
                    $asm.puts "stdu r1, -16(r1)"
                    $asm.puts "std #{r1op.ppc64leOperand}, 0(r1)"
                    $asm.puts "std #{r2op.ppc64leOperand}, 8(r1)"
                end
            else
                raise "ppc64le: push with #{operands.length} operands not supported"
            end

        when "pop"
            if operands.length == 1
                reg = operands[0]
                if reg.is_a?(RegisterID) && reg.name == "lr"
                    $asm.puts "ld r0, 0(r1)"
                    $asm.puts "addi r1, r1, 8"
                    $asm.puts "mtlr r0"
                else
                    $asm.puts "ld #{reg.ppc64leOperand}, 0(r1)"
                    $asm.puts "addi r1, r1, 8"
                end
            elsif operands.length == 2
                r1op = operands[0]
                r2op = operands[1]
                lr1  = r1op.is_a?(RegisterID) && r1op.name == "lr"
                lr2  = r2op.is_a?(RegisterID) && r2op.name == "lr"
                # pop R1, R2: load R1 from [sp+8], load R2 from [sp+0], sp += 16
                # (Reverse of ARM64 ldp: R1 gets [sp+8], R2 gets [sp+0])
                # This restores "push cfr, lr": cfr from [sp+0], lr from [sp+8]
                # Note: pop is reversed relative to push per offlineasm convention.
                slot0 = r2op  # r2op was pushed to [sp+0]
                slot8 = r1op  # r1op was pushed to [sp+8]
                if slot0.is_a?(RegisterID) && slot0.name == "lr"
                    $asm.puts "ld r0, 0(r1)"
                else
                    $asm.puts "ld #{slot0.ppc64leOperand}, 0(r1)"
                end
                if slot8.is_a?(RegisterID) && slot8.name == "lr"
                    $asm.puts "ld r0, 8(r1)"
                else
                    $asm.puts "ld #{slot8.ppc64leOperand}, 8(r1)"
                end
                $asm.puts "addi r1, r1, 16"
                if slot0.is_a?(RegisterID) && slot0.name == "lr"
                    $asm.puts "mtlr r0"
                elsif slot8.is_a?(RegisterID) && slot8.name == "lr"
                    $asm.puts "mtlr r0"
                end
            else
                raise "ppc64le: pop with #{operands.length} operands not supported"
            end

        # ------------------------------------------------------------------
        # Control flow
        # ------------------------------------------------------------------
        when "ret"
            $asm.puts "blr"

        when "jmp"
            op = operands[0]
            if op.is_a?(RegisterID)
                $asm.puts "mtctr #{op.ppc64leOperand}"
                $asm.puts "bctr"
            elsif op.is_a?(LabelReference) || op.is_a?(LocalLabelReference)
                $asm.puts "b #{op.asmLabel}"
            else
                raise "ppc64le: jmp operand type #{op.class} not supported at #{codeOriginString}"
            end

        when "call"
            op = operands[0]
            if op.is_a?(RegisterID)
                $asm.puts "mtctr #{op.ppc64leOperand}"
                $asm.puts "bctrl"
            elsif op.is_a?(LabelReference) || op.is_a?(LocalLabelReference)
                $asm.puts "bl #{op.asmLabel}"
            else
                raise "ppc64le: call operand type #{op.class} not supported at #{codeOriginString}"
            end

        # ------------------------------------------------------------------
        # PC-relative address load (for dispatch table setup)
        #
        # pcrtoaddr label, dest:
        #   mflr r0           # save return address
        #   bl 1f             # LR = address of 1: (next instruction)
        #   1: mflr dest      # dest = address of 1:
        #   mtlr r0           # restore return address
        #   addis dest, dest, (label - 1b)@ha
        #   addi dest, dest, (label - 1b)@l
        # ------------------------------------------------------------------
        when "pcrtoaddr"
            lbl  = operands[0].asmLabel
            dest = operands[1].ppc64leOperand
            $asm.puts "mflr r0"
            $asm.puts "bl 1f"
            $asm.puts "1:"
            $asm.puts "mflr #{dest}"
            $asm.puts "mtlr r0"
            $asm.puts "addis #{dest}, #{dest}, (#{lbl} - 1b)@ha"
            $asm.puts "addi #{dest}, #{dest}, (#{lbl} - 1b)@l"

        # globaladdr label, dest — like pcrtoaddr but for globally visible symbols
        when "globaladdr"
            lbl  = operands[0].asmLabel
            dest = operands[1].ppc64leOperand
            $asm.puts "mflr r0"
            $asm.puts "bl 1f"
            $asm.puts "1:"
            $asm.puts "mflr #{dest}"
            $asm.puts "mtlr r0"
            $asm.puts "addis #{dest}, #{dest}, (#{lbl} - 1b)@ha"
            $asm.puts "addi #{dest}, #{dest}, (#{lbl} - 1b)@l"

        # ------------------------------------------------------------------
        # Branch-on-test-bit-zero / non-zero
        # btiz  src, label  — branch if src == 0 (test integer zero)
        # btinz src, label  — branch if src != 0
        # btpz  src, label  — pointer-width zero test
        # btpnz src, label
        # btqz  src, label  — quad-word zero test
        # btqnz src, label
        # btz   src, label  — generic zero test (treat as btpz)
        # btnz  src, label  — generic non-zero test
        # ------------------------------------------------------------------
        when "btiz", "btpz", "btqz", "btz"
            reg = operands[0].ppc64leOperand
            lbl = operands[1].asmLabel
            $asm.puts "cmpdi #{reg}, 0"
            $asm.puts "beq #{lbl}"

        when "btinz", "btpnz", "btqnz", "btnz"
            reg = operands[0].ppc64leOperand
            lbl = operands[1].asmLabel
            $asm.puts "cmpdi #{reg}, 0"
            $asm.puts "bne #{lbl}"

        # Test bit N: btbz reg, imm, label — branch if bit N of reg is zero
        when "btbz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rldicl. r0, #{reg}, 64 - #{bit}, 63"
            $asm.puts "beq #{lbl}"

        when "btbnz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rldicl. r0, #{reg}, 64 - #{bit}, 63"
            $asm.puts "bne #{lbl}"

        # Test signed bit (same as btbz/btbnz for PPC64LE):
        when "btbiz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rlwinm. r0, #{reg}, 0, #{bit}, #{bit}"
            $asm.puts "beq #{lbl}"

        when "btbinz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rlwinm. r0, #{reg}, 0, #{bit}, #{bit}"
            $asm.puts "bne #{lbl}"

        # ------------------------------------------------------------------
        # Integer compare-and-branch
        # b{i,q,p}{eq,neq,lt,gt,le,ge} src1, src2, label
        # signed variants: bilt, bigt, bile, bige
        # unsigned: bult, bugt, bule, buge (use cmpldi)
        # b{i,q,p}beq, b{i,q,p}baeq — above/below (unsigned)
        # ------------------------------------------------------------------
        when "bieq"
            ppc64leEmitCompareAndBranch(:eq, :i, true, operands)
        when "bineq"
            ppc64leEmitCompareAndBranch(:neq, :i, true, operands)
        when "bilt"
            ppc64leEmitCompareAndBranch(:lt, :i, true, operands)
        when "bigt"
            ppc64leEmitCompareAndBranch(:gt, :i, true, operands)
        when "bile"
            ppc64leEmitCompareAndBranch(:le, :i, true, operands)
        when "bige"
            ppc64leEmitCompareAndBranch(:ge, :i, true, operands)
        when "bult", "bib"
            ppc64leEmitCompareAndBranch(:lt, :i, false, operands)
        when "bugt", "bia"
            ppc64leEmitCompareAndBranch(:gt, :i, false, operands)
        when "bule", "bibe"
            ppc64leEmitCompareAndBranch(:le, :i, false, operands)
        when "buge", "biaeq"
            ppc64leEmitCompareAndBranch(:ge, :i, false, operands)

        when "bqeq", "bpeq"
            ppc64leEmitCompareAndBranch(:eq, :q, true, operands)
        when "bqneq", "bpneq"
            ppc64leEmitCompareAndBranch(:neq, :q, true, operands)
        when "bqlt", "bplt"
            ppc64leEmitCompareAndBranch(:lt, :q, true, operands)
        when "bqgt", "bpgt"
            ppc64leEmitCompareAndBranch(:gt, :q, true, operands)
        when "bqle", "bple"
            ppc64leEmitCompareAndBranch(:le, :q, true, operands)
        when "bqge", "bpge"
            ppc64leEmitCompareAndBranch(:ge, :q, true, operands)
        when "bqult", "bpult", "bqb", "bpb"
            ppc64leEmitCompareAndBranch(:lt, :q, false, operands)
        when "bqugt", "bpugt", "bqa", "bpa"
            ppc64leEmitCompareAndBranch(:gt, :q, false, operands)
        when "bqule", "bpule", "bqbe", "bpbe"
            ppc64leEmitCompareAndBranch(:le, :q, false, operands)
        when "bquge", "bpuge", "bqaeq", "bpaeq"
            ppc64leEmitCompareAndBranch(:ge, :q, false, operands)

        when "bbieq", "bbbeq", "bbaeq"
            ppc64leEmitCompareAndBranch(:eq, :b, true, operands)
        when "bbineq", "bbbneq", "bbaneq"
            ppc64leEmitCompareAndBranch(:neq, :b, true, operands)

        # ------------------------------------------------------------------
        # Add-with-branch-on-overflow
        # ------------------------------------------------------------------
        when "baddis", "baddio"
            ops  = operands
            # baddis src, dst, label  — branch if src + dst overflows (signed)
            src = ops[0].is_a?(Immediate) ? ops[0].value.to_s : ops[0].ppc64leOperand
            dst = ops[1].ppc64leOperand
            lbl = ops[2].asmLabel
            if ops[0].is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{src}"
            else
                $asm.puts "addo. #{dst}, #{dst}, #{src}"
            end
            $asm.puts (opcode == "baddis") ? "bns #{lbl}" : "bso #{lbl}"

        when "baddinz"
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "add. #{dst}, #{dst}, #{src.ppc64leOperand}"
            end
            $asm.puts "bne #{lbl}"

        # ------------------------------------------------------------------
        # Floating-point operations (basic)
        # ------------------------------------------------------------------
        when "addf", "addd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fadd #{dst}, #{dst}, #{src}"

        when "subf", "subd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fsub #{dst}, #{dst}, #{src}"

        when "mulf", "muld"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fmul #{dst}, #{dst}, #{src}"

        when "divf", "divd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fdiv #{dst}, #{dst}, #{src}"

        when "absf", "absd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fabs #{dst}, #{src}"

        when "negf", "negd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fneg #{dst}, #{src}"

        when "sqrtf"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fsqrts #{dst}, #{src}"

        when "sqrtd"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fsqrt #{dst}, #{src}"

        when "loadf"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            $asm.puts "lfs #{dst}, #{addr.ppc64leOperand}"

        when "loadd"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            $asm.puts "lfd #{dst}, #{addr.ppc64leOperand}"

        when "storef"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            $asm.puts "stfs #{src}, #{addr.ppc64leOperand}"

        when "stored"
            src  = operands[0].ppc64leOperand
            addr = operands[1]
            $asm.puts "stfd #{src}, #{addr.ppc64leOperand}"

        when "moved"
            dst = operands[1].ppc64leOperand
            src = operands[0].ppc64leOperand
            $asm.puts "fmr #{dst}, #{src}"

        # Integer-to-float conversions
        when "ci2d", "ci2ds"
            # Convert integer (in GPR) to double (in FPR): requires store/load roundtrip
            # via stack scratch area.  Use a simple 16-byte stack allocation.
            src  = operands[0].ppc64leOperand
            dst  = operands[1].ppc64leOperand
            $asm.puts "stdu r1, -16(r1)"
            $asm.puts "std #{src}, 0(r1)"
            $asm.puts "lfd #{dst}, 0(r1)"
            $asm.puts "addi r1, r1, 16"
            $asm.puts "fcfid #{dst}, #{dst}"

        when "cd2i", "truncated2is"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu r1, -16(r1)"
            $asm.puts "fctiwz #{src}, #{src}"
            $asm.puts "stfd #{src}, 0(r1)"
            $asm.puts "lwz #{dst}, 4(r1)"   # lower 32 bits of FP word (big-endian)
            $asm.puts "addi r1, r1, 16"

        # Double-to-float truncation and double-to-double
        when "fd2q"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu r1, -16(r1)"
            $asm.puts "stfd #{src}, 0(r1)"
            $asm.puts "ld #{dst}, 0(r1)"
            $asm.puts "addi r1, r1, 16"

        when "fq2d"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu r1, -16(r1)"
            $asm.puts "std #{src}, 0(r1)"
            $asm.puts "lfd #{dst}, 0(r1)"
            $asm.puts "addi r1, r1, 16"

        # ------------------------------------------------------------------
        # FP compare and branch
        # ------------------------------------------------------------------
        when "bdequn", "bfequn", "bdnequn", "bfnequn"
            a = operands[0].ppc64leOperand
            b = operands[1].ppc64leOperand
            l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"
            $asm.puts(opcode =~ /^bd?eq/ ? "beq #{l}" : "bne #{l}")

        when "bdlt", "bflt"
            a = operands[0].ppc64leOperand; b = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"; $asm.puts "blt #{l}"

        when "bdlteq", "bflteq"
            a = operands[0].ppc64leOperand; b = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"; $asm.puts "ble #{l}"

        when "bdgt", "bfgt"
            a = operands[0].ppc64leOperand; b = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"; $asm.puts "bgt #{l}"

        when "bdgteq", "bfgteq"
            a = operands[0].ppc64leOperand; b = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"; $asm.puts "bge #{l}"

        when "bdnequn", "bfnequn"
            a = operands[0].ppc64leOperand; b = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu 0, #{a}, #{b}"; $asm.puts "bne #{l}"

        # ------------------------------------------------------------------
        # Misc
        # ------------------------------------------------------------------
        when "crash"
            $asm.puts ".int 0xbadbeef0"   # illegal instruction trap

        when "break"
            $asm.puts ".int 0xbadbeef0"

        when "unimplemented"
            $asm.puts ".int 0xbadbeef0"

        # ------------------------------------------------------------------
        # Unhandled — emit an error comment and a crash trap so the build
        # fails loudly rather than silently generating wrong code.
        # ------------------------------------------------------------------
        else
            raise "ppc64le: unhandled opcode '#{opcode}' at #{codeOriginString}"
        end
    end
end
