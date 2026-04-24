// Standalone PPC64Assembler encoding verifier.
//
// Builds outside the WebKit tree. Reproduces the dForm / dsForm / xoForm /
// xfxForm helpers from PPC64Assembler.h bit-for-bit, emits the bytes each
// opcode method would emit, and prints them as a GNU as(1) `.byte` sequence.
// The test then assembles that sequence and objdumps it, so we verify that:
//   (a) our C++ emitter formulas match what we think they compute
//   (b) those bytes disassemble to the intended PPC64 mnemonic on real toolchain
//
// Build & run on the PPC64 test box:
//   g++ -O2 ppc64_encoding_test.cpp -o ppc64_encoding_test
//   ./ppc64_encoding_test > /tmp/test.s
//   as /tmp/test.s -o /tmp/test.o
//   objdump -d /tmp/test.o
//
// Then eyeball the objdump output against the intent-comments in the table.

#include <cassert>
#include <cstdint>
#include <cstdio>

using RegID = uint32_t;

static constexpr uint32_t dForm(uint32_t opcode, RegID rtOrRs, RegID ra, uint16_t imm)
{
    assert(opcode < 64);
    return (opcode << 26) | (rtOrRs << 21) | (ra << 16) | imm;
}

static constexpr uint32_t dsForm(uint32_t opcode, RegID rtOrRs, RegID ra, int16_t byteOffset, uint32_t xo)
{
    assert(opcode < 64);
    assert(xo < 4);
    assert((byteOffset & 0x3) == 0);
    return (opcode << 26) | (rtOrRs << 21) | (ra << 16)
         | (static_cast<uint32_t>(static_cast<uint16_t>(byteOffset)) & 0xFFFC) | xo;
}

static constexpr uint32_t xoForm(uint32_t opcode, RegID rt, RegID ra, RegID rb,
                                 uint32_t oe, uint32_t xo, uint32_t rc)
{
    assert(opcode < 64);
    assert(oe < 2);
    assert(xo < 512);
    assert(rc < 2);
    return (opcode << 26) | (rt << 21) | (ra << 16) | (rb << 11)
         | (oe << 10) | (xo << 1) | rc;
}

static constexpr uint32_t xfxForm(uint32_t opcode, RegID rtOrRs, uint32_t sprNum, uint32_t xo)
{
    assert(opcode < 64);
    assert(sprNum < 1024);
    assert(xo < 1024);
    uint32_t sprField = ((sprNum & 0x1F) << 5) | ((sprNum >> 5) & 0x1F);
    return (opcode << 26) | (rtOrRs << 21) | (sprField << 11) | (xo << 1);
}

static constexpr uint32_t iForm(uint32_t opcode, int32_t byteOffset, uint32_t aa, uint32_t lk)
{
    assert(opcode < 64);
    assert(aa < 2);
    assert(lk < 2);
    assert((byteOffset & 0x3) == 0);
    assert(byteOffset >= -(1 << 25) && byteOffset < (1 << 25));
    return (opcode << 26) | (static_cast<uint32_t>(byteOffset) & 0x03FFFFFC) | (aa << 1) | lk;
}

static constexpr uint32_t bForm(uint32_t opcode, uint32_t bo, uint32_t bi,
                                int32_t byteOffset, uint32_t aa, uint32_t lk)
{
    assert(opcode < 64);
    assert(bo < 32);
    assert(bi < 32);
    assert(aa < 2);
    assert(lk < 2);
    assert((byteOffset & 0x3) == 0);
    assert(byteOffset >= -(1 << 15) && byteOffset < (1 << 15));
    return (opcode << 26) | (bo << 21) | (bi << 16)
         | (static_cast<uint32_t>(byteOffset) & 0xFFFC) | (aa << 1) | lk;
}

static constexpr uint32_t xlForm(uint32_t opcode, uint32_t bo, uint32_t bi,
                                 uint32_t bh, uint32_t xo, uint32_t lk)
{
    assert(opcode < 64);
    assert(bo < 32);
    assert(bi < 32);
    assert(bh < 8);
    assert(xo < 1024);
    assert(lk < 2);
    return (opcode << 26) | (bo << 21) | (bi << 16) | (bh << 13) | (xo << 1) | lk;
}

