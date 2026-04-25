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

static constexpr uint32_t vxForm(uint32_t opcode, RegID vrt, RegID vra, RegID vrb, uint32_t xo)
{
    assert(opcode < 64);
    assert(xo < 2048);
    return (opcode << 26) | (vrt << 21) | (vra << 16) | (vrb << 11) | xo;
}

static constexpr uint32_t xx1Form(uint32_t opcode, uint32_t vsrT, RegID ra, RegID rb, uint32_t xo)
{
    assert(opcode < 64);
    assert(vsrT < 64);
    assert(xo < 1024);
    return (opcode << 26) | ((vsrT & 0x1F) << 21) | (ra << 16) | (rb << 11)
         | (xo << 1) | ((vsrT >> 5) & 1);
}

static constexpr uint32_t xx3Form(uint32_t opcode, uint32_t vsrT, uint32_t vsrA,
                                  uint32_t vsrB, uint32_t xo)
{
    assert(opcode < 64);
    assert(vsrT < 64);
    assert(vsrA < 64);
    assert(vsrB < 64);
    assert(xo < 256);
    return (opcode << 26) | ((vsrT & 0x1F) << 21) | ((vsrA & 0x1F) << 16)
         | ((vsrB & 0x1F) << 11) | (xo << 3)
         | (((vsrA >> 5) & 1) << 2) | (((vsrB >> 5) & 1) << 1) | ((vsrT >> 5) & 1);
}

static constexpr uint32_t vaForm(uint32_t opcode, RegID vrt, RegID vra, RegID vrb, RegID vrc, uint32_t xo)
{
    assert(opcode < 64);
    assert(xo < 64);
    return (opcode << 26) | (vrt << 21) | (vra << 16) | (vrb << 11) | (vrc << 6) | xo;
}

static constexpr uint32_t vcForm(uint32_t opcode, RegID vrt, RegID vra, RegID vrb,
                                 uint32_t rc, uint32_t xo)
{
    assert(opcode < 64);
    assert(rc < 2);
    assert(xo < 1024);
    return (opcode << 26) | (vrt << 21) | (vra << 16) | (vrb << 11) | (rc << 10) | xo;
}

static constexpr uint32_t xsForm(uint32_t opcode, RegID rs, RegID ra,
                                 uint32_t sh, uint32_t xo, uint32_t rc)
{
    assert(opcode < 64);
    assert(sh < 64);
    assert(xo < 512);
    assert(rc < 2);
    uint32_t shLow5 = sh & 0x1F;
    uint32_t sh2 = (sh >> 5) & 1;
    return (opcode << 26) | (rs << 21) | (ra << 16) | (shLow5 << 11) | (xo << 2) | (sh2 << 1) | rc;
}

static constexpr uint32_t mFormImm(uint32_t opcode, RegID rs, RegID ra,
                                   uint32_t sh, uint32_t mb, uint32_t me, uint32_t rc)
{
    assert(opcode < 64);
    assert(sh < 32);
    assert(mb < 32);
    assert(me < 32);
    assert(rc < 2);
    return (opcode << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) | (me << 1) | rc;
}

