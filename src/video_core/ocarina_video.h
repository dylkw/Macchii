// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include "common/common_types.h"
#include "video_core/native_qmd.h"
#include "video_core/textures/texture.h"

namespace VideoCommon::OcarinaVideo {

constexpr u64 ProgramId = 0x0400486024DEC000ULL;
constexpr u32 PassCount = 13;
constexpr u32 RecordStride = 0x200;
using Qmd = Tegra::NativeQmdScheduler::Descriptor;
using Constants = std::array<u32, 64>;

struct Pass {
    u64 key;
    u32 bytes;
    u32 groups;
    u32 block_y;
    u32 shared;
    u32 block_x{32};
    u32 groups_y{1};
};

// Exact native programs of the video enhancement and neural upscale after YUV conversion.
constexpr std::array<Pass, PassCount> Passes{{
    {0x2f5791cea2a85397, 22160, 900, 1, 3584},
    {0x313d724f3e214203, 11472, 720, 1, 2816},
    {0xdf2c83cf2ad70790, 5840, 97, 2, 3072},
    {0x1fdb95ea2565f49c, 46944, 120, 1, 4352},
    {0x0be2f024aea21a6c, 44016, 70, 4, 5120},
    {0x0618cc4a3ac8f33a, 18752, 60, 4, 21760},
    {0x0618cc4a3ac8f33a, 18752, 60, 4, 21760},
    {0xe27336d38f1cc8f4, 68784, 85, 2, 3072},
    {0x1fdb95ea2565f49c, 46944, 120, 1, 4352},
    {0xecebb02dec6faa17, 3920, 8100, 1, 1280},
    {0x313d724f3e214203, 11472, 720, 1, 2816},
    {0x2a366b38546a4d5e, 18848, 5400, 1, 2816},
    {0x6093c9143ba0904d, 6352, 80, 1, 4608, 128, 180},
}};

inline u64 ConstantAddress(const Qmd& qmd) {
    return (u64{qmd[33] & 0xff} << 32) | qmd[32];
}

inline bool ValidDispatch(const Qmd& qmd, u32 pass, u64 constant_address) {
    if (pass >= PassCount) {
        return false;
    }
    const auto& expected = Passes[pass];
    return qmd[12] == expected.groups && qmd[13] == expected.groups_y && qmd[14] == 1 &&
           qmd[18] == ((expected.block_x << 16) | 0xff30) &&
           qmd[19] == (0x10000 | expected.block_y) &&
           (qmd[17] & 0x3ffff) == expected.shared && (qmd[20] & 0xff) == 0xf9 &&
           ConstantAddress(qmd) == constant_address && ((qmd[33] >> 19) & 0x1fff) == 16 &&
           qmd[4] == 0x5f && qmd[16] == 9 && (qmd[25] & ~0xffU) == 0x44800000 &&
           qmd[26] == 1 && qmd[29] == 0 && qmd[53] == 0;
}

inline bool SameWork(const Qmd& a, const Qmd& b) {
    // Dependency and release payloads may be patched by the SDK after submission.
    for (const u32 word : {12, 13, 14, 17, 18, 19, 20, 23}) {
        if (a[word] != b[word]) return false;
    }
    for (u32 word = 32; word <= 50; ++word) {
        if (a[word] != b[word]) return false;
    }
    return true;
}

inline bool ValidImage(const Tegra::Texture::TICEntry& image,
                       Tegra::Texture::TextureFormat format,
                       Tegra::Texture::ComponentType component,
                       u32 width = 1920, u32 height = 1080) {
    using namespace Tegra::Texture;
    return (image.texture_type == TextureType::Texture2D ||
            image.texture_type == TextureType::Texture2DNoMipmap) && image.IsBlockLinear() &&
           image.Address() != 0 && image.Width() == width && image.Height() == height &&
           image.Depth() == 1 && image.BaseLayer() == 0 && image.max_mip_level == 0 &&
           image.res_min_mip_level == 0 && image.res_max_mip_level == 0 &&
           image.msaa_mode == MsaaMode::Msaa1x1 && image.is_sparse == 0 &&
           image.block_width == 0 && image.block_depth == 0 && image.srgb_conversion == 0 &&
           image.format == format && image.r_type == component && image.g_type == component &&
           image.b_type == component && image.a_type == component &&
           image.x_source == SwizzleSource::R && image.y_source == SwizzleSource::G &&
           image.z_source == SwizzleSource::B && image.w_source == SwizzleSource::A;
}

inline bool ValidOutput(const Tegra::Texture::TICEntry& input,
                        const Tegra::Texture::TICEntry& output) {
    using namespace Tegra::Texture;
    return ValidImage(input, TextureFormat::A8B8G8R8, ComponentType::UNORM) &&
           ValidImage(output, TextureFormat::A8B8G8R8, ComponentType::UNORM, 2560, 1440) &&
           input.Address() != output.Address();
}

inline bool ValidPair(const Tegra::Texture::TICEntry& input,
                      const Tegra::Texture::TICEntry& output) {
    using namespace Tegra::Texture;
    return ValidImage(input, TextureFormat::A8B8G8R8, ComponentType::UNORM) &&
           ValidImage(output, TextureFormat::A2B10G10R10, ComponentType::UNORM) &&
           input.Address() != output.Address();
}

} // namespace VideoCommon::OcarinaVideo
