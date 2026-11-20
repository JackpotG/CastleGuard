#pragma once

#include "axmol.h"

#include <initializer_list>

namespace battlefield_art
{
// 城墙外的静态地形。所有坐标按战场矩形归一化，随可见尺寸一起重画。
inline void drawExterior(ax::DrawNode* art, const ax::Vec2& origin,
                         const ax::Size& visibleSize, float wallTop)
{
    const float width = visibleSize.width;
    const float depth = origin.y + visibleSize.height - wallTop;
    const auto p = [&](float x, float y) {
        return ax::Vec2(origin.x + width * x, wallTop + depth * y);
    };
    const auto poly = [&](std::initializer_list<ax::Vec2> points, const ax::Color4F& color) {
        art->drawSolidPoly(points.begin(), static_cast<int>(points.size()), color);
    };

    art->drawSolidRect(p(0.0f, 0.0f), p(1.0f, 1.0f),
                       ax::Color4F(0.34f, 0.41f, 0.30f, 1.0f));

    // 不规则草地与裸土让道路融入场景，同时留出两侧的刷怪空间。
    poly({p(0.00f, 0.03f), p(0.23f, 0.06f), p(0.29f, 0.27f),
          p(0.18f, 0.48f), p(0.00f, 0.40f)},
         ax::Color4F(0.39f, 0.47f, 0.33f, 1.0f));
    poly({p(0.75f, 0.15f), p(1.00f, 0.07f), p(1.00f, 0.49f),
          p(0.81f, 0.45f), p(0.72f, 0.31f)},
         ax::Color4F(0.38f, 0.45f, 0.32f, 1.0f));
    poly({p(0.00f, 0.68f), p(0.18f, 0.59f), p(0.33f, 0.80f),
          p(0.27f, 1.00f), p(0.00f, 1.00f)},
         ax::Color4F(0.31f, 0.39f, 0.29f, 1.0f));
    poly({p(0.77f, 0.61f), p(1.00f, 0.58f), p(1.00f, 1.00f),
          p(0.74f, 1.00f), p(0.69f, 0.79f)},
         ax::Color4F(0.32f, 0.40f, 0.29f, 1.0f));

    // 路向远处收窄，宽度与城门相接；两条车辙延续同一透视方向。
    poly({p(0.36f, 0.00f), p(0.64f, 0.00f), p(0.565f, 1.00f), p(0.435f, 1.00f)},
         ax::Color4F(0.39f, 0.38f, 0.29f, 1.0f));
    poly({p(0.39f, 0.00f), p(0.61f, 0.00f), p(0.55f, 1.00f), p(0.45f, 1.00f)},
         ax::Color4F(0.49f, 0.44f, 0.32f, 1.0f));
    poly({p(0.425f, 0.00f), p(0.435f, 0.00f), p(0.475f, 1.00f), p(0.471f, 1.00f)},
         ax::Color4F(0.42f, 0.37f, 0.27f, 0.75f));
    poly({p(0.565f, 0.00f), p(0.575f, 0.00f), p(0.529f, 1.00f), p(0.525f, 1.00f)},
         ax::Color4F(0.42f, 0.37f, 0.27f, 0.75f));
    poly({p(0.44f, 0.14f), p(0.55f, 0.19f), p(0.53f, 0.35f), p(0.47f, 0.31f)},
         ax::Color4F(0.54f, 0.48f, 0.35f, 0.5f));
    poly({p(0.48f, 0.58f), p(0.54f, 0.62f), p(0.52f, 0.76f), p(0.46f, 0.72f)},
         ax::Color4F(0.54f, 0.48f, 0.35f, 0.4f));

    struct Detail { float x, y, scale; };
    const Detail trees[] = {
        {0.07f, 0.17f, 1.15f}, {0.91f, 0.23f, 1.05f},
        {0.04f, 0.47f, 0.93f}, {0.94f, 0.55f, 0.88f},
        {0.11f, 0.78f, 0.70f}, {0.91f, 0.84f, 0.65f},
    };
    for (const auto& tree : trees)
    {
        const ax::Vec2 center = p(tree.x, tree.y);
        const float radius = width * 0.043f * tree.scale;
        art->drawSolidCircle(center + ax::Vec2(radius * 0.20f, -radius * 0.30f),
                             radius * 1.1f, 0.0f, 18,
                             ax::Color4F(0.19f, 0.24f, 0.19f, 0.55f));
        art->drawSolidCircle(center, radius, 0.0f, 20,
                             ax::Color4F(0.20f, 0.32f, 0.22f, 1.0f));
        art->drawSolidCircle(center + ax::Vec2(-radius * 0.28f, radius * 0.19f),
                             radius * 0.65f, 0.0f, 16,
                             ax::Color4F(0.30f, 0.43f, 0.27f, 1.0f));
        art->drawSolidCircle(center + ax::Vec2(radius * 0.33f, radius * 0.26f),
                             radius * 0.57f, 0.0f, 16,
                             ax::Color4F(0.36f, 0.48f, 0.30f, 1.0f));
    }

    const Detail rocks[] = {
        {0.16f, 0.12f, 1.2f}, {0.82f, 0.11f, 0.9f}, {0.10f, 0.35f, 0.8f},
        {0.86f, 0.42f, 1.1f}, {0.19f, 0.57f, 0.7f}, {0.78f, 0.71f, 0.6f},
        {0.30f, 0.90f, 0.4f}, {0.66f, 0.91f, 0.45f},
    };
    for (const auto& rock : rocks)
    {
        const ax::Vec2 center = p(rock.x, rock.y);
        const float r = width * 0.015f * rock.scale;
        poly({center + ax::Vec2(-r, -r * 0.4f), center + ax::Vec2(r, -r * 0.4f),
              center + ax::Vec2(r * 0.65f, r * 0.45f),
              center + ax::Vec2(-r * 0.55f, r * 0.65f)},
             ax::Color4F(0.43f, 0.46f, 0.41f, 1.0f));
        art->drawLine(center + ax::Vec2(-r * 0.55f, r * 0.38f),
                      center + ax::Vec2(r * 0.53f, r * 0.20f),
                      ax::Color4F(0.65f, 0.67f, 0.58f, 0.8f), 1.5f);
    }

    const Detail tufts[] = {
        {0.22f, 0.16f, 1.0f}, {0.81f, 0.25f, 1.0f}, {0.14f, 0.30f, 0.8f},
        {0.73f, 0.36f, 0.9f}, {0.27f, 0.48f, 0.9f}, {0.88f, 0.64f, 0.8f},
        {0.17f, 0.68f, 0.7f}, {0.70f, 0.83f, 0.6f}, {0.34f, 0.75f, 0.5f},
    };
    for (const auto& tuft : tufts)
    {
        const ax::Vec2 base = p(tuft.x, tuft.y);
        const float h = width * 0.014f * tuft.scale;
        const ax::Color4F grass(0.19f, 0.34f, 0.22f, 0.9f);
        art->drawLine(base, base + ax::Vec2(-h * 0.6f, h), grass, 2.0f);
        art->drawLine(base, base + ax::Vec2(0.0f, h * 1.2f), grass, 2.0f);
        art->drawLine(base, base + ax::Vec2(h * 0.7f, h * 0.85f), grass, 2.0f);
    }

    // 靠边的旧栅栏只用于环境叙事，仍位于怪物节点下方。
    const ax::Color4F wood(0.31f, 0.26f, 0.18f, 1.0f);
    for (float side : {0.025f, 0.975f})
    {
        const float direction = side < 0.5f ? 1.0f : -1.0f;
        for (int i = 0; i < 3; ++i)
        {
            const ax::Vec2 stake = p(side + direction * 0.035f * i,
                                     0.66f + 0.035f * i);
            art->drawLine(stake + ax::Vec2(0.0f, -8.0f),
                          stake + ax::Vec2(0.0f, 12.0f), wood, 4.0f);
            if (i < 2)
                art->drawLine(stake + ax::Vec2(0.0f, 4.0f),
                              p(side + direction * 0.035f * (i + 1),
                                0.66f + 0.035f * (i + 1)) + ax::Vec2(0.0f, 4.0f),
                              wood, 3.0f);
        }
    }
}
}  // namespace battlefield_art
