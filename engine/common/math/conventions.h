#pragma once

namespace jaeng::math {
    enum class Handedness { Left, Right };
    enum class DepthRange { ZeroToOne, MinusOneToOne };

    struct ClipSpaceConvention {
        Handedness handed = Handedness::Left;
        DepthRange depth  = DepthRange::ZeroToOne;

        static constexpr ClipSpaceConvention DirectX() { return { Handedness::Left, DepthRange::ZeroToOne }; }
        static constexpr ClipSpaceConvention Vulkan()  { return { Handedness::Right, DepthRange::ZeroToOne }; } // Vulkan is Right-handed, Y-down in NDC but usually we handle Y in projection
    };
}
