// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <span>
#include <unordered_map>

#include <catch2/catch_test_macros.hpp>
#include <sirit/sirit.h>

#include "shader_recompiler/backend/spirv/emit_spirv.h"
#include "shader_recompiler/frontend/ir/ir_emitter.h"

TEST_CASE("SPIR-V CAS helpers atomically read the word they compare",
          "[shader][spirv][atomic_cas]") {
    namespace IR = Shader::IR;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.stage = Shader::Stage::Compute;
    program.workgroup_size = {1, 1, 1};
    program.shared_memory_size = 64;
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    IR::AbstractSyntaxNode body{};
    body.type = IR::AbstractSyntaxNode::Type::Block;
    body.data.block = &block;
    IR::AbstractSyntaxNode end{};
    end.type = IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {body, end};
    auto& info = program.info;
    info.storage_buffers_descriptors.push_back({0, 0x20, 1, true});
    info.uses_atomic_f32_add = true;
    info.uses_fp16 = true;
    info.uses_atomic_f16x2_add = true;
    info.uses_atomic_f32x2_add = true;
    info.uses_global_increment = true;
    info.uses_global_decrement = true;
    info.uses_shared_increment = true;
    info.uses_shared_decrement = true;
    info.uses_int8 = true;
    info.uses_int16 = true;
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);

    std::unordered_map<u32, std::span<const u32>> reads;
    size_t comparisons{};
    for (size_t offset = 5; offset < code.size();) {
        const u32 length = code[offset] >> 16;
        REQUIRE(length > 0);
        REQUIRE(offset + length <= code.size());
        const std::span instruction{code.data() + offset, length};
        const u32 opcode = code[offset] & 0xffff;
        if (opcode == 61 || opcode == 227) { // OpLoad or OpAtomicLoad
            reads.emplace(instruction[2], instruction);
        } else if (opcode == 230) { // OpAtomicCompareExchange
            ++comparisons;
            const auto expected = reads.find(instruction[8]);
            REQUIRE(expected != reads.end());
            CHECK((expected->second[0] & 0xffff) == 227);
            CHECK(expected->second[3] == instruction[3]);
            REQUIRE(expected->second.size() == 6);
            CHECK(expected->second[4] == instruction[4]);
            CHECK(expected->second[5] == instruction[5]);
        }
        offset += length;
    }
    CHECK(comparisons == 10);
}


#ifdef SIRIT_HAS_ARRAY_LENGTH
TEST_CASE("Native global fallbacks check the bound array before accessing it",
          "[shader][spirv][native_bounds]") {
    namespace IR = Shader::IR;
    Shader::ObjectPool<IR::Inst> pool;
    IR::Block block{pool};
    IR::Program program;
    program.stage = Shader::Stage::Compute;
    program.workgroup_size = {1, 1, 1};
    program.blocks = {&block};
    program.post_order_blocks = {&block};
    IR::AbstractSyntaxNode body{};
    body.type = IR::AbstractSyntaxNode::Type::Block;
    body.data.block = &block;
    IR::AbstractSyntaxNode end{};
    end.type = IR::AbstractSyntaxNode::Type::Return;
    program.syntax_list = {body, end};
    program.info.uses_int64 = true;
    program.info.uses_global_memory = true;
    program.info.native_global_bounds = true;
    program.info.constant_buffer_descriptors.push_back({0, 1});
    program.info.storage_buffers_descriptors.push_back({0, 0x20, 1, true});
    program.info.storage_buffers_descriptors.push_back({0, 0x30, 1, true});
    Shader::Profile profile{};
    profile.supported_spirv = 0x00010400;
    profile.support_int64 = true;
    profile.min_ssbo_alignment = 16;
    profile.unified_descriptor_binding = true;
    profile.needs_native_buffer_bounds = true;
    const auto code = Shader::Backend::SPIRV::EmitSPIRV(profile, program);

    size_t lengths{};
    std::unordered_map<u32, u32> extended_lengths;
    std::unordered_map<u32, bool> checked_lengths;
    for (size_t offset = 5; offset < code.size();) {
        const u32 length = code[offset] >> 16;
        REQUIRE(length > 0);
        REQUIRE(offset + length <= code.size());
        const std::span instruction{code.data() + offset, length};
        const u32 opcode = code[offset] & 0xffff;
        if (opcode == 68) { // OpArrayLength
            ++lengths;
            REQUIRE(length == 5);
            CHECK(instruction[4] == 0);
            checked_lengths.emplace(instruction[2], false);
        } else if (opcode == 113 && checked_lengths.contains(instruction[3])) { // OpUConvert
            extended_lengths.emplace(instruction[2], instruction[3]);
        } else if (opcode == 176 && extended_lengths.contains(instruction[4])) { // OpULessThan
            checked_lengths.at(extended_lengths.at(instruction[4])) = true;
        }
        offset += length;
    }
    REQUIRE(lengths > 0);
    for (const auto& [id, checked] : checked_lengths) {
        INFO(id);
        CHECK(checked);
    }
}
#endif
