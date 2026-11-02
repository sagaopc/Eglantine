// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

TEST_CASE("Ampere VOTEU writes uniform registers above UR15", "[video_core][ampere]") {
    // VOTEU.ANY UR16, UPT, PT (a Donkey Kong Bananza compute program): the ballot goes to R232.
    const std::array<Instruction, 2> code{{{{0x00107886, 0, 0x038e0100, 0x000fe400}}, EXIT}};
    const auto lowered = LowerToMaxwell(code);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.at(0) == 0x50d9e380000700e8ULL);
}

TEST_CASE("Ampere uniform moves and bit operations reach UR16 and up", "[video_core][ampere]") {
    const std::array<std::pair<Instruction, u64>, 6> cases{{
        {{{0x00167882, 0x1234, 0, 0}}, 0x010000012347f0f0ULL},         // UMOV UR22, 0x1234 (R240)
        {{{0x00117c82, 3, 0x08000000, 0}}, 0x5c9807800db700e9ULL},     // UMOV UR17, UR3
        {{{0x001272bd, 3, 0x080e0000, 0}}, 0x5c3000000db700eaULL},     // UFLO.U32 UR18, UR3
        {{{0x001372be, 3, 0x08000000, 0}}, 0x382801800007dbebULL},     // UBREV UR19, UR3
        {{{0x000472bf, 20, 0x08000000, 0}}, 0x5c0800000ec700dcULL},    // UPOPC UR4, UR20
        {{{0x00047c02, 16, 0x08000f00, 0}}, 0x5c9807800e870004ULL},    // MOV R4, UR16
    }};
    for (const auto& [instruction, expected] : cases) {
        const std::array<Instruction, 2> code{{instruction, EXIT}};
        const auto lowered = LowerToMaxwell(code);
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.at(0) == expected);
    }
}
} // namespace
