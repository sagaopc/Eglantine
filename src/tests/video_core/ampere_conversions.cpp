// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

TEST_CASE("Ampere F2I lowers the ceiling rounding mode", "[video_core][ampere]") {
    // F2I.U32.CEIL.NTZ R2, R2 (a Donkey Kong Bananza compute program)
    const std::array<Instruction, 2> code{{{{0x00027305, 0x00000002, 0x0020b000, 0x000e3000}}, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    // Maxwell F2I.U32.CEIL R2, R250 after the NaN clear and the 2^32 clamp.
    REQUIRE(lowered.instructions.at(6) == 0x5cb001000fa70a02ULL);
}

TEST_CASE("Ampere F2I lowers 8-bit results as saturated 32-bit ones", "[video_core][ampere]") {
    // F2I.FTZ.S8.TRUNC.NTZ R21, R3 (a Donkey Kong Bananza pixel shader)
    const std::array<Instruction, 2> code{{{{0x00157305, 0x00000003, 0x0021e100, 0x0001e400}}, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    const std::array<u64, 5> expected{
        0x5cb0118000371a15ULL, // F2I.FTZ.S32.TRUNC R21, R3
        0x010ffffff807f0fcULL, // MOV32I R252, -128
        0x5c2107800fc71515ULL, // IMNMX.S32 R21, R21, R252, !PT (max)
        0x0100000007f7f0fcULL, // MOV32I R252, 127
        0x5c2103800fc71515ULL, // IMNMX.S32 R21, R21, R252, PT (min)
    };
    for (size_t i = 0; i < expected.size(); ++i) {
        REQUIRE(lowered.instructions.at(i) == expected[i]);
    }
}

TEST_CASE("Ampere F2I still rejects 64-bit results and conversions without NTZ",
          "[video_core][ampere]") {
    for (const u32 mode : {0x0021f800U, 0x0021d000U}) {
        const std::array<Instruction, 2> code{{{{0x00047305, 0x00000005, mode, 0}}, EXIT}};
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(!static_cast<bool>(lowered));
        REQUIRE(lowered.error == "native float-to-integer conversion mode");
    }
}

TEST_CASE("Ampere indexed LDC lowers byte and halfword loads", "[video_core][ampere]") {
    // @!P1 LDC.U8 R19, c[0xf][RZ + 0x20]
    const std::array<Instruction, 2> code{{{{0xff139b82, 0x03c00800, 0x00000000, 0x000e6200}}, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.at(0) == 0xef9000f00209ff13ULL);

    // LDC.S16 R4, c[0xf][R0 + 0x20]
    const std::array<Instruction, 2> halfword{{{{0x00047b82, 0x03c00800, 0x00000600, 0}}, EXIT}};
    const auto lowered_halfword = LowerToMaxwell(halfword);
    REQUIRE(static_cast<bool>(lowered_halfword));
    REQUIRE(lowered_halfword.instructions.at(0) == 0xef9300f002070004ULL);
}
} // namespace

namespace {
TEST_CASE("Ampere HSETP2 with a register operand", "[video_core][ampere]") {
    // HSETP2.EQ.AND P2, PT, R44.H0_H0, R44.H1_H1, P2 (a compute program of Donkey Kong Bananza).
    const std::array<Shader::Ampere::Instruction, 2> code{{
        {{0x2c007234, 0x3000002c, 0x01742800, 0}},
        {{0x0000794d, 0, 0x03800000, 0}},
    }};
    const auto lowered = Shader::Ampere::LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.front() == 0x5d21011032c72c17ULL);
}
} // namespace
