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
#   r11  => ws0, t9  (volatile scratch; t9=ws0 per LLInt const ws0 = t9)
#   r12  => ws1, t10 (volatile scratch; t10=ws1 per LLInt const ws1 = t10)
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
#   r25  => ws2, t11 (callee-saved; preserved by C; t11=ws2 per LLInt const ws2 = t11)
#   r26  => ws3, t12 (callee-saved; preserved by C; t12=ws3 per LLInt const ws3 = t12)
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
    SpecialRegister.new("r27"),
    SpecialRegister.new("r28"),
    SpecialRegister.new("r29"),
    SpecialRegister.new("r30"),
]
PPC64LE_EXTRA_FPRS = [
    SpecialRegister.new("f24"),
    SpecialRegister.new("f25"),
    SpecialRegister.new("f26"),
    SpecialRegister.new("f27"),
]

# -------------------------------------------------------------------------
# PtrTag-based call classification.
#
# Offlineasm resolves `constexpr FooPtrTag` to a bare Immediate before the
# backend lowering runs, so the lowering cannot see tag NAMES.  Capture the
# name -> value mapping as ConstExpr nodes are resolved (transform.rb
# Node#resolve -> ConstExpr#resolveOffsets) and classify call sites
# symbolically.  Never replicate WTF's makePtrTagHash here: a first attempt
# did exactly that, produced values matching no real tag, silently
# classified every call as JS, and the missing r2 (TOC) restore after host
# C calls crashed the first opcode dispatch after every host-call return
# (dispatch loads the opcode table TOC-relative: `ld rX, off(r2)`).
#
# JS-entry tags: the callee is LLInt/JIT code following the JSC calling
# convention -- the caller has already arranged sp for the callee's frame,
# the callee never runs an ELFv2 GEP, never touches r2, and stores the
# return PC itself.  Emit a bare mtctr/bctrl; any extra stack adjustment
# here would shift the callee's cfr and corrupt the frame header slots.
#
# Everything else (HostFunctionPtrTag, CustomAccessorPtrTag, and the
# untagged `call reg` sites from the cCall*/slow-path macros) is a C call
# under the full ELFv2 ABI: the callee may run its GEP off r12 and
# overwrite r2 (host functions live in the jsc BINARY -- a different
# module with a different TOC), and writes its saved lr at 16(caller_sp).
# Emit a 32-byte linkage frame, set r12, and restore r2 after return
# (ELFv2 rev 1.5 secs 2.2.2-2.2.3, 2.4.2).
# -------------------------------------------------------------------------
$ppc64leConstExprValues = {}
module PPC64LEConstExprCapture
    def resolveOffsets(constantsMap)
        result = super
        $ppc64leConstExprValues[@value] = result.value if result.is_a?(Immediate)
        result
    end
end
class ConstExpr
    prepend PPC64LEConstExprCapture
end

PPC64LE_JS_ENTRY_TAG_NAMES = %w[
    JSEntryPtrTag JSEntrySlowPathPtrTag WasmEntryPtrTag LLIntToWasmEntryPtrTag
    wasmIPIntTailCallWasmEntryPtrTag
].freeze

PPC64LE_C_CALL_TAG_NAMES = %w[
    HostFunctionPtrTag CustomAccessorPtrTag OperationPtrTag NoPtrTag
].freeze

# Returns [:js, tagName] or [:c, tagName-or-nil]; raises on a tag value that
# matches neither list so a new tag fails at offlineasm time instead of
# miscompiling to the wrong calling convention.
def ppc64leClassifyCallTag(tagValue, whereString)
    return [:c, nil] unless tagValue
    jsName = PPC64LE_JS_ENTRY_TAG_NAMES.find { |n| $ppc64leConstExprValues[n] == tagValue }
    cName  = PPC64LE_C_CALL_TAG_NAMES.find { |n| $ppc64leConstExprValues[n] == tagValue }
    if jsName and cName
        raise "ppc64le: PtrTag hash collision between #{jsName} and #{cName} (#{tagValue}) at #{whereString}"
    end
    return [:js, jsName] if jsName
    return [:c, cName] if cName
    known = $ppc64leConstExprValues.select { |_, v| v == tagValue }.keys
    raise "ppc64le: call with unclassified PtrTag value #{tagValue} (#{known.empty? ? 'no name captured' : known.join('/')}) at #{whereString}"
end

# -------------------------------------------------------------------------
# RegisterID operand → PPC64LE register number (bare numeral).
# PPC GAS does not accept "rN" register names by default (without -mregnames),
# so we emit bare numerals like "1" for r1, matching GCC's convention.
# -------------------------------------------------------------------------
class RegisterID
    def ppc64leOperand
        case @name
        when "t0", "a0", "wa0", "r0"
            "3"
        when "t1", "a1", "wa1", "r1"
            "4"
        when "t2", "a2", "wa2"
            "5"
        when "t3", "a3", "wa3"
            "6"
        when "t4", "a4", "wa4"
            "7"
        when "t5", "a5", "wa5"
            "8"
        when "t6", "a6", "wa6"
            "9"
        when "t7", "a7", "wa7"
            "10"
        when "ws0", "t9"     # t9 = ws0 per LowLevelInterpreter.asm const ws0 = t9
            "11"
        when "ws1", "t10"    # t10 = ws1 per LowLevelInterpreter.asm const ws1 = t10
            "12"
        when "ws2", "t11"    # t11 = ws2; callee-saved, C preserves it across calls
            "25"
        when "ws3", "t12"    # t12 = ws3; callee-saved, C preserves it across calls
            "26"
        when "csr0"
            "14"
        when "csr1"
            "15"
        when "csr2"
            "16"
        when "csr3"
            "17"
        when "csr4"
            "18"
        when "csr5"
            "19"
        when "csr6"
            "20"
        when "csr7"
            "21"
        when "csr8"
            "22"
        when "csr9"
            "23"
        when "csr10"
            "24"
        when "cfr"
            "31"
        when "sp"
            "1"
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
            "0"
        when "ft1"
            "9"
        when "ft2"
            "10"
        when "ft3"
            "11"
        when "ft4"
            "12"
        when "ft5"
            "13"
        when "fa0", "wfa0"
            "1"
        when "fa1", "wfa1"
            "2"
        when "fa2", "wfa2"
            "3"
        when "fa3", "wfa3"
            "4"
        when "fa4", "wfa4"
            "5"
        when "fa5", "wfa5"
            "6"
        when "fa6", "wfa6"
            "7"
        when "fa7", "wfa7"
            "8"
        when "ft6"    # wfa6 alias on 8-arg platforms; callee-saved on PPC64LE ELFv2
            "22"
        when "ft7"    # wfa7 alias on 8-arg platforms
            "23"
        when "csfr0"
            "14"
        when "csfr1"
            "15"
        when "csfr2"
            "16"
        when "csfr3"
            "17"
        when "csfr4"
            "18"
        when "csfr5"
            "19"
        when "csfr6"
            "20"
        when "csfr7"
            "21"
        else
            raise "ppc64le: unknown FPR name '#{@name}' at #{codeOriginString}"
        end
    end
end