static constexpr uint32_t aForm(uint32_t opcode, RegID frt, RegID fra, RegID frb, RegID frc,
                                uint32_t xo, uint32_t rc)
{
    assert(opcode < 64);
    assert(xo < 32);
    assert(rc < 2);
    return (opcode << 26) | (frt << 21) | (fra << 16) | (frb << 11) | (frc << 6) | (xo << 1) | rc;
}

static constexpr uint32_t mdForm(uint32_t opcode, RegID rs, RegID ra,
                                 uint32_t sh, uint32_t mbOrMe, uint32_t xo, uint32_t rc)
{
    assert(opcode < 64);
    assert(sh < 64);
    assert(mbOrMe < 64);
    assert(xo < 8);
    assert(rc < 2);
    uint32_t shLow5 = sh & 0x1F;
    uint32_t sh2 = (sh >> 5) & 1;
    uint32_t mbField = ((mbOrMe & 0x1F) << 1) | ((mbOrMe >> 5) & 1);
    return (opcode << 26)
         | (rs << 21)
         | (ra << 16)
         | (shLow5 << 11)
         | (mbField << 5)
         | (xo << 2)
         | (sh2 << 1)
         | rc;
}

static constexpr uint32_t xForm(uint32_t opcode, RegID rs, RegID ra, RegID rb, uint32_t xo, uint32_t rc)
{
    assert(opcode < 64);
    assert(xo < 1024);
    assert(rc < 2);
    return (opcode << 26) | (rs << 21) | (ra << 16) | (rb << 11) | (xo << 1) | rc;
}

static constexpr uint32_t cmpXForm(uint32_t opcode, uint32_t bf, uint32_t l,
                                   RegID ra, RegID rb, uint32_t xo)
{
    assert(opcode < 64);
    assert(bf < 8);
    assert(l < 2);
    assert(xo < 1024);
    return (opcode << 26) | (bf << 23) | (l << 21) | (ra << 16) | (rb << 11) | (xo << 1);
}

static constexpr uint32_t cmpDForm(uint32_t opcode, uint32_t bf, uint32_t l,
                                   RegID ra, uint16_t imm)
{
    assert(opcode < 64);
    assert(bf < 8);
    assert(l < 2);
    return (opcode << 26) | (bf << 23) | (l << 21) | (ra << 16) | imm;
}

struct Test {
    const char* mnemonic;
    uint32_t encoding;
    uint32_t expected;
};

