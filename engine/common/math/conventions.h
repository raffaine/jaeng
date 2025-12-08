#pragma once

namespace jaeng::math {
    enum class Handedness { Left, Right };
    enum class DepthRange { ZeroToOne, MinusOneToOne };

    struct ClipSpaceConvention {
        Handedness handed = Handedness::Left;
        DepthRange depth  = DepthRange::ZeroToOne;
    };
}