# SpecialRegister (e.g. PPC64LE_EXTRA_GPRS entries with names like "r27").
# Strip the leading "r"/"f" so we emit bare numerals (GAS convention).
class SpecialRegister
    def ppc64leOperand
        @name.sub(/^[rf]/, "")
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
        # base.ppc64leOperand returns a bare numeral; emit GAS-syntax "OFFSET(BASE)".
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
    # Lower BaseIndex addressing modes that PPC64 cannot encode directly.
    # Pattern: base + index * (1 << scaleShift) + offset
    # Emitted as: tmp = index << scaleShift; tmp += offset; tmp += base; use Address(tmp,0)
    #
    # Also lower jmp/call with Address operand:
    #   jmp [base+offset]  →  loadp [base+offset], tmp; jmp tmp
    #   call [base+offset] →  loadp [base+offset], tmp; call tmp
    newList = []
    list.each { |node|
        next (newList << node) unless node.is_a?(Instruction)

        ops = node.operands

        # jmp/call with an Address target — load the pointer into a Tmp first.
        # Preserve any extra operands (e.g. PtrTag) so the lowering can still
        # distinguish C calls from JS calls based on HostFunctionPtrTag.
        if (node.opcode == "jmp" || node.opcode == "call") && ops[0].is_a?(Address)
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "loadp", [ops[0], tmp])
            newList << Instruction.new(co, node.opcode, [tmp] + ops[1..], node.annotation)
            next
        end

        # orh src, Address — read-modify-write: load halfword, OR, store back
        # PPC has no memory-OR instruction; expand to loadh / ori / storeh with a Tmp.
        if node.opcode == "orh" && ops[1].is_a?(Address)
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "loadh",  [ops[1], tmp])
            newList << Instruction.new(co, "ori",    [ops[0], tmp])
            newList << Instruction.new(co, "storeh", [tmp, ops[1]])
            next
        end

        # Compare-and-branch with an Address as first operand:
        # riscLowerTest expands btpz addr, label → bpeq addr, 0, label.
        # PPC cmp* requires a register; load from the address into a Tmp first.
        if node.opcode =~ /\Ab[ipqb]/ && ops[0].is_a?(Address)
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            load_op = case node.opcode[1]
                      when "p" then "loadp"
                      when "q" then "loadq"
                      when "i" then "loadi"
                      when "b" then "loadb"
                      else          "loadp"
                      end
            newList << Instruction.new(co, load_op, [ops[0], tmp])
            newList << Instruction.new(co, node.opcode, [tmp] + ops[1..], node.annotation)
            next
        end

        # Compare-and-branch with an Address as second operand (3-operand form):
        # e.g. bpbeq reg, addr, label.  Per Power ISA v2.07B §3.3.10, cmp/cmpl/cmpd/cmpld
        # are X-form and require two GPRs.  Load the memory operand into a Tmp first.
        # The /\Ab[ipqb]/ regex excludes badd*/bsub* (read-modify-write), bt* (test-and-branch),
        # and bd*/bf* (FP), so this only fires on integer compare-and-branch.
        if node.opcode =~ /\Ab[ipqb]/ && ops.length >= 3 && ops[1].is_a?(Address)
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            load_op = case node.opcode[1]
                      when "p" then "loadp"
                      when "q" then "loadq"
                      when "i" then "loadi"
                      when "b" then "loadb"
                      else          "loadp"
                      end
            newList << Instruction.new(co, load_op, [ops[1], tmp])
            newList << Instruction.new(co, node.opcode, [ops[0], tmp] + ops[2..], node.annotation)
            next
        end

        # transferp/transferq src_addr, dst_addr — memory-to-memory pointer copy via Tmp.
        if node.opcode == "transferp" || node.opcode == "transferq"
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "loadp",  [ops[0], tmp])
            newList << Instruction.new(co, "storep", [tmp, ops[1]])
            next
        end

        # baddis imm, addr, label — read-modify-write counter then branch if (signed)
        # negative.  PPC has no fused arith-and-branch on memory; decompose into:
        #   loadi  addr, tmp
        #   addi   imm,  tmp        ; plain add, no flags (per Power ISA v2.07B §3.3.9)
        #   storei tmp,  addr
        #   bilt   tmp, 0, label    ; cmpwi+blt (signed 32-bit < 0)
        # This is semantically equivalent to "add to memory, branch if result < 0".
        # Only baddis (32-bit signed) is exercised by current LLInt sources; pointer/
        # quad and bsub variants will be added on demand.
        if node.opcode == "baddis" && ops[1].is_a?(Address)
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "loadi",  [ops[1], tmp])
            newList << Instruction.new(co, "addi",   [ops[0], tmp])
            newList << Instruction.new(co, "storei", [tmp, ops[1]])
            newList << Instruction.new(co, "bilt",   [tmp, Immediate.new(co, 0), ops[2]], node.annotation)
            next
        end

        # load* with lr as destination: PPC LR is an SPR — must use mtlr.
        # Expand: load addr, lr  →  load addr, tmp; move tmp, lr
        if node.opcode =~ /\Aload/ && ops[-1].is_a?(RegisterID) && ops[-1].name == "lr"
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, node.opcode, ops[0..-2] + [tmp], node.annotation)
            newList << Instruction.new(co, "move", [tmp, ops[-1]])
            next
        end

        # store* with lr as source: must use mflr first.
        # Expand: store lr, addr  →  move lr, tmp; store tmp, addr
        if node.opcode =~ /\Astore/ && ops[0].is_a?(RegisterID) && ops[0].name == "lr"
            co  = node.codeOrigin
            tmp = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "move", [ops[0], tmp])
            newList << Instruction.new(co, node.opcode, [tmp] + ops[1..], node.annotation)
            next
        end

        # DS-form opcodes (ld/lwa/std) require 4-byte-aligned offsets.
        # Also catch any offset outside 16-bit signed range.
        # Split: ppc64le_li64 offset, tmp; addp base, tmp; Address(tmp, 0)
        large_addr_pos = ops.each_with_index.find { |op, _|
            next false unless op.is_a?(Address) && op.offset.is_a?(Immediate)
            v = op.offset.value
            ds_form = %w[loadp loadq loadis storep storeq].include?(node.opcode)
            (v < -32768 || v > 32767) || (ds_form && (v % 4) != 0)
        }&.last

        if large_addr_pos
            addr = ops[large_addr_pos]
            co   = node.codeOrigin
            tmp  = Tmp.new(co, :gpr)
            # ppc64le_li64 handles any 64-bit value; addp adds the base
            newList << Instruction.new(co, "ppc64le_li64", [addr.offset, tmp])
            newList << Instruction.new(co, "addp", [addr.base, tmp])
            new_addr = Address.new(co, tmp, Immediate.new(co, 0))
            new_ops  = ops.dup
            new_ops[large_addr_pos] = new_addr
            newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
            next
        end

        # Detect which operand position holds a BaseIndex, if any.
        bi_pos = ops.each_with_index.find { |op, _| op.is_a?(BaseIndex) }&.last

        unless bi_pos
            newList << node
            next
        end

        bi  = ops[bi_pos]
        co  = node.codeOrigin
        tmp = Tmp.new(co, :gpr)

        # tmp = index
        newList << Instruction.new(co, "movep", [bi.index, tmp])
        # tmp = tmp << scaleShift
        if bi.scaleShift > 0
            newList << Instruction.new(co, "lshiftq",
                [Immediate.new(co, bi.scaleShift), tmp])
        end
        # tmp += const offset
        if bi.offset.value != 0
            newList << Instruction.new(co, "addp", [bi.offset, tmp])
        end
        # tmp += base
        newList << Instruction.new(co, "addp", [bi.base, tmp])

        # Replace the BaseIndex with Address(tmp, 0)
        new_addr = Address.new(co, tmp, Immediate.new(co, 0))
        new_ops  = ops.dup
        new_ops[bi_pos] = new_addr

        newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
    }
    newList
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
            when /^and[ipq]$/, /^or[ipq]$/, /^xor[ipq]$/
                # PPC andi./ori/xori take 16-bit UNSIGNED immediates (Power ISA v2.07B
                # §3.3.13).  If the immediate doesn't fit, materialize it via
                # ppc64le_li64 into a Tmp and rewrite to use the register-form op.
                ops = node.operands
                imm_idx = ops.index { |o| o.is_a?(Immediate) }
                if imm_idx && !ops[imm_idx].ppc64le16BitUnsignedImmediate?
                    co  = node.codeOrigin
                    tmp = Tmp.new(co, :gpr)
                    newList << Instruction.new(co, "ppc64le_li64", [ops[imm_idx], tmp])
                    new_ops = ops.dup
                    new_ops[imm_idx] = tmp
                    newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
                else
                    newList << node
                end
            when /^add[ipq]$/
                # PPC addi RT,RA,SI takes 16-bit SIGNED immediate (Power ISA v2.07B §3.3.9).
                # Materialize larger constants via Tmp and use register-form add.
                ops = node.operands
                imm_idx = ops.index { |o| o.is_a?(Immediate) }
                if imm_idx && !ops[imm_idx].ppc64le16BitSignedImmediate?
                    co  = node.codeOrigin
                    tmp = Tmp.new(co, :gpr)
                    newList << Instruction.new(co, "ppc64le_li64", [ops[imm_idx], tmp])
                    new_ops = ops.dup
                    new_ops[imm_idx] = tmp
                    newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
                else
                    newList << node
                end
            when /^store[bhipq]$/
                # PPC st{b,h,w,d} require a GPR source.  An Immediate operand[0]
                # is invalid encoding; materialize via Tmp.
                ops = node.operands
                if ops[0].is_a?(Immediate)
                    co  = node.codeOrigin
                    tmp = Tmp.new(co, :gpr)
                    newList << Instruction.new(co, "ppc64le_li64", [ops[0], tmp])
                    newList << Instruction.new(co, node.opcode, [tmp, ops[1]], node.annotation)
                else
                    newList << node
                end
            when /^sub[ipq]$/
                # subi imm, dest is lowered to addi dest, dest, -imm.  Materialize when
                # NEGATED value doesn't fit in 16-bit signed (e.g. imm = 0x40000000).
                ops = node.operands
                imm_idx = ops.index { |o| o.is_a?(Immediate) }
                if imm_idx
                    neg = -ops[imm_idx].value
                    if neg < -32768 || neg > 32767
                        co  = node.codeOrigin
                        tmp = Tmp.new(co, :gpr)
                        newList << Instruction.new(co, "ppc64le_li64", [ops[imm_idx], tmp])
                        new_ops = ops.dup
                        new_ops[imm_idx] = tmp
                        newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
                        next
                    end
                end
                newList << node
            when /^b[ipqb][a-z]+\z/
                # Compare-and-branch (3-operand form) lowering uses cmpdi/cmpwi which take
                # 16-bit signed immediates, or cmpldi/cmplwi which take 16-bit unsigned
                # (Power ISA v2.07B §3.3.10).  If ops[1] is an out-of-range Immediate,
                # materialize and let the lowering pick the register-form compare.
                # Skip 2-operand bt*z forms (only one register operand).
                ops = node.operands
                if ops.length == 3 && ops[1].is_a?(Immediate)
                    imm = ops[1]
                    # Determine signedness by mnemonic suffix.  Unsigned variants:
                    # bub, bua, bube, buae, bib, bia, bibe, biaeq, bpb, bpa, bpbe, bpbeq, bpaeq,
                    # bbb, bba, bbbeq, bbaeq, bqb, bqa, bqbe, bqbeq, bquge, ...
                    # Heuristic: opcode contains "b" or "a" (above/below) → unsigned.
                    unsigned = node.opcode =~ /b(b|a)e?q?\z/ || node.opcode =~ /\bbu/
                    fits = unsigned ? imm.ppc64le16BitUnsignedImmediate? : imm.ppc64le16BitSignedImmediate?
                    unless fits
                        co  = node.codeOrigin
                        tmp = Tmp.new(co, :gpr)
                        newList << Instruction.new(co, "ppc64le_li64", [imm, tmp])
                        new_ops = ops.dup
                        new_ops[1] = tmp
                        newList << Instruction.new(co, node.opcode, new_ops, node.annotation)
                        next
                    end
                end
                newList << node
            else
                newList << node
            end
        else
            newList << node
        end
    }
    newList
