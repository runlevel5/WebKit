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

        // XO-form (opcode 31, XO=266 = add)
        { "add 3,4,5",                 xoForm(31, 3, 4, 5, 0, 266, 0),                       0x7c642a14 },
        { "add 0,1,2",                 xoForm(31, 0, 1, 2, 0, 266, 0),                       0x7c011214 },

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
