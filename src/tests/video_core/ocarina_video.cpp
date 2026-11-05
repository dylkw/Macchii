// SPDX-License-Identifier: GPL-3.0-or-later

#include <catch2/catch_test_macros.hpp>
#include "video_core/ocarina_video.h"

namespace {
namespace OV = VideoCommon::OcarinaVideo;
using namespace Tegra::Texture;

TICEntry Image(bool output) {
    TICEntry image{};
    image.address_low = output ? 0x800000 : 0x100000;
    image.width_minus_one.Assign(1919);
    image.height_minus_1.Assign(1079);
    image.header_version.Assign(TICHeaderVersion::BlockLinear);
    image.texture_type.Assign(TextureType::Texture2DNoMipmap);
    image.format.Assign(output ? TextureFormat::A2B10G10R10 : TextureFormat::A8B8G8R8);
    const auto type = ComponentType::UNORM;
    image.r_type.Assign(type); image.g_type.Assign(type);
    image.b_type.Assign(type); image.a_type.Assign(type);
    image.x_source.Assign(SwizzleSource::R); image.y_source.Assign(SwizzleSource::G);
    image.z_source.Assign(SwizzleSource::B); image.w_source.Assign(SwizzleSource::A);
    return image;
}
}

TEST_CASE("Ocarina video replacement rejects incompatible image contracts", "[ocarina_video]") {
    REQUIRE(OV::ValidPair(Image(false), Image(true)));
    REQUIRE_FALSE(OV::ValidPair(Image(true), Image(false)));
    for (bool alter_input : {false, true}) {
        const auto rejected = [&](auto mutate) {
            auto input = Image(false), output = Image(true);
            mutate(alter_input ? input : output);
            REQUIRE_FALSE(OV::ValidPair(input, output));
        };
        rejected([](auto& x) { x.address_low = 0; });
        rejected([](auto& x) { x.width_minus_one.Assign(1279); });
        rejected([](auto& x) { x.height_minus_1.Assign(719); });
        rejected([](auto& x) { x.depth_minus_1.Assign(1); });
        rejected([](auto& x) { x.layer_base_0_2.Assign(1); });
        rejected([](auto& x) { x.is_sparse.Assign(1); });
        rejected([](auto& x) { x.srgb_conversion.Assign(1); });
        rejected([](auto& x) { x.r_type.Assign(ComponentType::UINT); });
        rejected([](auto& x) { x.x_source.Assign(SwizzleSource::B); });
        rejected([](auto& x) { x.max_mip_level.Assign(1); });
        rejected([](auto& x) { x.header_version.Assign(TICHeaderVersion::Pitch); });
        rejected([](auto& x) { x.texture_type.Assign(TextureType::Texture2DArray); });
    }
}

TEST_CASE("Ocarina QMD comparison distinguishes work from completion metadata", "[ocarina_video]") {
    OV::Qmd qmd{};
    for (u32 word : {12, 13, 14, 17, 18, 19, 20, 23, 32, 33, 48, 49, 50}) {
        auto changed = qmd;
        changed[word] = 1;
        REQUIRE_FALSE(OV::SameWork(qmd, changed));
    }
    for (u32 word : {4, 15, 16, 24, 25, 26, 28, 29, 30, 52, 53, 54}) {
        auto changed = qmd;
        changed[word] = 1;
        REQUIRE(OV::SameWork(qmd, changed));
    }
    REQUIRE_FALSE(OV::ValidDispatch(qmd, OV::PassCount, 0));
}

TEST_CASE("Ocarina spatial output requires the captured upscale dimensions and encoding", "[ocarina_video]") {
    auto input = Image(false), output = Image(false);
    output.address_low = 0x800000;
    output.width_minus_one.Assign(2559);
    output.height_minus_1.Assign(1439);
    REQUIRE(OV::ValidOutput(input, output));
    for (const auto dimensions : {std::array{1920, 1080}, std::array{2560, 1080},
                                  std::array{1920, 1440}, std::array{1280, 720}}) {
        auto changed = output;
        changed.width_minus_one.Assign(dimensions[0] - 1);
        changed.height_minus_1.Assign(dimensions[1] - 1);
        REQUIRE_FALSE(OV::ValidOutput(input, changed));
    }
    output.srgb_conversion.Assign(1);
    REQUIRE_FALSE(OV::ValidOutput(input, output));
    REQUIRE_FALSE(OV::ValidOutput(input, Image(true)));
}