end

# -------------------------------------------------------------------------
# Overflow / sign branch lowering (badd*o, bsub*o, badd*s, bsub*s).
#
# PPC64 has no equivalent of x86 "add; jo/js", and the XER route is a trap:
# the dot form (addo.) copies XER[SO] — the STICKY summary-overflow bit —
# into CR0, not this instruction's OV, so one earlier overflow anywhere
# makes every later bso fire; addic. never touches OV at all; and addo/
# subfo detect 64-bit overflow, which two int32 operands can never produce,
# so int32 overflow goes silently undetected (observed: s+=i loops wrapping
# mod 2^32 instead of promoting to double).  We avoid XER entirely:
#
#   int32 overflow (baddio/bsubio): do the arithmetic exactly in 64 bits on
#   sign-extended copies; overflow iff the 64-bit result differs from its
#   own low-word sign extension (extsw + cmpd).
#
#   int64 overflow (badd{p,q}o/bsub{p,q}o): sign algebra — for r = a+b,
#   overflow iff (a^r) & (b^r) < 0; for r = a-b, iff (a^b) & (a^r) < 0.
#
#   sign branches (b*s): plain add/sub, then branch if result negative
#   (32-bit: bilt → cmpwi tests the low word; 64-bit: bqlt → cmpdi).
#
# Everything is emitted as generic offlineasm ops on fresh Tmps so the
# register assigner picks provably-dead registers (same pattern as the
# jmp/call Address expansion — never hardcode a scratch in lowering).
# Address-destination forms (baddis imm, addr, lbl) are left untouched for
# the rmw expansion in ppc64leLowerMalformedAddresses.  The residual direct
# lowerings raise, so an unhandled operand shape fails loudly at offlineasm
# time instead of miscompiling.
# -------------------------------------------------------------------------
def ppc64leLowerOverflowBranches(list)
    newList = []
    list.each {
        | node |
        unless node.is_a?(Instruction)
            newList << node
            next
        end
        ops = node.operands
        co  = node.codeOrigin
        case node.opcode
        when "baddio", "bsubio"
            unless ops[1].is_a?(RegisterID)
                newList << node
                next
            end
            arith = (node.opcode == "baddio") ? "addq" : "subq"
            wide = Tmp.new(co, :gpr)   # exact 64-bit result
            sext = Tmp.new(co, :gpr)   # sign-extended src / re-extension of result
            newList << Instruction.new(co, "sxi2q", [ops[1], wide], node.annotation)
            if ops[0].is_a?(Immediate)
                newList << Instruction.new(co, arith, [ops[0], wide])
            else
                newList << Instruction.new(co, "sxi2q", [ops[0], sext])
                newList << Instruction.new(co, arith, [sext, wide])
            end
            # x86 addl / arm64 "adds Wn" zero-extend the 32-bit result, and
            # LLInt relies on that (binaryOp re-boxes with a plain
            # "orq numberTag"): a sign-extended negative result would leave
            # 0xFFFFFFFF in the upper word and malform the JSValue box.
            newList << Instruction.new(co, "zxi2q", [wide, ops[1]])
            newList << Instruction.new(co, "sxi2q", [wide, sext])
            newList << Instruction.new(co, "bqneq", [wide, sext, ops[2]])
        when "baddpo", "baddqo", "bsubpo", "bsubqo"
            unless ops[1].is_a?(RegisterID)
                newList << node
                next
            end
            isAdd = node.opcode.start_with?("badd")
            a = Tmp.new(co, :gpr)
            b = Tmp.new(co, :gpr)
            newList << Instruction.new(co, "move", [ops[1], a], node.annotation)  # a = old dst
            newList << Instruction.new(co, "move", [ops[0], b])                   # b = src (reg or imm)
            newList << Instruction.new(co, isAdd ? "addq" : "subq", [b, ops[1]])  # dst = r
            if isAdd
                newList << Instruction.new(co, "xorq", [ops[1], a])               # a = a^r
                newList << Instruction.new(co, "xorq", [ops[1], b])               # b = b^r
            else
                newList << Instruction.new(co, "xorq", [a, b])                    # b = a^b (before a is clobbered)
                newList << Instruction.new(co, "xorq", [ops[1], a])               # a = a^r
            end
            newList << Instruction.new(co, "andq", [b, a])                        # a &= b
            newList << Instruction.new(co, "bqlt", [a, Immediate.new(co, 0), ops[2]])
        when "baddis", "bsubis"
            unless ops[1].is_a?(RegisterID)
                newList << node   # baddis imm, Address, lbl → rmw expansion later
                next
            end
            newList << Instruction.new(co, (node.opcode == "baddis") ? "addi" : "subi", [ops[0], ops[1]], node.annotation)
            newList << Instruction.new(co, "bilt", [ops[1], Immediate.new(co, 0), ops[2]])
        when "baddps", "baddqs", "bsubps", "bsubqs"
            unless ops[1].is_a?(RegisterID)
                newList << node
                next
            end
            newList << Instruction.new(co, node.opcode.start_with?("badd") ? "addq" : "subq", [ops[0], ops[1]], node.annotation)
            newList << Instruction.new(co, "bqlt", [ops[1], Immediate.new(co, 0), ops[2]])
        else
            newList << node
        end
    }
    newList
end

# Per-backend hook called by riscLowerMisplacedAddresses BEFORE its generic
# rewrite.  We claim jmp/call so the generic path doesn't strip extra operands
# (notably PtrTag, which we use to distinguish C from JS calls in lowering).
# At this point our own ppc64leLowerMalformedAddresses has already replaced
# any Address target with a Tmp, so the call/jmp is safe to leave as-is.
class Instruction
    def self.lowerMisplacedAddressesPPC64LE(node, newList)
        if node.opcode == "jmp" || node.opcode == "call"
            newList << node
            return [true, newList]
        end
        [false, newList]
    end
end

