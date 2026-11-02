// SPDX-License-Identifier: GPL-3.0-or-later

#include <vector>
#include <catch2/catch_test_macros.hpp>

#include "shader_recompiler/frontend/ampere/lower_to_maxwell.h"
#include "shader_recompiler/frontend/ampere/program_capture.h"

namespace {
using Shader::Ampere::Instruction;
using Shader::Ampere::LowerToMaxwell;
using Shader::Ampere::ProgramCaptureState;

constexpr Instruction EXIT{{0x0000794d, 0, 0x03800000, 0}};

// The PIC ATEXIT prologue of a passthrough geometry program (as Donkey Kong Bananza's culling
// program registers it), an EXIT that runs the handler, and the handler itself at 0x60.
std::vector<Instruction> AtexitProgram(bool imad_carry) {
    return {
        {{0x0000734e, 0, 0, 0}},             // LEPC R0
        {{0x00037802, 0x60, 0x00000f00, 0}}, // MOV R3, 0x60
        {{0x00007210, 3, 0x07f1e0ff, 0}},    // IADD3 R0, P0, R0, R3, RZ
        imad_carry ? Instruction{{0xff017224, 0xff, 0x000e0401, 0}}  // IMAD.X R1, RZ, RZ, R1, P0
                   : Instruction{{0x01017210, 0xff, 0x007fe4ff, 0}}, // IADD3.X R1, R1, RZ, RZ, P0
        {{0x00007357, 0, 0, 0}},             // BMOV.64 ATEXIT_PC, R0
        EXIT,
        {{0x00ff7324, 255, 0, 0}},           // OUT.FINAL RZ, R0, RZ
        {{0x00007957, 0, 0, 0}},             // BMOV.64 ATEXIT_PC, RZ
        EXIT,
    };
}

TEST_CASE("Ampere ATEXIT prologue lowers with a verified finalization handler",
          "[video_core][ampere]") {
    for (const bool imad_carry : {false, true}) {
        const auto code = AtexitProgram(imad_carry);
        const auto lowered = LowerToMaxwell(code, {.geometry = true});
        REQUIRE(static_cast<bool>(lowered));
        // The program ends at its own EXIT; the handler only finalizes the primitive.
        REQUIRE(lowered.source_size == 6 * sizeof(Instruction));
        REQUIRE(lowered.instructions.back() == 0xe30000000007000fULL);
    }
}

TEST_CASE("Ampere ATEXIT needs the handler it registers", "[video_core][ampere]") {
    auto code = AtexitProgram(true);
    code.resize(6); // handler not captured
    REQUIRE(LowerToMaxwell(code, {.geometry = true}).error ==
            "native unverified ATEXIT finalization epilogue");
    code = AtexitProgram(true);
    code[6].words[2] = 0x4000; // the handler emits a vertex: not a plain finalization
    REQUIRE(!static_cast<bool>(LowerToMaxwell(code, {.geometry = true})));
    // LEPC anywhere else stays unsupported.
    const std::vector<Instruction> lepc{{{0x0000734e, 0, 0, 0}}, EXIT};
    REQUIRE(!static_cast<bool>(LowerToMaxwell(lepc, {.geometry = true})));
}

TEST_CASE("Ampere program capture continues through the ATEXIT handler", "[video_core][ampere]") {
    const auto code = AtexitProgram(true);
    ProgramCaptureState capture;
    u32 end = 0;
    for (u32 index = 0; index < code.size(); ++index) {
        if (capture.Observe(code[index], index) == ProgramCaptureState::Result::Complete) {
            end = index;
            break;
        }
    }
    REQUIRE(end == 8);
}
} // namespace
