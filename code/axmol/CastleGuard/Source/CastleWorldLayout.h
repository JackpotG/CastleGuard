#pragma once

#include "axmol.h"

#include <algorithm>

namespace castle_world_layout
{
constexpr float WALL_RATIO = 0.12f;
constexpr float WALL_TOTAL_RATIO = 0.18f;
constexpr float PLATFORM_DEPTH_RATIO = 0.10f;
constexpr float INNER_PARAPET_RATIO = 0.012f;

inline float platformDepth(const ax::Size& visibleSize)
{
    return visibleSize.height * PLATFORM_DEPTH_RATIO;
}

inline float obliqueWallTop(const ax::Size& visibleSize)
{
    return visibleSize.height * WALL_TOTAL_RATIO;
}

inline float cityPanDistance(const ax::Size& visibleSize)
{
    return visibleSize.height - obliqueWallTop(visibleSize);
}

inline float cityWallBase(const ax::Size& visibleSize)
{
    return visibleSize.height * (1.0f - WALL_TOTAL_RATIO);
}

inline float battleWallBase(const ax::Size& visibleSize)
{
    return cityWallBase(visibleSize) - cityPanDistance(visibleSize);
}
}  // namespace castle_world_layout
