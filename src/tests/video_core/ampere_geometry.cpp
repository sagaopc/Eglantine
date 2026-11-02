// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

Shader::Ampere::LoweredProgram Lower(Instruction instruction, bool geometry) {
    const std::array<Instruction, 2> code{{instruction, EXIT}};
    return LowerToMaxwell(code, {.geometry = geometry});
}

TEST_CASE("Ampere geometry ISBERD copies the input vertex handle", "[video_core][ampere]") {
    // ISBERD R5, [R3]: Maxwell ISBERD R5, R3 (default mode, a copy in the frontend).
    const auto lowered = Lower({{0x03057923, 0, 0, 0}}, true);
    REQUIRE(static_cast<bool>(lowered));
    REQUIRE(lowered.instructions.at(0) == 0xefd0000000070305ULL);
    // Outside a geometry program the map region is still rejected (task/mesh stage buffer).
    REQUIRE(!static_cast<bool>(Lower({{0x03057923, 0, 0, 0}}, false)));
    // An offset or another region is not a plain vertex handle.
    REQUIRE(!static_cast<bool>(Lower({{0x03057923, 0x100, 0, 0}}, true)));
    REQUIRE(!static_cast<bool>(Lower({{0x03057923, 0, 0x3000, 0}}, true)));
}

TEST_CASE("Ampere geometry OUT emits and cuts on stream zero", "[video_core][ampere]") {
    const std::array<std::pair<u32, u64>, 3> cases{{
        {0x4000, 0xfbe000800ff70406ULL}, // OUT.EMIT R6, R4, RZ
        {0x8000, 0xfbe001000ff70406ULL}, // OUT.CUT
        {0xc000, 0xfbe001800ff70406ULL}, // OUT.EMIT.CUT
    }};
    for (const auto& [mode, expected] : cases) {
        const auto lowered = Lower({{0x04067324, 255, mode, 0}}, true);
        REQUIRE(static_cast<bool>(lowered));
        REQUIRE(lowered.instructions.at(0) == expected);
    }
    // No emit or cut (a finalization), another stream, or outside a geometry program: rejected.
    REQUIRE(Lower({{0x04067324, 255, 0, 0}}, true).error == "native geometry output mode");
    REQUIRE(!static_cast<bool>(Lower({{0x04067324, 7, 0x4000, 0}}, true)));
    REQUIRE(!static_cast<bool>(Lower({{0x04067324, 255, 0x4000, 0}}, false)));
}

TEST_CASE("Ampere geometry reads an input attribute through its vertex handle",
          "[video_core][ampere]") {
    // S2R R0, SR_INVOCATION_INFO; PRMT R1, R0, 0x7770, RZ (its low byte: the primitive's first
    // entry); ISBERD R2, [R1]; ALD R4, a[0x70], R2.
    const std::vector<Instruction> code{
        {{0x00007919, 0, 0x1d00, 0}}, {{0x00017816, 0x7770, 255, 0}},
        {{0x01027923, 0, 0, 0}},      {{0xff047321, 0x02 | (0x70U << 8), 0, 0}}, EXIT};
    const auto lowered = LowerToMaxwell(code, {.geometry = true});
    REQUIRE(static_cast<bool>(lowered));
    const auto& out = lowered.instructions;
    REQUIRE(out.front() == 0xf0c8000001d70000ULL);      // S2R R0, SR_INVOCATION_INFO
    REQUIRE(out.at(out.size() - 3) == 0xefd0000000070102ULL); // ISBERD R2, R1
    REQUIRE(out.at(out.size() - 2) == 0xefd801000707ff04ULL); // ALD R4, a[0x70], R2
}

TEST_CASE("Ampere geometry viewport mask stores can be dropped", "[video_core][ampere]") {
    // AST a[0x3a0], R1 (ViewportMask): the culling result of a passthrough geometry program.
    const std::array<Instruction, 2> code{{{{0xff007322, 0x0003a001, 0, 0}}, EXIT}};
    const auto kept = LowerToMaxwell(code, {.geometry = true});
    REQUIRE(static_cast<bool>(kept));
    REQUIRE(kept.instructions.front() == 0xeff000003a07ff01ULL); // AST a[0x3a0], R1, R0
    const auto dropped = LowerToMaxwell(code, {.geometry = true, .keep_all_primitives = true});
    REQUIRE(static_cast<bool>(dropped));
    REQUIRE(dropped.instructions.size() == kept.instructions.size() - 1);
}
} // namespace