class Sequence
    def getModifiedListPPC64LE(result = @list)
        # Expand bt*z/bt*nz (test-and-branch) into and + bieq/bineq
        result = riscLowerTest(result)
        # Expand bmulio → smulli + rshifti + bineq
        result = riscLowerHardBranchOps(result)
        # Expand overflow/sign branches (badd*o/bsub*o/badd*s/bsub*s) into
        # XER-free sequences on fresh Tmps.  Must run before the large-immediate
        # pass (it emits move/addq with the original immediates) and before the
        # malformed-address passes (it leaves Address forms for the rmw path).
        result = ppc64leLowerOverflowBranches(result)
        # Lower large immediates AFTER riscLowerTest, since riscLowerTest synthesises
        # and{i,p,q} with the original immediate (e.g. btqnz t, ~1, lbl → andq t, ~1, tmp).
        result = ppc64leLowerLargeImmediates(result)
        # PPC-specific malformed addresses: BaseIndex → Address, jmp/call Address,
        # transferp, lr SPR routing, baddis rmw, DS-form alignment.
        result = ppc64leLowerMalformedAddresses(result)
        # Second pass: BaseIndex expansion may have produced new jmp/call Address nodes.
        result = ppc64leLowerMalformedAddresses(result)
        # Generic RISC pass: lower Address operands in arith/test/branch/compare ops by
        # loading them into Tmps.  Covers cases not handled by the PPC-specific pass.
        result = riscLowerMisplacedAddresses(result)
        result = assignRegistersToTemporaries(result, :gpr, PPC64LE_EXTRA_GPRS)
        result = assignRegistersToTemporaries(result, :fpr, PPC64LE_EXTRA_FPRS)
        result
    end
end

# -------------------------------------------------------------------------
# Helper: emit a conditional branch that may target a label more than ±32 KB
# away.  PPC BC-form (Power ISA v2.07B §2.4.1) has only a 14-bit BD field
# (×4 = ±32 KB byte displacement); the unconditional B-form has 24 bits ×4 =
# ±32 MB.  We invert the condition and skip over a long-form `b` so any
# target reachable by `b` works.  The cost is +1 fall-through instruction.
# -------------------------------------------------------------------------
$ppc64leLongBranchCounter = 0
PPC64LE_BRANCH_INVERSE = {
    "blt" => "bge", "bge" => "blt",
    "bgt" => "ble", "ble" => "bgt",
    "beq" => "bne", "bne" => "beq",
    "bso" => "bns", "bns" => "bso",
}.freeze

def ppc64leEmitLongBranch(cond, target)
    inverted = PPC64LE_BRANCH_INVERSE[cond] or raise "ppc64le: unknown conditional #{cond}"
    $ppc64leLongBranchCounter += 1
    skip = ".Lppc64le_lb_#{$ppc64leLongBranchCounter}"
    $asm.puts "#{inverted} #{skip}"
    $asm.puts "b #{target}"
    $asm.puts "#{skip}:"
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

    if size == :i || size == :b
        # byte values are zero-extended to full register by lbz; compare as 32-bit
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
    when :eq  then ppc64leEmitLongBranch("beq", lbl)
    when :neq then ppc64leEmitLongBranch("bne", lbl)
    when :lt  then ppc64leEmitLongBranch("blt", lbl)
    when :gt  then ppc64leEmitLongBranch("bgt", lbl)
    when :le  then ppc64leEmitLongBranch("ble", lbl)
    when :ge  then ppc64leEmitLongBranch("bge", lbl)
    else
        raise "ppc64le: unknown compare condition #{cond}"
    end
end

# -------------------------------------------------------------------------
# -------------------------------------------------------------------------
# Helper: conditional set (c* opcodes).
# Emits: compare → mfcr → rlwinm to extract a single CR0 bit into LSB.
#
# After mfcr, CRF0 occupies x86-bit positions 31..28:
#   bit 31 = CR0.LT, bit 30 = CR0.GT, bit 29 = CR0.EQ, bit 28 = CR0.SO
# rlwinm(dst, dst, SH, 31, 31) rotates x86-bit (31-SH+1) to bit 0 and masks.
# To get CR0.LT (bit 31) to bit 0: SH=1  (31+1=32 mod 32=0) ✓
# To get CR0.GT (bit 30) to bit 0: SH=2  (30+2=32 mod 32=0) ✓
# To get CR0.EQ (bit 29) to bit 0: SH=3  (29+3=32 mod 32=0) ✓
# -------------------------------------------------------------------------
# -------------------------------------------------------------------------
# Helper: FP conditional set (cd*/cf* opcodes).
# fcmpu cr0, fA, fB sets CR0: LT(bit31), GT(bit30), EQ(bit29), FU(bit28).
# Same bit layout as integer CR0 — reuse rlwinm rotations.
# "ordered" conditions exclude NaN (FU=0). "un" variants include NaN.
# -------------------------------------------------------------------------
def ppc64leEmitFPConditionalSet(cond, is_double, ops)
    fA  = ops[0].ppc64leOperand
    fB  = ops[1].ppc64leOperand
    dst = ops[2].ppc64leOperand
    # fcmpu (unordered) sets CR0.FU (= SO) for NaN; does NOT raise exceptions.
    $asm.puts "fcmpu cr0, #{fA}, #{fB}"
    case cond
    when :lt
        # LT=1 only when ordered and fA < fB. NaN → LT=0. ✓
        $asm.puts "mfcr #{dst}"
        $asm.puts "rlwinm #{dst}, #{dst}, 1, 31, 31"
    when :gt
        # GT=1 only when ordered and fA > fB. NaN → GT=0. ✓
        $asm.puts "mfcr #{dst}"
        $asm.puts "rlwinm #{dst}, #{dst}, 2, 31, 31"
    when :eq
        # EQ=1 only when ordered and fA == fB. NaN → EQ=0. ✓
        $asm.puts "mfcr #{dst}"
        $asm.puts "rlwinm #{dst}, #{dst}, 3, 31, 31"
    when :lteq
        # LT | EQ and not NaN. Use branches: if GT or FU → false.
        # CR0.SO (bso) = FU (NaN). bgt = GT.
        $asm.puts "li #{dst}, 0"
        $asm.puts "bso 1f"    # NaN → false
        $asm.puts "bgt 1f"    # GT  → false
        $asm.puts "li #{dst}, 1"
        $asm.puts "1:"
    when :gteq
        # GT | EQ and not NaN. If LT or FU → false.
        $asm.puts "li #{dst}, 0"
        $asm.puts "bso 1f"    # NaN → false
        $asm.puts "blt 1f"    # LT  → false
        $asm.puts "li #{dst}, 1"
        $asm.puts "1:"
    when :neq
        # Not equal and not NaN. If EQ or FU → false.
        $asm.puts "li #{dst}, 0"
        $asm.puts "bso 1f"    # NaN → false
        $asm.puts "beq 1f"    # EQ  → false
        $asm.puts "li #{dst}, 1"
        $asm.puts "1:"
    when :neq_un
        # Not equal or NaN (unordered variant). EQ=0 OR FU=1 → true.
        $asm.puts "mfcr #{dst}"
        $asm.puts "rlwinm #{dst}, #{dst}, 3, 31, 31"  # EQ bit
        $asm.puts "xori #{dst}, #{dst}, 1"             # NOT EQ (NaN→EQ=0→NOT EQ=1) ✓
    else
        raise "ppc64le: unknown FP conditional set condition #{cond}"
    end
end

# Helper: FP branch (bd*/bf* opcodes).
# fcmpu cr0, fA, fB; CR0: LT(31), GT(30), EQ(29), FU/SO(28).
# ops: [fA, fB, label]
def ppc64leEmitFPBranch(cond, ops)
    fA  = ops[0].ppc64leOperand
    fB  = ops[1].ppc64leOperand
    lbl = ops[2].asmLabel
    $asm.puts "fcmpu cr0, #{fA}, #{fB}"
    case cond
    when :lt                                    # ordered <: NaN→LT=0 ✓
        ppc64leEmitLongBranch("blt", lbl)
    when :gt                                    # ordered >: NaN→GT=0 ✓
        ppc64leEmitLongBranch("bgt", lbl)
    when :eq                                    # ordered =: NaN→EQ=0 ✓
        ppc64leEmitLongBranch("beq", lbl)
    when :lteq                                  # ordered ≤: NaN→no branch
        ppc64leEmitLongBranch("blt", lbl)
        ppc64leEmitLongBranch("beq", lbl)
    when :gteq                                  # ordered ≥: NaN→no branch
        ppc64leEmitLongBranch("bgt", lbl)
        ppc64leEmitLongBranch("beq", lbl)
    when :neq_un                                # not-equal or NaN: bne = NOT EQ ✓
        ppc64leEmitLongBranch("bne", lbl)
    when :ltun                                  # < or NaN
        ppc64leEmitLongBranch("blt", lbl)
        ppc64leEmitLongBranch("bso", lbl)
    when :gtun                                  # > or NaN
        ppc64leEmitLongBranch("bgt", lbl)
        ppc64leEmitLongBranch("bso", lbl)
    when :ltequn                                # ≤ or NaN: ble = NOT GT ✓
        ppc64leEmitLongBranch("ble", lbl)
    when :gtequn                                # ≥ or NaN: bge = NOT LT ✓
        ppc64leEmitLongBranch("bge", lbl)
    else
        raise "ppc64le: unknown FP branch condition #{cond}"
    end