int main()
{
    // Every `expected` value was captured from GNU `as` output on POWER9 /
    // Fedora 44 2026-04-24 and cross-checked against Power ISA v2.07B Book I.
    // If a formula below produces something else, the assertion fires and
    // we stop before emitting nonsense.
    Test tests[] = {
        // D-form (opcode 24 = ori, opcode 14 = addi)
        { "nop (= ori 0,0,0)",         dForm(24, 0, 0, 0),                                   0x60000000 },
        { "ori 3,4,0x1234",            dForm(24, 4, 3, 0x1234),                              0x60831234 },
        { "li 5,100 (= addi 5,0,100)", dForm(14, 5, 0, static_cast<uint16_t>(100)),          0x38a00064 },
        { "li 5,-100",                 dForm(14, 5, 0, static_cast<uint16_t>(-100)),         0x38a0ff9c },
        { "li 3,42",                   dForm(14, 3, 0, 42),                                  0x3860002a },

        // XO-form (opcode 31, XO=266 = add, XO=40 = subf, XO=104 = neg)
        { "add 3,4,5",                 xoForm(31, 3, 4, 5, 0, 266, 0),                       0x7c642a14 },
        { "add 0,1,2",                 xoForm(31, 0, 1, 2, 0, 266, 0),                       0x7c011214 },
        { "subf 3,4,5",                xoForm(31, 3, 4, 5, 0, 40, 0),                        0x7c642850 },
        { "subf 0,1,2",                xoForm(31, 0, 1, 2, 0, 40, 0),                        0x7c011050 },
        { "neg 3,4",                   xoForm(31, 3, 4, 0, 0, 104, 0),                       0x7c6400d0 },

        // D-form opcode 15 = addis / lis (simplified mnemonic with RA=0)
        { "lis 3,0x1234",              dForm(15, 3, 0, 0x1234),                              0x3c601234 },
        { "lis 5,-1",                  dForm(15, 5, 0, static_cast<uint16_t>(-1)),           0x3ca0ffff },
        { "addis 6,7,-32768",          dForm(15, 6, 7, static_cast<uint16_t>(-32768)),       0x3cc78000 },

        // D-form narrow loads/stores (full 16-bit signed byte displacement).
        { "lwz 3,0(4)",                dForm(32, 3, 4, 0),                                   0x80640000 },
        { "lwz 3,100(4)",              dForm(32, 3, 4, 100),                                 0x80640064 },
        { "lwz 3,-4(4)",               dForm(32, 3, 4, static_cast<uint16_t>(-4)),           0x8064fffc },
        { "stw 3,0(4)",                dForm(36, 3, 4, 0),                                   0x90640000 },
        { "stw 5,16(1)",               dForm(36, 5, 1, 16),                                  0x90a10010 },
        { "lbz 3,0(4)",                dForm(34, 3, 4, 0),                                   0x88640000 },
        { "stb 5,1(1)",                dForm(38, 5, 1, 1),                                   0x98a10001 },
        { "lhz 3,2(4)",                dForm(40, 3, 4, 2),                                   0xa0640002 },
        { "lha 3,4(4)",                dForm(42, 3, 4, 4),                                   0xa8640004 },
        { "sth 3,6(4)",                dForm(44, 3, 4, 6),                                   0xb0640006 },

        // Multiply XO-form (opcode 31): low/high × {32,64}-bit signed/unsigned.
        { "mullw  3,4,5",              xoForm(31, 3, 4, 5, 0, 235, 0),                       0x7c6429d6 },
        { "mulld  3,4,5",              xoForm(31, 3, 4, 5, 0, 233, 0),                       0x7c6429d2 },
        { "mulhw  3,4,5",              xoForm(31, 3, 4, 5, 0, 75, 0),                        0x7c642896 },
        { "mulhd  3,4,5",              xoForm(31, 3, 4, 5, 0, 73, 0),                        0x7c642892 },
        { "mulhwu 3,4,5",              xoForm(31, 3, 4, 5, 0, 11, 0),                        0x7c642816 },
        { "mulhdu 3,4,5",              xoForm(31, 3, 4, 5, 0, 9, 0),                         0x7c642812 },

        // mulli D-form (opcode 7).
        { "mulli  3,4,100",            dForm(7, 3, 4, 100),                                  0x1c640064 },
        { "mulli  3,4,-1",             dForm(7, 3, 4, static_cast<uint16_t>(-1)),            0x1c64ffff },

        // MD-form probe: confirm the mb encoding rule with 8 single-bit
        // mb values. GNU as disassembles these as simplified mnemonics
        // (clrldi / rotldi) but the underlying hex is what matters.
        // rldicl opcode 30, XO=0.
        { "rldicl 3,4,0,0  (rotldi 0)",    mdForm(30, 4, 3, 0, 0,  0, 0),                     0x78830000 },
        { "rldicl 3,4,0,1  (clrldi 1)",    mdForm(30, 4, 3, 0, 1,  0, 0),                     0x78830040 },
        { "rldicl 3,4,0,2  (clrldi 2)",    mdForm(30, 4, 3, 0, 2,  0, 0),                     0x78830080 },
        { "rldicl 3,4,0,4  (clrldi 4)",    mdForm(30, 4, 3, 0, 4,  0, 0),                     0x78830100 },
        { "rldicl 3,4,0,8  (clrldi 8)",    mdForm(30, 4, 3, 0, 8,  0, 0),                     0x78830200 },
        { "rldicl 3,4,0,16 (clrldi 16)",   mdForm(30, 4, 3, 0, 16, 0, 0),                     0x78830400 },
        { "rldicl 3,4,0,32 (clrldi 32)",   mdForm(30, 4, 3, 0, 32, 0, 0),                     0x78830020 },
        { "rldicl 3,4,0,63 (clrldi 63)",   mdForm(30, 4, 3, 0, 63, 0, 0),                     0x788307e0 },

        // MD-form with non-trivial SH (exercises the sh2 split at bit 30).
        // rldicl 3,4,56,8  (srdi 8)  — SH=56, MB=8.  sh_low5=24, sh2=1.
        { "rldicl 3,4,56,8  (srdi 8)",     mdForm(30, 4, 3, 56, 8,  0, 0),                    0x7883c202 },
        { "rldicl 3,4,32,32 (srdi 32)",    mdForm(30, 4, 3, 32, 32, 0, 0),                    0x78830022 },
        { "rldicl 3,4,63,1  (srdi 1)",     mdForm(30, 4, 3, 63, 1,  0, 0),                    0x7883f842 },

        // rldicr opcode 30, XO=1. sldi n = rldicr SH=n, ME=63-n.
        { "rldicr 3,4,8,55  (sldi 8)",     mdForm(30, 4, 3, 8, 55, 1, 0),                     0x788345e4 },
        { "rldicr 3,4,0,63  (clrrdi 0)",   mdForm(30, 4, 3, 0, 63, 1, 0),                     0x788307e4 },

        // rldic  opcode 30, XO=2.
        { "rldic  3,4,4,60",               mdForm(30, 4, 3, 4, 60, 2, 0),                     0x78832728 },

        // rldimi opcode 30, XO=3.
        { "rldimi 3,4,8,0",                mdForm(30, 4, 3, 8, 0,  3, 0),                     0x7883400c },

        // FP A-form arithmetic. opcode 63 = double, opcode 59 = single.
        // For fadd/fsub/fdiv FRC=0, for fmul FRB=0. fmadd uses all four.
        { "fadd  3,4,5",               aForm(63, 3, 4, 5, 0, 21, 0),                         0xfc64282a },
        { "fsub  3,4,5",               aForm(63, 3, 4, 5, 0, 20, 0),                         0xfc642828 },
        { "fmul  3,4,5",               aForm(63, 3, 4, 0, 5, 25, 0),                         0xfc640172 },
        { "fdiv  3,4,5",               aForm(63, 3, 4, 5, 0, 18, 0),                         0xfc642824 },
        { "fadds 3,4,5",               aForm(59, 3, 4, 5, 0, 21, 0),                         0xec64282a },
        { "fsubs 3,4,5",               aForm(59, 3, 4, 5, 0, 20, 0),                         0xec642828 },
        { "fmuls 3,4,5",               aForm(59, 3, 4, 0, 5, 25, 0),                         0xec640172 },
        { "fdivs 3,4,5",               aForm(59, 3, 4, 5, 0, 18, 0),                         0xec642824 },
        { "fmadd 3,4,5,6",             aForm(63, 3, 4, 6, 5, 29, 0),                         0xfc64317a },

        // FP unary X-form (opcode 63). FRA slot = 0 (unused).
        { "fneg 3,4",                  xForm(63, 3, 0, 4, 40,  0),                           0xfc602050 },
        { "fabs 3,4",                  xForm(63, 3, 0, 4, 264, 0),                           0xfc602210 },
        { "fmr  3,4",                  xForm(63, 3, 0, 4, 72,  0),                           0xfc602090 },

        // FP load/store D-form. RT/RS slot holds an FPR number; bit layout
        // is otherwise identical to GPR D-form so we just use dForm() with
        // the FPR number cast to RegID.
        { "lfd  3,0(4)",               dForm(50, 3, 4, 0),                                   0xc8640000 },
        { "stfd 3,0(4)",               dForm(54, 3, 4, 0),                                   0xd8640000 },
        { "lfs  3,8(4)",               dForm(48, 3, 4, 8),                                   0xc0640008 },
        { "stfs 3,16(4)",              dForm(52, 3, 4, 16),                                  0xd0640010 },
        { "lfd  31,-8(1)",             dForm(50, 31, 1, static_cast<uint16_t>(-8)),          0xcbe1fff8 },

        // Sign-extend X-form (opcode 31). RB slot unused (0).
        { "extsb 3,4",                 xForm(31, 4, 3, 0, 954, 0),                           0x7c830774 },
        { "extsh 3,4",                 xForm(31, 4, 3, 0, 922, 0),                           0x7c830734 },
        { "extsw 3,4",                 xForm(31, 4, 3, 0, 986, 0),                           0x7c8307b4 },

        // Shift-by-register X-form (opcode 31).
        { "slw  3,4,5",                xForm(31, 4, 3, 5, 24, 0),                            0x7c832830 },
        { "srw  3,4,5",                xForm(31, 4, 3, 5, 536, 0),                           0x7c832c30 },
        { "sraw 3,4,5",                xForm(31, 4, 3, 5, 792, 0),                           0x7c832e30 },
        { "sld  3,4,5",                xForm(31, 4, 3, 5, 27, 0),                            0x7c832836 },
        { "srd  3,4,5",                xForm(31, 4, 3, 5, 539, 0),                           0x7c832c36 },
        { "srad 3,4,5",                xForm(31, 4, 3, 5, 794, 0),                           0x7c832e34 },

        // Divide XO-form (opcode 31).
        { "divw  3,4,5",               xoForm(31, 3, 4, 5, 0, 491, 0),                       0x7c642bd6 },
        { "divwu 3,4,5",               xoForm(31, 3, 4, 5, 0, 459, 0),                       0x7c642b96 },
        { "divd  3,4,5",               xoForm(31, 3, 4, 5, 0, 489, 0),                       0x7c642bd2 },
        { "divdu 3,4,5",               xoForm(31, 3, 4, 5, 0, 457, 0),                       0x7c642b92 },

        // tw 31,0,0 (= trap, opcode 31, XO=4, TO=31 = unconditional).
        // Built inline because the TO slot uses the same bits as our
        // xForm's rtOrRs (RegisterID typed) parameter.
        { "trap (tw 31,0,0)",          (31u<<26)|(31u<<21)|(0u<<16)|(0u<<11)|(4u<<1),        0x7fe00008 },

        // X-form indexed loads/stores (opcode 31).
        { "ldx  3,4,5",                xForm(31, 3, 4, 5, 21, 0),                            0x7c64282a },
        { "stdx 3,4,5",                xForm(31, 3, 4, 5, 149, 0),                           0x7c64292a },
        { "lwzx 3,4,5",                xForm(31, 3, 4, 5, 23, 0),                            0x7c64282e },
        { "stwx 3,4,5",                xForm(31, 3, 4, 5, 151, 0),                           0x7c64292e },
        { "lbzx 3,4,5",                xForm(31, 3, 4, 5, 87, 0),                            0x7c6428ae },
        { "stbx 3,4,5",                xForm(31, 3, 4, 5, 215, 0),                           0x7c6429ae },
        { "lhzx 3,4,5",                xForm(31, 3, 4, 5, 279, 0),                           0x7c642a2e },
        { "lhax 3,4,5",                xForm(31, 3, 4, 5, 343, 0),                           0x7c642aae },
        { "sthx 3,4,5",                xForm(31, 3, 4, 5, 407, 0),                           0x7c642b2e },

        // DS-form (opcode 58 = ld, opcode 62 = std, XO=0)
        { "ld 3,0(4)",                 dsForm(58, 3, 4, 0, 0),                               0xe8640000 },
        { "ld 3,8(4)",                 dsForm(58, 3, 4, 8, 0),                               0xe8640008 },
        { "ld 3,-16(4)",               dsForm(58, 3, 4, -16, 0),                             0xe864fff0 },
        { "ld 3,0(0)",                 dsForm(58, 3, 0, 0, 0),                               0xe8600000 },
        { "std 3,0(4)",                dsForm(62, 3, 4, 0, 0),                               0xf8640000 },
        { "std 5,16(1)",               dsForm(62, 5, 1, 16, 0),                              0xf8a10010 },
        { "std 3,-24(1)",              dsForm(62, 3, 1, -24, 0),                             0xf861ffe8 },

        // XFX-form (opcode 31, XO=339 = mfspr, XO=467 = mtspr; SPR#8=LR, SPR#9=CTR)
        { "mflr 3  (= mfspr 3,8)",     xfxForm(31, 3, 8, 339),                               0x7c6802a6 },
        { "mtlr 3  (= mtspr 8,3)",     xfxForm(31, 3, 8, 467),                               0x7c6803a6 },
        { "mfctr 3 (= mfspr 3,9)",     xfxForm(31, 3, 9, 339),                               0x7c6902a6 },
        { "mtctr 3 (= mtspr 9,3)",     xfxForm(31, 3, 9, 467),                               0x7c6903a6 },

        // I-form (opcode 18 = b / bl). GNU as emitted these at known addresses;
        // we pass the resolved byte offset (not a symbol).
        { "b    .   (offset 0)",       iForm(18, 0, 0, 0),                                   0x48000000 },
        { "b    .+4 (offset 4)",       iForm(18, 4, 0, 0),                                   0x48000004 },
        { "bl   .-8 (offset -8)",      iForm(18, -8, 0, 1),                                  0x4bfffff9 },

        // B-form (opcode 16 = bc). beq = BO 12 (branch-if-CR[BI]=1), BI 2 (CR0.EQ).
        { "beq  .-12 (bc 12,2,-12)",   bForm(16, 12, 2, -12, 0, 0),                          0x4182fff4 },

        // Logical X-form (opcode 31). In asm syntax the dest is first
        // (RA) and the source is second (RS): "and 3,4,5" means
        // RA=3 (dest), RS=4 (source1), RB=5 (source2). In our xForm
        // helper RS comes first, RA second: xForm(31, rs=4, ra=3, rb=5, ...)
        { "and 3,4,5",                 xForm(31, 4, 3, 5, 28, 0),                            0x7c832838 },
        { "or  3,4,5",                 xForm(31, 4, 3, 5, 444, 0),                           0x7c832b78 },
        { "xor 3,4,5",                 xForm(31, 4, 3, 5, 316, 0),                           0x7c832a78 },
        { "mr 7,8 (= or 7,8,8)",       xForm(31, 8, 7, 8, 444, 0),                           0x7d074378 },

        // Compare X-form (opcode 31, XO=0 = cmp, XO=32 = cmpl). L=1 for
        // 64-bit (cmpd/cmpld), L=0 for 32-bit (cmpw/cmplw).
        { "cmpd  0,3,4",               cmpXForm(31, 0, 1, 3, 4, 0),                          0x7c232000 },
        { "cmpd  7,3,4",               cmpXForm(31, 7, 1, 3, 4, 0),                          0x7fa32000 },
        { "cmpw  0,3,4",               cmpXForm(31, 0, 0, 3, 4, 0),                          0x7c032000 },
        { "cmpld 0,3,4",               cmpXForm(31, 0, 1, 3, 4, 32),                         0x7c232040 },

        // Compare D-form (opcode 11 = cmpi signed, opcode 10 = cmpli unsigned).
        { "cmpdi  0,3,0",              cmpDForm(11, 0, 1, 3, 0),                             0x2c230000 },
        { "cmpdi  0,3,-1",             cmpDForm(11, 0, 1, 3, static_cast<uint16_t>(-1)),     0x2c23ffff },
        { "cmpdi  7,3,100",            cmpDForm(11, 7, 1, 3, 100),                           0x2fa30064 },
        { "cmpldi 0,3,100",            cmpDForm(10, 0, 1, 3, 100),                           0x28230064 },
        { "cmpwi  0,3,42",             cmpDForm(11, 0, 0, 3, 42),                            0x2c03002a },

        // XL-form (opcode 19, XO=16 = bclr, XO=528 = bcctr).
        { "blr   (= bclr 20,0,0)",     xlForm(19, 20, 0, 0, 16, 0),                          0x4e800020 },
        { "bctr  (= bcctr 20,0,0)",    xlForm(19, 20, 0, 0, 528, 0),                         0x4e800420 },
        { "bctrl (= bcctr 20,0,0,1)",  xlForm(19, 20, 0, 0, 528, 1),                         0x4e800421 },
        { "beqlr (= bclr 12,2,0)",     xlForm(19, 12, 2, 0, 16, 0),                          0x4d820020 },
        { "bnelr (= bclr  4,2,0)",     xlForm(19,  4, 2, 0, 16, 0),                          0x4c820020 },
    };

    int failures = 0;
    for (auto& t : tests) {
        if (t.encoding != t.expected) {
            fprintf(stderr, "FAIL %s: got 0x%08x expected 0x%08x\n",
                    t.mnemonic, t.encoding, t.expected);
            ++failures;
        }
    }
    if (failures) {
        fprintf(stderr, "%d encoding mismatch(es)\n", failures);
        return 1;
    }

    printf("# Generated by ppc64_encoding_test — all %zu encodings match expected hex.\n",
           sizeof(tests) / sizeof(tests[0]));
    printf("# Disassemble with `as ... | objdump -d` and eyeball the mnemonics.\n");
    printf(".text\n");
    for (auto& t : tests) {
        uint32_t e = t.encoding;
        printf(".byte 0x%02x, 0x%02x, 0x%02x, 0x%02x  # %-32s (0x%08x)\n",
               e & 0xff, (e >> 8) & 0xff, (e >> 16) & 0xff, (e >> 24) & 0xff,
               t.mnemonic, e);
    }
    return 0;
}