static constexpr uint32_t mFormReg(uint32_t opcode, RegID rs, RegID ra,
                                   RegID rb, uint32_t mb, uint32_t me, uint32_t rc)
{
    assert(opcode < 64);
    assert(mb < 32);
    assert(me < 32);
    assert(rc < 2);
    return (opcode << 26) | (rs << 21) | (ra << 16) | (rb << 11) | (mb << 6) | (me << 1) | rc;
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

        // FP convert X-form (opcode 63, FRA=0). Verified against GNU as.
        { "fcfid   3,4",               xForm(63, 3, 0, 4, 846, 0),                           0xfc60269c },
        { "fctid   3,4",               xForm(63, 3, 0, 4, 814, 0),                           0xfc60265c },
        { "fctidz  3,4",               xForm(63, 3, 0, 4, 815, 0),                           0xfc60265e },
        { "fctiw   3,4",               xForm(63, 3, 0, 4,  14, 0),                           0xfc60201c },
        { "fctiwz  3,4",               xForm(63, 3, 0, 4,  15, 0),                           0xfc60201e },
        { "fcfidu  3,4",               xForm(63, 3, 0, 4, 974, 0),                           0xfc60279c },
        { "fctidu  3,4",               xForm(63, 3, 0, 4, 942, 0),                           0xfc60275c },
        { "fctiduz 3,4",               xForm(63, 3, 0, 4, 943, 0),                           0xfc60275e },
        { "fctiwu  3,4",               xForm(63, 3, 0, 4, 142, 0),                           0xfc60211c },
        { "fctiwuz 3,4",               xForm(63, 3, 0, 4, 143, 0),                           0xfc60211e },

        // FP compare — BF-shape X-form. bf<<23 at bits 6-8 matches
        // cmpXForm with L=0; FRA and FRB are FPR indices (which use the
        // same 5-bit slots as GPRs for encoding purposes).
        { "fcmpu 0,3,4",               cmpXForm(63, 0, 0, 3, 4, 0),                          0xfc032000 },
        { "fcmpu 7,3,4",               cmpXForm(63, 7, 0, 3, 4, 0),                          0xff832000 },
        { "fcmpo 0,3,4",               cmpXForm(63, 0, 0, 3, 4, 32),                         0xfc032040 },

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

        // Count-leading-zeros / popcount X-form (opcode 31, RB slot=0).
        { "cntlzw  3,4",               xForm(31, 4, 3, 0, 26,  0),                           0x7c830034 },
        { "cntlzd  3,4",               xForm(31, 4, 3, 0, 58,  0),                           0x7c830074 },
        { "popcntw 3,4",               xForm(31, 4, 3, 0, 378, 0),                           0x7c8302f4 },
        { "popcntd 3,4",               xForm(31, 4, 3, 0, 506, 0),                           0x7c8303f4 },
        { "popcntb 3,4",               xForm(31, 4, 3, 0, 122, 0),                           0x7c8300f4 },

        // srawi (X-form, SH as immediate in RB slot).
        { "srawi 3,4,8",               xForm(31, 4, 3, 8, 824, 0),                           0x7c834670 },
        { "srawi 3,4,31",              xForm(31, 4, 3, 31, 824, 0),                          0x7c83fe70 },

        // sradi (XS-form, split SH, XO=413, 9 bits).
        { "sradi 3,4,8",               xsForm(31, 4, 3, 8,  413, 0),                         0x7c834674 },
        { "sradi 3,4,32",              xsForm(31, 4, 3, 32, 413, 0),                         0x7c830676 },
        { "sradi 3,4,63",              xsForm(31, 4, 3, 63, 413, 0),                         0x7c83fe76 },

        // subfic D-form (opcode 8).
        { "subfic 3,4,100",            dForm(8, 3, 4, 100),                                  0x20640064 },
        { "subfic 3,4,-1",             dForm(8, 3, 4, static_cast<uint16_t>(-1)),            0x2064ffff },

        // M-form 32-bit rotate-and-mask. Simple 5-bit MB/ME, no swap.
        { "rlwinm 3,4,8,0,31",         mFormImm(21, 4, 3, 8,  0,  31, 0),                    0x5483403e },
        { "rlwinm 3,4,0,24,31",        mFormImm(21, 4, 3, 0,  24, 31, 0),                    0x5483063e },
        { "rlwinm 3,4,16,16,31",       mFormImm(21, 4, 3, 16, 16, 31, 0),                    0x5483843e },
        { "rlwimi 3,4,8,0,23",         mFormImm(20, 4, 3, 8,  0,  23, 0),                    0x5083402e },
        { "rlwnm  3,4,5,0,31",         mFormReg(23, 4, 3, 5,  0,  31, 0),                    0x5c83283e },
        // Simplified mnemonics (slwi / srwi / clrlwi → rlwinm).
        { "slwi   3,4,8  (rlwinm 3,4,8,0,23)",   mFormImm(21, 4, 3, 8,  0,  23, 0),          0x5483402e },
        { "srwi   3,4,8  (rlwinm 3,4,24,8,31)",  mFormImm(21, 4, 3, 24, 8,  31, 0),          0x5483c23e },
        { "clrlwi 3,4,16 (rlwinm 3,4,0,16,31)",  mFormImm(21, 4, 3, 0,  16, 31, 0),          0x5483043e },

        // Atomic LL/SC (X-form, opcode 31). store-cond. variants all use Rc=1.
        { "lwarx  3,4,5",              xForm(31, 3, 4, 5,  20, 0),                           0x7c642828 },
        { "ldarx  3,4,5",              xForm(31, 3, 4, 5,  84, 0),                           0x7c6428a8 },
        { "lbarx  3,4,5",              xForm(31, 3, 4, 5,  52, 0),                           0x7c642868 },
        { "lharx  3,4,5",              xForm(31, 3, 4, 5, 116, 0),                           0x7c6428e8 },
        { "stwcx. 3,4,5",              xForm(31, 3, 4, 5, 150, 1),                           0x7c64292d },
        { "stdcx. 3,4,5",              xForm(31, 3, 4, 5, 214, 1),                           0x7c6429ad },
        { "stbcx. 3,4,5",              xForm(31, 3, 4, 5, 694, 1),                           0x7c642d6d },
        { "sthcx. 3,4,5",              xForm(31, 3, 4, 5, 726, 1),                           0x7c642dad },

        // Cache block ops. X-form, RT/RS slot = 0.
        { "dcbst 0,3",                 xForm(31, 0, 0, 3, 54,   0),                          0x7c00186c },
        { "dcbf  0,3",                 xForm(31, 0, 0, 3, 86,   0),                          0x7c0018ac },
        { "icbi  0,3",                 xForm(31, 0, 0, 3, 982,  0),                          0x7c001fac },
        { "dcbz  0,3",                 xForm(31, 0, 0, 3, 1014, 0),                          0x7c001fec },

        // VMX FP arithmetic (single-precision per lane).
        { "vaddfp 3,4,5",              vxForm(4, 3, 4, 5,   10),                             0x1064280a },
        { "vsubfp 3,4,5",              vxForm(4, 3, 4, 5,   74),                             0x1064284a },
        { "vminfp 3,4,5",              vxForm(4, 3, 4, 5, 1098),                             0x10642c4a },
        { "vmaxfp 3,4,5",              vxForm(4, 3, 4, 5, 1034),                             0x10642c0a },

        // VMX FP unary (VRA slot = 0).
        { "vrfin 3,4",                 vxForm(4, 3, 0, 4, 522),                              0x1060220a },
        { "vrfip 3,4",                 vxForm(4, 3, 0, 4, 650),                              0x1060228a },
        { "vrfim 3,4",                 vxForm(4, 3, 0, 4, 714),                              0x106022ca },
        { "vrfiz 3,4",                 vxForm(4, 3, 0, 4, 586),                              0x1060224a },

        // VMX popcount per lane (VRA = 0).
        { "vpopcntb 3,4",              vxForm(4, 3, 0, 4, 1795),                             0x10602703 },
        { "vpopcnth 3,4",              vxForm(4, 3, 0, 4, 1859),                             0x10602743 },
        { "vpopcntw 3,4",              vxForm(4, 3, 0, 4, 1923),                             0x10602783 },
        { "vpopcntd 3,4",              vxForm(4, 3, 0, 4, 1987),                             0x106027c3 },

        // VMX unpack-and-sign-extend (VRA = 0). Mnemonic high/low are
        // BE-named — on PPC64LE, vupkhsb actually unpacks the wasm-LOW
        // lanes; see PLAN.md SIMD lessons.
        { "vupkhsb 3,4",               vxForm(4, 3, 0, 4,  526),                             0x1060220e },
        { "vupklsb 3,4",               vxForm(4, 3, 0, 4,  654),                             0x1060228e },
        { "vupkhsh 3,4",               vxForm(4, 3, 0, 4,  590),                             0x1060224e },
        { "vupklsh 3,4",               vxForm(4, 3, 0, 4,  718),                             0x106022ce },
        { "vupkhsw 3,4",               vxForm(4, 3, 0, 4, 1614),                             0x1060264e },
        { "vupklsw 3,4",               vxForm(4, 3, 0, 4, 1742),                             0x106026ce },

        // VMX FP↔int conversions (UIMM in VRA slot, fractional-bit-position).
        { "vctsxs 3,4,0",              vxForm(4, 3, 0, 4, 970),                              0x106023ca },
        { "vctuxs 3,4,0",              vxForm(4, 3, 0, 4, 906),                              0x1060238a },
        { "vcfsx  3,4,0",              vxForm(4, 3, 0, 4, 842),                              0x1060234a },
        { "vcfux  3,4,0",              vxForm(4, 3, 0, 4, 778),                              0x1060230a },
        { "vctsxs 3,4,5 (UIMM=5)",     vxForm(4, 3, 5, 4, 970),                              0x106523ca },
        { "vcfsx  3,4,3 (UIMM=3)",     vxForm(4, 3, 3, 4, 842),                              0x1063234a },

        // VMX FP compares (VC-form, Rc=0).
        { "vcmpeqfp 3,4,5",            vcForm(4, 3, 4, 5, 0, 198),                           0x106428c6 },
        { "vcmpgefp 3,4,5",            vcForm(4, 3, 4, 5, 0, 454),                           0x106429c6 },
        { "vcmpgtfp 3,4,5",            vcForm(4, 3, 4, 5, 0, 710),                           0x10642ac6 },

        // VSX XX1-form load/store (opcode 31). VSR# is split: low 5
        // bits in T-slot, high bit at bit 31. vs35 exercises TX=1.
        { "lxvd2x  vs3,r4,r5",         xx1Form(31, 3,  4, 5, 844),                           0x7c642e98 },
        { "lxvd2x  vs35,r4,r5",        xx1Form(31, 35, 4, 5, 844),                           0x7c642e99 },
        { "stxvd2x vs3,r4,r5",         xx1Form(31, 3,  4, 5, 972),                           0x7c642f98 },
        { "lxvw4x  vs3,r4,r5",         xx1Form(31, 3,  4, 5, 780),                           0x7c642e18 },
        { "stxvw4x vs3,r4,r5",         xx1Form(31, 3,  4, 5, 908),                           0x7c642f18 },

        // VSX XX3-form bitwise (opcode 60).
        { "xxlor  vs3,vs4,vs5",        xx3Form(60, 3, 4, 5, 146),                            0xf0642c90 },
        { "xxlxor vs3,vs4,vs5",        xx3Form(60, 3, 4, 5, 154),                            0xf0642cd0 },
        { "xxland vs3,vs4,vs5",        xx3Form(60, 3, 4, 5, 130),                            0xf0642c10 },

        // VA-form 4-operand. Note vmaddfp asm syntax reorders
        // (VRT,VRA,VRC,VRB) but the encoding is (VRT,VRA,VRB,VRC).
        { "vperm    3,4,5,6",          vaForm(4, 3, 4, 5, 6, 43),                            0x106429ab },
        { "vsel     3,4,5,6",          vaForm(4, 3, 4, 5, 6, 42),                            0x106429aa },
        { "vmaddfp  3,4,6,5  (asm)",   vaForm(4, 3, 4, 5, 6, 46),                            0x106429ae },

        // VMX lane-wise min/max (16 cases).
        { "vminub 3,4,5",              vxForm(4, 3, 4, 5, 514),                              0x10642a02 },
        { "vmaxub 3,4,5",              vxForm(4, 3, 4, 5,   2),                              0x10642802 },
        { "vminuh 3,4,5",              vxForm(4, 3, 4, 5, 578),                              0x10642a42 },
        { "vmaxuh 3,4,5",              vxForm(4, 3, 4, 5,  66),                              0x10642842 },
        { "vminuw 3,4,5",              vxForm(4, 3, 4, 5, 642),                              0x10642a82 },
        { "vmaxuw 3,4,5",              vxForm(4, 3, 4, 5, 130),                              0x10642882 },
        { "vminud 3,4,5",              vxForm(4, 3, 4, 5, 706),                              0x10642ac2 },
        { "vmaxud 3,4,5",              vxForm(4, 3, 4, 5, 194),                              0x106428c2 },
        { "vminsb 3,4,5",              vxForm(4, 3, 4, 5, 770),                              0x10642b02 },
        { "vmaxsb 3,4,5",              vxForm(4, 3, 4, 5, 258),                              0x10642902 },
        { "vminsh 3,4,5",              vxForm(4, 3, 4, 5, 834),                              0x10642b42 },
        { "vmaxsh 3,4,5",              vxForm(4, 3, 4, 5, 322),                              0x10642942 },
        { "vminsw 3,4,5",              vxForm(4, 3, 4, 5, 898),                              0x10642b82 },
        { "vmaxsw 3,4,5",              vxForm(4, 3, 4, 5, 386),                              0x10642982 },
        { "vminsd 3,4,5",              vxForm(4, 3, 4, 5, 962),                              0x10642bc2 },
        { "vmaxsd 3,4,5",              vxForm(4, 3, 4, 5, 450),                              0x106429c2 },

        // VMX lane-wise shifts and rotates (VX-form, opcode 4).
        { "vslb  3,4,5",               vxForm(4, 3, 4, 5,  260),                             0x10642904 },
        { "vslh  3,4,5",               vxForm(4, 3, 4, 5,  324),                             0x10642944 },
        { "vslw  3,4,5",               vxForm(4, 3, 4, 5,  388),                             0x10642984 },
        { "vsld  3,4,5",               vxForm(4, 3, 4, 5, 1476),                             0x10642dc4 },
        { "vsrb  3,4,5",               vxForm(4, 3, 4, 5,  516),                             0x10642a04 },
        { "vsrh  3,4,5",               vxForm(4, 3, 4, 5,  580),                             0x10642a44 },
        { "vsrw  3,4,5",               vxForm(4, 3, 4, 5,  644),                             0x10642a84 },
        { "vsrd  3,4,5",               vxForm(4, 3, 4, 5, 1732),                             0x10642ec4 },
        { "vsrab 3,4,5",               vxForm(4, 3, 4, 5,  772),                             0x10642b04 },
        { "vsrah 3,4,5",               vxForm(4, 3, 4, 5,  836),                             0x10642b44 },
        { "vsraw 3,4,5",               vxForm(4, 3, 4, 5,  900),                             0x10642b84 },
        { "vsrad 3,4,5",               vxForm(4, 3, 4, 5,  964),                             0x10642bc4 },
        { "vrlb  3,4,5",               vxForm(4, 3, 4, 5,    4),                             0x10642804 },
        { "vrlh  3,4,5",               vxForm(4, 3, 4, 5,   68),                             0x10642844 },
        { "vrlw  3,4,5",               vxForm(4, 3, 4, 5,  132),                             0x10642884 },
        { "vrld  3,4,5",               vxForm(4, 3, 4, 5,  196),                             0x106428c4 },

        // VMX merge — interleave high/low/even/odd lanes from two vectors.
        { "vmrghb 3,4,5",              vxForm(4, 3, 4, 5,   12),                             0x1064280c },
        { "vmrghh 3,4,5",              vxForm(4, 3, 4, 5,   76),                             0x1064284c },
        { "vmrghw 3,4,5",              vxForm(4, 3, 4, 5,  140),                             0x1064288c },
        { "vmrglb 3,4,5",              vxForm(4, 3, 4, 5,  268),                             0x1064290c },
        { "vmrglh 3,4,5",              vxForm(4, 3, 4, 5,  332),                             0x1064294c },
        { "vmrglw 3,4,5",              vxForm(4, 3, 4, 5,  396),                             0x1064298c },
        { "vmrgew 3,4,5",              vxForm(4, 3, 4, 5, 1932),                             0x10642f8c },
        { "vmrgow 3,4,5",              vxForm(4, 3, 4, 5, 1676),                             0x10642e8c },

        // VMX splat (lane-from-vector). UIMM in VRA slot.
        { "vspltb 3,4,7",              vxForm(4, 3, 7, 4, 524),                              0x1067220c },
        { "vsplth 3,4,3",              vxForm(4, 3, 3, 4, 588),                              0x1063224c },
        { "vspltw 3,4,1",              vxForm(4, 3, 1, 4, 652),                              0x1061228c },
        // VMX splat immediate. SIMM in VRA slot, VRB=0.
        { "vspltisb 3,5",              vxForm(4, 3, 5,  0, 780),                             0x1065030c },
        { "vspltish 3,-1",             vxForm(4, 3, 31, 0, 844),                             0x107f034c },
        { "vspltisw 3,15",             vxForm(4, 3, 15, 0, 908),                             0x106f038c },

        // VMX compares (VC-form, opcode 4). Rc=0 in our defaults.
        { "vcmpequb 3,4,5",            vcForm(4, 3, 4, 5, 0,   6),                           0x10642806 },
        { "vcmpequb. 3,4,5 (Rc=1)",    vcForm(4, 3, 4, 5, 1,   6),                           0x10642c06 },
        { "vcmpequh 3,4,5",            vcForm(4, 3, 4, 5, 0,  70),                           0x10642846 },
        { "vcmpequw 3,4,5",            vcForm(4, 3, 4, 5, 0, 134),                           0x10642886 },
        { "vcmpequd 3,4,5",            vcForm(4, 3, 4, 5, 0, 199),                           0x106428c7 },
        { "vcmpgtub 3,4,5",            vcForm(4, 3, 4, 5, 0, 518),                           0x10642a06 },
        { "vcmpgtsb 3,4,5",            vcForm(4, 3, 4, 5, 0, 774),                           0x10642b06 },
        { "vcmpgtsd 3,4,5",            vcForm(4, 3, 4, 5, 0, 967),                           0x10642bc7 },
        { "vcmpgtuh 3,4,5",            vcForm(4, 3, 4, 5, 0, 582),                           0x10642a46 },
        { "vcmpgtuw 3,4,5",            vcForm(4, 3, 4, 5, 0, 646),                           0x10642a86 },
        { "vcmpgtud 3,4,5",            vcForm(4, 3, 4, 5, 0, 711),                           0x10642ac7 },
        { "vcmpgtsh 3,4,5",            vcForm(4, 3, 4, 5, 0, 838),                           0x10642b46 },
        { "vcmpgtsw 3,4,5",            vcForm(4, 3, 4, 5, 0, 902),                           0x10642b86 },

        // VMX load/store (X-form, opcode 31, VR in RT/RS slot).
        { "lvx    3,4,5",              xForm(31, 3, 4, 5, 103, 0),                           0x7c6428ce },
        { "stvx   3,4,5",              xForm(31, 3, 4, 5, 231, 0),                           0x7c6429ce },
        { "lvebx  3,4,5",              xForm(31, 3, 4, 5,   7, 0),                           0x7c64280e },
        { "lvehx  3,4,5",              xForm(31, 3, 4, 5,  39, 0),                           0x7c64284e },
        { "lvewx  3,4,5",              xForm(31, 3, 4, 5,  71, 0),                           0x7c64288e },
        { "stvebx 3,4,5",              xForm(31, 3, 4, 5, 135, 0),                           0x7c64290e },
        { "stvehx 3,4,5",              xForm(31, 3, 4, 5, 167, 0),                           0x7c64294e },
        { "stvewx 3,4,5",              xForm(31, 3, 4, 5, 199, 0),                           0x7c64298e },

        // VMX vector multiply for WASM SIMD i16x8/i32x4/i64x2.mul + dot.
        { "vmuluwm  3,4,5",            vxForm(4, 3, 4, 5, 137),                              0x10642889 },
        { "vmulesh  3,4,5",            vxForm(4, 3, 4, 5, 840),                              0x10642b48 },
        { "vmulosh  3,4,5",            vxForm(4, 3, 4, 5, 328),                              0x10642948 },
        { "vmulesw  3,4,5",            vxForm(4, 3, 4, 5, 904),                              0x10642b88 },
        { "vmulosw  3,4,5",            vxForm(4, 3, 4, 5, 392),                              0x10642988 },
        { "vmsumshm 3,4,5,6",          vaForm(4, 3, 4, 5, 6, 40),                            0x106429a8 },

        // VMX integer add/sub SATURATING (12 cases).
        { "vaddubs 3,4,5",             vxForm(4, 3, 4, 5,  512),                             0x10642a00 },
        { "vadduhs 3,4,5",             vxForm(4, 3, 4, 5,  576),                             0x10642a40 },
        { "vadduws 3,4,5",             vxForm(4, 3, 4, 5,  640),                             0x10642a80 },
        { "vaddsbs 3,4,5",             vxForm(4, 3, 4, 5,  768),                             0x10642b00 },
        { "vaddshs 3,4,5",             vxForm(4, 3, 4, 5,  832),                             0x10642b40 },
        { "vaddsws 3,4,5",             vxForm(4, 3, 4, 5,  896),                             0x10642b80 },
        { "vsububs 3,4,5",             vxForm(4, 3, 4, 5, 1536),                             0x10642e00 },
        { "vsubuhs 3,4,5",             vxForm(4, 3, 4, 5, 1600),                             0x10642e40 },
        { "vsubuws 3,4,5",             vxForm(4, 3, 4, 5, 1664),                             0x10642e80 },
        { "vsubsbs 3,4,5",             vxForm(4, 3, 4, 5, 1792),                             0x10642f00 },
        { "vsubshs 3,4,5",             vxForm(4, 3, 4, 5, 1856),                             0x10642f40 },
        { "vsubsws 3,4,5",             vxForm(4, 3, 4, 5, 1920),                             0x10642f80 },

        // VMX integer add/sub modulo (VX-form, opcode 4).
        { "vaddubm 3,4,5",             vxForm(4, 3, 4, 5, 0),                                0x10642800 },
        { "vadduhm 3,4,5",             vxForm(4, 3, 4, 5, 64),                               0x10642840 },
        { "vadduwm 3,4,5",             vxForm(4, 3, 4, 5, 128),                              0x10642880 },
        { "vaddudm 3,4,5",             vxForm(4, 3, 4, 5, 192),                              0x106428c0 },
        { "vsububm 3,4,5",             vxForm(4, 3, 4, 5, 1024),                             0x10642c00 },
        { "vsubuhm 3,4,5",             vxForm(4, 3, 4, 5, 1088),                             0x10642c40 },
        { "vsubuwm 3,4,5",             vxForm(4, 3, 4, 5, 1152),                             0x10642c80 },
        { "vsubudm 3,4,5",             vxForm(4, 3, 4, 5, 1216),                             0x10642cc0 },

        // VMX (Altivec) logical, VX-form (opcode 4).
        { "vand  3,4,5",               vxForm(4, 3, 4, 5, 1028),                             0x10642c04 },
        { "vor   3,4,5",               vxForm(4, 3, 4, 5, 1156),                             0x10642c84 },
        { "vxor  3,4,5",               vxForm(4, 3, 4, 5, 1220),                             0x10642cc4 },
        { "vnor  3,4,5",               vxForm(4, 3, 4, 5, 1284),                             0x10642d04 },
        { "vandc 3,4,5",               vxForm(4, 3, 4, 5, 1092),                             0x10642c44 },

        // Memory barriers. sync L at opcode 31, XO=598 with L at Power bits
        // 9-10 (shift 21). isync = opcode 19, XO=150 (XL-form). eieio = 854.
        { "sync (hwsync)",             (31u<<26)|(0u<<21)|(598u<<1),                         0x7c0004ac },
        { "lwsync",                    (31u<<26)|(1u<<21)|(598u<<1),                         0x7c2004ac },
        { "ptesync",                   (31u<<26)|(2u<<21)|(598u<<1),                         0x7c4004ac },
        { "isync",                     (19u<<26)|(150u<<1),                                  0x4c00012c },
        { "eieio",                     (31u<<26)|(854u<<1),                                  0x7c0006ac },

        // tw 31,0,0 (= trap, opcode 31, XO=4, TO=31 = unconditional).
        // Built inline because the TO slot uses the same bits as our
        // xForm's rtOrRs (RegisterID typed) parameter.
        { "trap (tw 31,0,0)",          (31u<<26)|(31u<<21)|(0u<<16)|(0u<<11)|(4u<<1),        0x7fe00008 },

        // D-form/DS-form load/store with update.
        { "ldu  3,8(4)",               dsForm(58, 3, 4, 8,  1),                              0xe8640009 },
        { "stdu 3,-8(1)",              dsForm(62, 3, 1, -8, 1),                              0xf861fff9 },
        { "lwzu 3,4(4)",               dForm(33, 3, 4, 4),                                   0x84640004 },
        { "stwu 3,-16(1)",             dForm(37, 3, 1, static_cast<uint16_t>(-16)),          0x9461fff0 },
        { "lbzu 3,1(4)",               dForm(35, 3, 4, 1),                                   0x8c640001 },
        { "stbu 3,-1(4)",              dForm(39, 3, 4, static_cast<uint16_t>(-1)),           0x9c64ffff },
        { "lhzu 3,2(4)",               dForm(41, 3, 4, 2),                                   0xa4640002 },
        { "lhau 3,2(4)",               dForm(43, 3, 4, 2),                                   0xac640002 },
        { "sthu 3,-2(4)",              dForm(45, 3, 4, static_cast<uint16_t>(-2)),           0xb464fffe },

        // X-form indexed update (opcode 31, XO = base XO + 32).
        { "ldux  3,4,5",               xForm(31, 3, 4, 5,  53, 0),                           0x7c64286a },
        { "stdux 3,4,5",               xForm(31, 3, 4, 5, 181, 0),                           0x7c64296a },
        { "lwzux 3,4,5",               xForm(31, 3, 4, 5,  55, 0),                           0x7c64286e },
        { "stwux 3,4,5",               xForm(31, 3, 4, 5, 183, 0),                           0x7c64296e },
        { "lbzux 3,4,5",               xForm(31, 3, 4, 5, 119, 0),                           0x7c6428ee },
        { "stbux 3,4,5",               xForm(31, 3, 4, 5, 247, 0),                           0x7c6429ee },
        { "lhzux 3,4,5",               xForm(31, 3, 4, 5, 311, 0),                           0x7c642a6e },
        { "lhaux 3,4,5",               xForm(31, 3, 4, 5, 375, 0),                           0x7c642aee },
        { "sthux 3,4,5",               xForm(31, 3, 4, 5, 439, 0),                           0x7c642b6e },

        // FP indexed load/store (X-form, opcode 31).
        { "lfdx  3,4,5",               xForm(31, 3, 4, 5, 599, 0),                           0x7c642cae },
        { "stfdx 3,4,5",               xForm(31, 3, 4, 5, 727, 0),                           0x7c642dae },
        { "lfsx  3,4,5",               xForm(31, 3, 4, 5, 535, 0),                           0x7c642c2e },
        { "stfsx 3,4,5",               xForm(31, 3, 4, 5, 663, 0),                           0x7c642d2e },

        // FP indexed update (XO = indexed XO + 32).
        { "lfdux  3,4,5",              xForm(31, 3, 4, 5, 631, 0),                           0x7c642cee },
        { "stfdux 3,4,5",              xForm(31, 3, 4, 5, 759, 0),                           0x7c642dee },
        { "lfsux  3,4,5",              xForm(31, 3, 4, 5, 567, 0),                           0x7c642c6e },
        { "stfsux 3,4,5",              xForm(31, 3, 4, 5, 695, 0),                           0x7c642d6e },

        // FP D-form update (opcode = base FP opcode + 1).
        { "lfdu  3,8(4)",              dForm(51, 3, 4, 8),                                   0xcc640008 },
        { "stfdu 3,-8(1)",             dForm(55, 3, 1, static_cast<uint16_t>(-8)),           0xdc61fff8 },
        { "lfsu  3,4(4)",              dForm(49, 3, 4, 4),                                   0xc4640004 },
        { "stfsu 3,-4(1)",             dForm(53, 3, 1, static_cast<uint16_t>(-4)),           0xd461fffc },

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