end

def ppc64leEmitConditionalSet(cond, size, signed, ops)
    ra  = ops[0].ppc64leOperand
    rbl = ops[1]
    dst = ops[2].ppc64leOperand

    if size == :i
        cmpOp  = signed ? "cmpwi" : "cmplwi"
        cmpOpR = signed ? "cmpw"  : "cmplw"
    else
        cmpOp  = signed ? "cmpdi" : "cmpldi"
        cmpOpR = signed ? "cmpd"  : "cmpld"
    end

    if rbl.is_a?(Immediate)
        $asm.puts "#{cmpOp} #{ra}, #{rbl.value}"
    else
        $asm.puts "#{cmpOpR} #{ra}, #{rbl.ppc64leOperand}"
    end

    $asm.puts "mfcr #{dst}"
    case cond
    when :eq
        $asm.puts "rlwinm #{dst}, #{dst}, 3, 31, 31"
    when :neq
        $asm.puts "rlwinm #{dst}, #{dst}, 3, 31, 31"
        $asm.puts "xori #{dst}, #{dst}, 1"
    when :lt
        $asm.puts "rlwinm #{dst}, #{dst}, 1, 31, 31"
    when :gt
        $asm.puts "rlwinm #{dst}, #{dst}, 2, 31, 31"
    when :le  # NOT gt
        $asm.puts "rlwinm #{dst}, #{dst}, 2, 31, 31"
        $asm.puts "xori #{dst}, #{dst}, 1"
    when :ge  # NOT lt
        $asm.puts "rlwinm #{dst}, #{dst}, 1, 31, 31"
        $asm.puts "xori #{dst}, #{dst}, 1"
    else
        raise "ppc64le: unknown conditional set condition #{cond}"
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
        # offlineasm: dst = src1 + src2.  Caller is responsible for choosing
        # only commutative ops here (add{i,p,q}); for non-commutative subtract,
        # see the dedicated subi/subp/subq case in lowerPPC64LE.
        dst = ops[2].ppc64leOperand
        if ops[0].is_a?(Immediate) && ops[1].is_a?(Immediate)
            raise "ppc64le: both operands immediate in #{node.opcode} at #{node.codeOriginString}"
        elsif ops[0].is_a?(Immediate)
            # imm-first: swap, since add is commutative
            $asm.puts "#{opcode3imm} #{dst}, #{ops[1].ppc64leOperand}, #{ops[0].value}"
        elsif ops[1].is_a?(Immediate)
            $asm.puts "#{opcode3imm} #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].value}"
        else
            $asm.puts "#{opcode3reg} #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].ppc64leOperand}"
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
            src = operands[0]
            if src.is_a?(Immediate)
                # mulli RT, RA, SI — 16-bit signed immediate (Power ISA v2.07B §3.3.10)
                if src.ppc64le16BitSignedImmediate?
                    $asm.puts "mulli #{dst}, #{dst}, #{src.value}"
                else
                    raise "ppc64le: mulq immediate #{src.value} out of 16-bit range at #{codeOriginString}"
                end
            else
                $asm.puts "mulld #{dst}, #{dst}, #{src.ppc64leOperand}"
            end

        when "muli"
            dst = operands[1].ppc64leOperand
            src = operands[0]
            if src.is_a?(Immediate)
                if src.ppc64le16BitSignedImmediate?
                    $asm.puts "mulli #{dst}, #{dst}, #{src.value}"
                else
                    raise "ppc64le: muli immediate #{src.value} out of 16-bit range at #{codeOriginString}"
                end
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
        # Critical correctness note: with bare-numeral register encoding, GAS
        # parses an integer in a register slot as a register number.  So
        # emitting `and dst, src, 1` is silently assembled as `and dst, src, r1`
        # (= AND with sp).  Every 3-operand form below MUST detect Immediate
        # operands and route to andi./ori/xori (which take a real immediate
        # field) — never let a bare integer end up as the third arg of `and`.

        when "andp", "andq", "andi"
            ops = operands
            dst = ops.last.ppc64leOperand
            if ops.length == 3
                if ops[0].is_a?(Immediate) && ops[1].is_a?(Immediate)
                    raise "ppc64le: andp with two immediates at #{codeOriginString}"
                elsif ops[0].is_a?(Immediate)
                    $asm.puts "andi. #{dst}, #{ops[1].ppc64leOperand}, #{ops[0].value}"
                elsif ops[1].is_a?(Immediate)
                    $asm.puts "andi. #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].value}"
                else
                    $asm.puts "and #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].ppc64leOperand}"
                end
            else
                src = ops[0]
                if src.is_a?(Immediate)
                    $asm.puts "andi. #{dst}, #{dst}, #{src.value}"
                else
                    $asm.puts "and #{dst}, #{src.ppc64leOperand}, #{dst}"
                end
            end

        when "orp", "orq", "ori"
            ops = operands
            dst = ops.last.ppc64leOperand
            if ops.length == 3
                if ops[0].is_a?(Immediate) && ops[1].is_a?(Immediate)
                    raise "ppc64le: orp with two immediates at #{codeOriginString}"
                elsif ops[0].is_a?(Immediate)
                    $asm.puts "ori #{dst}, #{ops[1].ppc64leOperand}, #{ops[0].value}"
                elsif ops[1].is_a?(Immediate)
                    $asm.puts "ori #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].value}"
                else
                    $asm.puts "or #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].ppc64leOperand}"
                end
            else
                src = ops[0]
                if src.is_a?(Immediate)
                    $asm.puts "ori #{dst}, #{dst}, #{src.value}"
                else
                    $asm.puts "or #{dst}, #{src.ppc64leOperand}, #{dst}"
                end
            end

        when "xorp", "xorq", "xori"
            ops = operands
            dst = ops.last.ppc64leOperand
            if ops.length == 3
                if ops[0].is_a?(Immediate) && ops[1].is_a?(Immediate)
                    raise "ppc64le: xorp with two immediates at #{codeOriginString}"
                elsif ops[0].is_a?(Immediate)
                    $asm.puts "xori #{dst}, #{ops[1].ppc64leOperand}, #{ops[0].value}"
                elsif ops[1].is_a?(Immediate)
                    $asm.puts "xori #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].value}"
                else
                    $asm.puts "xor #{dst}, #{ops[0].ppc64leOperand}, #{ops[1].ppc64leOperand}"
                end
            else
                src = ops[0]
                if src.is_a?(Immediate)
                    $asm.puts "xori #{dst}, #{dst}, #{src.value}"
                else
                    $asm.puts "xor #{dst}, #{src.ppc64leOperand}, #{dst}"
                end
            end

        when "notp", "notq", "noti"
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
            # 2-op: rshifti imm_or_reg, dst  →  dst >>= imm/reg (arithmetic)
            # 3-op: rshifti src, imm_or_reg, dst  →  dst = src >> imm/reg
            if operands.size == 3
                src = operands[0].ppc64leOperand
                shamt = operands[1]
                dst = operands[2].ppc64leOperand
                if shamt.is_a?(Immediate)
                    $asm.puts "srawi #{dst}, #{src}, #{shamt.value}"
                else
                    $asm.puts "sraw #{dst}, #{src}, #{shamt.ppc64leOperand}"
                end
            else
                dst = operands[1].ppc64leOperand
                src = operands[0]
                if src.is_a?(Immediate)
                    $asm.puts "srawi #{dst}, #{dst}, #{src.value}"
                else
                    $asm.puts "sraw #{dst}, #{dst}, #{src.ppc64leOperand}"
                end
            end

        when "smulli"
            # smulli src1, src2, dst_lo, dst_hi
            # riscLowerHardBranchOps generates: smulli [src, dst, dst, tmp]
            # where dst is both the second factor AND the low-result register.
            # Compute mulhw FIRST (while dst still holds the original factor),
            # then mullw to overwrite dst with the low result.
            src1   = operands[0].ppc64leOperand
            src2   = operands[1].ppc64leOperand
            dst_lo = operands[2].ppc64leOperand
            dst_hi = operands[3].ppc64leOperand
            $asm.puts "mulhw #{dst_hi}, #{src1}, #{src2}"
            $asm.puts "mullw #{dst_lo}, #{src1}, #{src2}"

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

        when "loadbs", "load8SignedExtendTo32", "loadbsi"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lbz #{dst}, #{addr.ppc64leOperand}"
            $asm.puts "extsb #{dst}, #{dst}"

        when "loadbsq"
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

        when "loadhs", "load16SignedExtendTo32", "loadhsi"
            addr = operands[0]
            dst  = operands[1].ppc64leOperand
            ppc64leValidateDOffset(addr.offset.value, codeOriginString)
            $asm.puts "lha #{dst}, #{addr.ppc64leOperand}"

        when "loadhsq"
            # lha sign-extends 16-bit to 64-bit in 64-bit mode (POWER ISA §3.3.2)
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
            elsif addr.is_a?(LabelReference)
                # ELFv2 §3.5: load the symbol's address from its GOT entry.
                # Using @toc@ha+@toc@l(rD) with `ld` would compute an offset to
                # the SYMBOL itself (DS-form load FROM the symbol), which only
                # works if the symbol is within ±2 GB of TOC AND we want its
                # first 8 bytes — not what `leap _g_config, dst` means.
                # @got@ha+@got@l(rD) with `ld` makes the linker create a GOT
                # entry containing the symbol's address; the load reads that
                # entry, giving us &symbol regardless of where the symbol lives.
                # Empirical test: gcc -fPIC -shared on inline asm using this
                # exact form against a hidden extern resolves to a single
                # `ld rD, NN(r2)` after linker relaxation.
                lbl = addr.asmLabel
                $asm.puts "addis #{dst}, 2, #{lbl}@got@ha"
                $asm.puts "ld #{dst}, #{lbl}@got@l(#{dst})"
                if addr.offset != 0
                    $asm.puts "addi #{dst}, #{dst}, #{addr.offset}"
                end
            else
                raise "ppc64le: leap requires Address or LabelReference operand at #{codeOriginString}"
            end

        # ------------------------------------------------------------------
        # Stack: push and pop
        #
        # push reg           — single register (8-byte, may temporarily misalign)
        # push reg1, reg2    — paired push (16-byte, maintains alignment)
        #   "push cfr, lr"  — the common prologue form on PPC64LE:
        #       mflr 0 ; stdu r1, -16(r1) ; std r31, 0(r1) ; std r0, 8(r1)
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
                    $asm.puts "mflr 0"
                    $asm.puts "stdu 1, -8(1)"
                    $asm.puts "std 0, 0(1)"
                else
                    $asm.puts "stdu 1, -8(1)"
                    $asm.puts "std #{reg.ppc64leOperand}, 0(1)"
                end
            elsif operands.length == 2
                r1op = operands[0]
                r2op = operands[1]
                lr1  = r1op.is_a?(RegisterID) && r1op.name == "lr"
                lr2  = r2op.is_a?(RegisterID) && r2op.name == "lr"
                if lr1 || lr2
                    # One of the operands is lr; save it into r0 first.
                    $asm.puts "mflr 0"
                    $asm.puts "stdu 1, -16(1)"
                    $asm.puts "std #{lr1 ? '0' : r1op.ppc64leOperand}, 0(1)"
                    $asm.puts "std #{lr2 ? '0' : r2op.ppc64leOperand}, 8(1)"
                else
                    $asm.puts "stdu 1, -16(1)"
                    $asm.puts "std #{r1op.ppc64leOperand}, 0(1)"
                    $asm.puts "std #{r2op.ppc64leOperand}, 8(1)"
                end
            else
                raise "ppc64le: push with #{operands.length} operands not supported"
            end

        when "pop"
            if operands.length == 1
                reg = operands[0]
                if reg.is_a?(RegisterID) && reg.name == "lr"
                    $asm.puts "ld 0, 0(1)"
                    $asm.puts "addi 1, 1, 8"
                    $asm.puts "mtlr 0"
                else
                    $asm.puts "ld #{reg.ppc64leOperand}, 0(1)"
                    $asm.puts "addi 1, 1, 8"
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
                    $asm.puts "ld 0, 0(1)"
                else
                    $asm.puts "ld #{slot0.ppc64leOperand}, 0(1)"
                end
                if slot8.is_a?(RegisterID) && slot8.name == "lr"
                    $asm.puts "ld 0, 8(1)"
                else
                    $asm.puts "ld #{slot8.ppc64leOperand}, 8(1)"
                end
                $asm.puts "addi 1, 1, 16"
                if slot0.is_a?(RegisterID) && slot0.name == "lr"
                    $asm.puts "mtlr 0"
                elsif slot8.is_a?(RegisterID) && slot8.name == "lr"
                    $asm.puts "mtlr 0"
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
            if op.is_a?(RegisterID) || op.is_a?(SpecialRegister)
                $asm.puts "mtctr #{op.ppc64leOperand}"
                $asm.puts "bctr"
            elsif op.is_a?(LabelReference) || op.is_a?(LocalLabelReference)
                $asm.puts "b #{op.asmLabel}"
            else
                raise "ppc64le: jmp operand type #{op.class} not supported at #{codeOriginString}"
            end

        when "call"
            op = operands[0]
            # Classify by PtrTag (symbolically, via the captured constexpr
            # map — see ppc64leClassifyCallTag above).  JS-entry tags → bare
            # bctrl; C tags and untagged register calls → full ELFv2 stanza.
            tagValue = (operands.length > 1 && operands[1].is_a?(Immediate)) ? operands[1].value : nil
            kind, tagName = ppc64leClassifyCallTag(tagValue, codeOriginString)
            if op.is_a?(RegisterID) || op.is_a?(SpecialRegister)
                $asm.comment "call #{kind}#{tagName ? " (#{tagName})" : ""}"
                # ELFv2 sec 2.4.2: caller sets r12 = callee entry so the
                # callee's GEP can compute its TOC (harmless for JS callees).
                if kind == :c
                    # C callee follows full ABI: it'll write lr at caller_sp+16
                    # (need linkage area) and may clobber r2 (need save/restore
                    # — host functions live in the jsc binary, a different
                    # module with a different TOC).
                    $asm.puts "stdu 1, -32(1)"
                    $asm.puts "std 2, 24(1)"
                    $asm.puts "mr 12, #{op.ppc64leOperand}"
                    $asm.puts "mtctr 12"
                    $asm.puts "bctrl"
                    $asm.puts "ld 2, 24(1)"
                    $asm.puts "addi 1, 1, 32"
                else
                    # JS callee: makeJavaScriptCall-style sp dance is already
                    # arranged by the caller; an extra stdu would shift the
                    # callee's cfr off and the codeBlock/callee/argCount slots
                    # would be read from wrong offsets.  Skip frame allocation.
                    $asm.puts "mr 12, #{op.ppc64leOperand}"
                    $asm.puts "mtctr 12"
                    $asm.puts "bctrl"
                end
            elsif op.is_a?(LabelReference) || op.is_a?(LocalLabelReference)
                # Direct call to a C function (slow path / runtime helper).
                # ELFv2 sec 2.2.4: the callee writes its saved lr at 16(r1) of
                # the CALLER's sp.  We must reserve at least 32 bytes of
                # "linkage area" so that store does not clobber caller data.
                # GCC's canonical caller emits `stdu 1,-32(1); bl ...; addi 1,1,32`,
                # verified empirically with `g++ -O2 -fPIC -S` on extern "C" calls.
                $asm.puts "stdu 1, -32(1)"
                $asm.puts "bl #{op.asmLabel}"
                $asm.puts "addi 1, 1, 32"
            else
                raise "ppc64le: call operand type #{op.class} not supported at #{codeOriginString}"
            end

        # ------------------------------------------------------------------
        # PC-relative address load (for dispatch table setup)
        #
        # pcrtoaddr label, dest:
        #   mflr 0           # save return address
        #   bl 1f             # LR = address of 1: (next instruction)
        #   1: mflr dest      # dest = address of 1:
        #   mtlr 0           # restore return address
        #   addis dest, dest, (label - 1b)@ha
        #   addi dest, dest, (label - 1b)@l
        # ------------------------------------------------------------------
        when "pcrtoaddr"
            lbl  = operands[0].asmLabel
            dest = operands[1].ppc64leOperand
            $asm.puts "mflr 0"
            $asm.puts "bl 1f"
            $asm.puts "1:"
            $asm.puts "mflr #{dest}"
            $asm.puts "mtlr 0"
            $asm.puts "addis #{dest}, #{dest}, (#{lbl} - 1b)@ha"
            $asm.puts "addi #{dest}, #{dest}, (#{lbl} - 1b)@l"

        # globaladdr label, dest — like pcrtoaddr but for globally visible symbols
        when "globaladdr"
            lbl  = operands[0].asmLabel
            dest = operands[1].ppc64leOperand
            $asm.puts "mflr 0"
            $asm.puts "bl 1f"
            $asm.puts "1:"
            $asm.puts "mflr #{dest}"
            $asm.puts "mtlr 0"
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
            ppc64leEmitLongBranch("beq", lbl)

        when "btinz", "btpnz", "btqnz", "btnz"
            reg = operands[0].ppc64leOperand
            lbl = operands[1].asmLabel
            $asm.puts "cmpdi #{reg}, 0"
            ppc64leEmitLongBranch("bne", lbl)

        # Test bit N: btbz reg, imm, label — branch if bit N of reg is zero
        when "btbz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rldicl. 0, #{reg}, 64 - #{bit}, 63"
            ppc64leEmitLongBranch("beq", lbl)

        when "btbnz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rldicl. 0, #{reg}, 64 - #{bit}, 63"
            ppc64leEmitLongBranch("bne", lbl)

        # Test signed bit (same as btbz/btbnz for PPC64LE):
        when "btbiz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rlwinm. 0, #{reg}, 0, #{bit}, #{bit}"
            ppc64leEmitLongBranch("beq", lbl)

        when "btbinz"
            reg = operands[0].ppc64leOperand
            bit = operands[1].value
            lbl = operands[2].asmLabel
            $asm.puts "rlwinm. 0, #{reg}, 0, #{bit}, #{bit}"
            ppc64leEmitLongBranch("bne", lbl)

        # ------------------------------------------------------------------
        # Integer compare-and-branch
        # b{i,q,p}{eq,neq,lt,gt,le,ge} src1, src2, label
        # signed variants: bilt, bigt, bile, bige
        # unsigned: bult, bugt, bule, buge (use cmpldi)
        # b{i,q,p}beq, b{i,q,p}baeq — above/below (unsigned)
        # ------------------------------------------------------------------
        when "bieq", "bbeq"
            ppc64leEmitCompareAndBranch(:eq, :i, true, operands)
        when "bineq", "bbneq"
            ppc64leEmitCompareAndBranch(:neq, :i, true, operands)
        when "bilt", "bblt"
            ppc64leEmitCompareAndBranch(:lt, :i, true, operands)
        when "bigt", "bbgt"
            ppc64leEmitCompareAndBranch(:gt, :i, true, operands)
        when "bile", "bilteq", "bblteq"
            ppc64leEmitCompareAndBranch(:le, :i, true, operands)
        when "bige", "bigteq", "bbgteq"
            ppc64leEmitCompareAndBranch(:ge, :i, true, operands)
        when "bult", "bib", "bbb"
            ppc64leEmitCompareAndBranch(:lt, :i, false, operands)
        when "bugt", "bia", "bba"
            ppc64leEmitCompareAndBranch(:gt, :i, false, operands)
        when "bule", "bibe", "bibeq", "bbbeq"
            ppc64leEmitCompareAndBranch(:le, :i, false, operands)
        when "buge", "biaeq", "bbaeq"
            ppc64leEmitCompareAndBranch(:ge, :i, false, operands)

        when "bqeq", "bpeq"
            ppc64leEmitCompareAndBranch(:eq, :q, true, operands)
        when "bqneq", "bpneq"
            ppc64leEmitCompareAndBranch(:neq, :q, true, operands)
        when "bqlt", "bplt"
            ppc64leEmitCompareAndBranch(:lt, :q, true, operands)
        when "bqgt", "bpgt"
            ppc64leEmitCompareAndBranch(:gt, :q, true, operands)
        when "bqle", "bple", "bqlteq", "bplteq"
            ppc64leEmitCompareAndBranch(:le, :q, true, operands)
        when "bqge", "bpge", "bqgteq", "bpgteq"
            ppc64leEmitCompareAndBranch(:ge, :q, true, operands)
        when "bqult", "bpult", "bqb", "bpb"
            ppc64leEmitCompareAndBranch(:lt, :q, false, operands)
        when "bqugt", "bpugt", "bqa", "bpa"
            ppc64leEmitCompareAndBranch(:gt, :q, false, operands)
        when "bqule", "bpule", "bqbe", "bpbe", "bpbeq", "bqbeq"
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
        when "baddis", "baddio", "baddps", "baddqs", "baddpo", "baddqo",
             "bsubis", "bsubio", "bsubps", "bsubqs", "bsubpo", "bsubqo"
            # All register/immediate forms are expanded by
            # ppc64leLowerOverflowBranches; baddis-with-Address by the rmw
            # expansion in ppc64leLowerMalformedAddresses.  Reaching here means
            # an operand shape neither pass claims — fail loudly rather than
            # emit a wrong XER-based sequence.
            raise "ppc64le: unlowered overflow/sign branch #{opcode} at #{codeOriginString}"

        when "baddinz", "baddiz"
            # 32-bit: CR0 must reflect the low word only.  The dot form (add.)
            # compares the full 64-bit register, which is wrong once bit 32
            # carries (e.g. lwz-loaded 0xFFFFFFFF + 1: low word is zero but
            # the 64-bit result is 0x100000000).  Use plain add + cmpwi.
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addi #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "add #{dst}, #{dst}, #{src.ppc64leOperand}"
            end
            # Match x86 addl / arm64 adds Wn: 32-bit result is zero-extended.
            $asm.puts "rldicl #{dst}, #{dst}, 0, 32"
            $asm.puts "cmpwi #{dst}, 0"
            ppc64leEmitLongBranch(opcode == "baddiz" ? "beq" : "bne", lbl)

        when "baddpnz", "baddqnz"
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "add. #{dst}, #{dst}, #{src.ppc64leOperand}"
            end
            ppc64leEmitLongBranch("bne", lbl)

        when "baddpz", "baddqz"
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{src.value}"
            else
                $asm.puts "add. #{dst}, #{dst}, #{src.ppc64leOperand}"
            end
            ppc64leEmitLongBranch("beq", lbl)

        # ------------------------------------------------------------------
        # Subtract-with-branch
        # bsubinz src, dst, label  →  dst -= src; branch if dst != 0
        # bsubiz  src, dst, label  →  dst -= src; branch if dst == 0
        # (pointer/quad variants are same as int on 64-bit)
        # ------------------------------------------------------------------
        when "bsubinz", "bsubiz"
            # 32-bit: plain sub + cmpwi (see baddinz note on dot-form vs low word).
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addi #{dst}, #{dst}, #{-src.value}"
            else
                $asm.puts "subf #{dst}, #{src.ppc64leOperand}, #{dst}"
            end
            # Match x86 subl: 32-bit result is zero-extended.
            $asm.puts "rldicl #{dst}, #{dst}, 0, 32"
            $asm.puts "cmpwi #{dst}, 0"
            ppc64leEmitLongBranch(opcode == "bsubiz" ? "beq" : "bne", lbl)

        when "bsubpnz", "bsubqnz"
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{-src.value}"
            else
                $asm.puts "subf. #{dst}, #{src.ppc64leOperand}, #{dst}"
            end
            ppc64leEmitLongBranch("bne", lbl)

        when "bsubpz", "bsubqz"
            src = operands[0]
            dst = operands[1].ppc64leOperand
            lbl = operands[2].asmLabel
            if src.is_a?(Immediate)
                $asm.puts "addic. #{dst}, #{dst}, #{-src.value}"
            else
                $asm.puts "subf. #{dst}, #{src.ppc64leOperand}, #{dst}"
            end
            ppc64leEmitLongBranch("beq", lbl)

        # ------------------------------------------------------------------
        # Conditional set: c{i,p,q,b}{eq,neq,lt,gt,lteq,gteq,a,b,aeq,beq}
        # Sets dst = 1 if condition true, 0 otherwise.
        # Uses mfcr + rlwinm bit extraction (no scratch register).
        # ------------------------------------------------------------------
        when "cieq", "cbeq", "cpeq", "cqeq"
            sz = (opcode == "cieq" || opcode == "cbeq") ? :i : :q
            ppc64leEmitConditionalSet(:eq, sz, true, operands)
        when "cineq", "cbneq", "cpneq", "cqneq"
            sz = (opcode == "cineq" || opcode == "cbneq") ? :i : :q
            ppc64leEmitConditionalSet(:neq, sz, true, operands)
        when "cilt", "cblt", "cplt", "cqlt"
            sz = (opcode == "cilt" || opcode == "cblt") ? :i : :q
            ppc64leEmitConditionalSet(:lt, sz, true, operands)
        when "cigt", "cbgt", "cpgt", "cqgt"
            sz = (opcode == "cigt" || opcode == "cbgt") ? :i : :q
            ppc64leEmitConditionalSet(:gt, sz, true, operands)
        when "cilteq", "cblteq", "cplteq", "cqlteq"
            sz = (opcode == "cilteq" || opcode == "cblteq") ? :i : :q
            ppc64leEmitConditionalSet(:le, sz, true, operands)
        when "cigteq", "cbgteq", "cpgteq", "cqgteq"
            sz = (opcode == "cigteq" || opcode == "cbgteq") ? :i : :q
            ppc64leEmitConditionalSet(:ge, sz, true, operands)
        # Unsigned variants
        when "cib", "cbb", "cpb", "cqb"
            sz = (opcode == "cib" || opcode == "cbb") ? :i : :q
            ppc64leEmitConditionalSet(:lt, sz, false, operands)
        when "cia", "cba", "cpa", "cqa"
            sz = (opcode == "cia" || opcode == "cba") ? :i : :q
            ppc64leEmitConditionalSet(:gt, sz, false, operands)
        when "cibeq", "cbbeq", "cpbeq", "cqbeq"
            sz = (opcode == "cibeq" || opcode == "cbbeq") ? :i : :q
            ppc64leEmitConditionalSet(:le, sz, false, operands)
        when "ciaeq", "cbaeq", "cpaeq", "cqaeq"
            sz = (opcode == "ciaeq" || opcode == "cbaeq") ? :i : :q
            ppc64leEmitConditionalSet(:ge, sz, false, operands)

        # FP conditional set — double
        when "cdlt"
            ppc64leEmitFPConditionalSet(:lt, true, operands)
        when "cdgt"
            ppc64leEmitFPConditionalSet(:gt, true, operands)
        when "cdeq"
            ppc64leEmitFPConditionalSet(:eq, true, operands)
        when "cdlteq"
            ppc64leEmitFPConditionalSet(:lteq, true, operands)
        when "cdgteq"
            ppc64leEmitFPConditionalSet(:gteq, true, operands)
        when "cdneq"
            ppc64leEmitFPConditionalSet(:neq, true, operands)
        when "cdnequn"
            ppc64leEmitFPConditionalSet(:neq_un, true, operands)

        # FP conditional set — float
        when "cflt"
            ppc64leEmitFPConditionalSet(:lt, false, operands)
        when "cfgt"
            ppc64leEmitFPConditionalSet(:gt, false, operands)
        when "cfeq"
            ppc64leEmitFPConditionalSet(:eq, false, operands)
        when "cflteq"
            ppc64leEmitFPConditionalSet(:lteq, false, operands)
        when "cfgteq"
            ppc64leEmitFPConditionalSet(:gteq, false, operands)
        when "cfneq"
            ppc64leEmitFPConditionalSet(:neq, false, operands)
        when "cfnequn"
            ppc64leEmitFPConditionalSet(:neq_un, false, operands)

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

        # Integer-to-float conversions.
        # Semantics per the ARM64 reference backend: the "s" suffix means
        # SIGNED source (scvtf), no suffix means UNSIGNED (ucvtf); "ci2*"
        # converts the low 32 bits ONLY (x86 cvtsi2sd Ed / arm64 scvtf Wn),
        # "cq2*" converts all 64.  The width part is critical: LLInt feeds
        # ci2ds boxed int32 JSValues whose upper bits hold the numberTag —
        # converting all 64 bits turns boxed 1 into -(2^49-1).
        #
        # mtvsrwa (sign-extend word), mtvsrwz (zero-extend word) and mtvsrd
        # move GPR→VSR directly (Power ISA v2.07B Book I §7.6, POWER8);
        # FPR f_n aliases VSR n, so fcfid* can consume the result in place.
        # fcfid/fcfids/fcfidu/fcfidus: v2.07B Book I §4.6.7.
        # Empirically verified on POWER9 (scratchpad ovf/cvt tests,
        # 2026-07-14): mtvsrwa+fcfid on boxed 0xFFFE000000000001 yields 1.0.
        when "ci2ds"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrwa #{dst}, #{src}"
            $asm.puts "fcfid #{dst}, #{dst}"
        when "ci2d"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrwz #{dst}, #{src}"
            $asm.puts "fcfid #{dst}, #{dst}"
        when "ci2fs"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrwa #{dst}, #{src}"
            $asm.puts "fcfids #{dst}, #{dst}"
        when "ci2f"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrwz #{dst}, #{src}"
            $asm.puts "fcfids #{dst}, #{dst}"
        when "cq2ds"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrd #{dst}, #{src}"
            $asm.puts "fcfid #{dst}, #{dst}"
        when "cq2d"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrd #{dst}, #{src}"
            $asm.puts "fcfidu #{dst}, #{dst}"
        when "cq2fs"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrd #{dst}, #{src}"
            $asm.puts "fcfids #{dst}, #{dst}"
        when "cq2f"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "mtvsrd #{dst}, #{src}"
            $asm.puts "fcfidus #{dst}, #{dst}"

        when "cd2i", "truncated2is"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu 1, -16(1)"
            $asm.puts "fctiwz #{src}, #{src}"
            $asm.puts "stfd #{src}, 0(1)"
            $asm.puts "lwz #{dst}, 4(1)"   # lower 32 bits of FP word (big-endian)
            $asm.puts "addi 1, 1, 16"

        # Double-to-float truncation and double-to-double
        when "fd2q"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu 1, -16(1)"
            $asm.puts "stfd #{src}, 0(1)"
            $asm.puts "ld #{dst}, 0(1)"
            $asm.puts "addi 1, 1, 16"

        when "fq2d"
            src = operands[0].ppc64leOperand
            dst = operands[1].ppc64leOperand
            $asm.puts "stdu 1, -16(1)"
            $asm.puts "std #{src}, 0(1)"
            $asm.puts "lfd #{dst}, 0(1)"
            $asm.puts "addi 1, 1, 16"

        # ------------------------------------------------------------------
        # FP compare and branch  (fcmpu cr0: LT=bit31, GT=bit30, EQ=bit29, FU=SO=bit28)
        # Ordered variants (no "un"): NaN must NOT trigger the branch.
        # Unordered variants ("un"/"equn"): NaN MUST trigger the branch.
        # ------------------------------------------------------------------

        # ordered equal: EQ=1 only when not NaN ✓
        when "bdeq", "bfeq"
            ppc64leEmitFPBranch(:eq, operands)

        # ordered not-equal — not used yet; NaN → false
        when "bdneq", "bfneq"
            ppc64leEmitFPBranch(:lteq, operands)   # placeholder — should be blt+bgt

        # not-equal or NaN: bne = NOT EQ covers both cases ✓
        when "bdnequn", "bfnequn"
            ppc64leEmitFPBranch(:neq_un, operands)

        # ordered: NaN → no branch (NaN sets FU not LT/GT/EQ)
        when "bdlt", "bflt"
            ppc64leEmitFPBranch(:lt, operands)
        when "bdgt", "bfgt"
            ppc64leEmitFPBranch(:gt, operands)

        # ordered ≤/≥: use blt+beq / bgt+beq so NaN (FU only) does not trigger
        when "bdlteq", "bflteq"
            ppc64leEmitFPBranch(:lteq, operands)
        when "bdgteq", "bfgteq"
            ppc64leEmitFPBranch(:gteq, operands)

        # unordered (NaN → branch): ble = NOT GT ✓ for ltequn; bge = NOT LT ✓ for gtequn
        when "bdltequn", "bfltequn"
            ppc64leEmitFPBranch(:ltequn, operands)
        when "bdgtequn", "bfgtequn"
            ppc64leEmitFPBranch(:gtequn, operands)

        # unordered lt/gt: simple condition + bso for NaN path
        when "bdltun", "bfltun"
            ppc64leEmitFPBranch(:ltun, operands)
        when "bdgtun", "bfgtun"
            ppc64leEmitFPBranch(:gtun, operands)

        # unordered equal (NaN → branch): bso + beq
        when "bdequn", "bfequn"
            fA = operands[0].ppc64leOperand; fB = operands[1].ppc64leOperand; l = operands[2].asmLabel
            $asm.puts "fcmpu cr0, #{fA}, #{fB}"
            ppc64leEmitLongBranch("bso", l)   # NaN → branch
            ppc64leEmitLongBranch("beq", l)   # equal → branch

        # ------------------------------------------------------------------
        # Misc
        # ------------------------------------------------------------------
        when "crash"
            $asm.puts ".int 0xbadbeef0"   # illegal instruction trap

        when "break"
            $asm.puts ".int 0xbadbeef0"

        when "unimplemented"
            $asm.puts ".int 0xbadbeef0"

        else
            lowerDefault
        end
    end
end
