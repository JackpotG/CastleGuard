#pragma once

#include "axmol.h"

namespace wall_supply_art
{
enum class Kind
{
    Coins,
    Crystals,
};

inline void drawOpenCrate(ax::DrawNode* art, const ax::Rect& bounds, Kind kind)
{
    const float x = bounds.getMinX();
    const float y = bounds.getMinY();
    const float w = bounds.size.width;
    const float h = bounds.size.height;
    const bool coins = kind == Kind::Coins;
    const ax::Color4F face = coins ? ax::Color4F(0.38f, 0.25f, 0.15f, 1.0f)
                                     : ax::Color4F(0.20f, 0.34f, 0.39f, 1.0f);
    const ax::Color4F trim = coins ? ax::Color4F(0.69f, 0.54f, 0.29f, 1.0f)
                                     : ax::Color4F(0.55f, 0.72f, 0.75f, 1.0f);

    art->drawSolidRect(ax::Vec2(x + 3.0f, y - 3.0f),
                       ax::Vec2(x + w + 3.0f, y + h * 0.46f),
                       ax::Color4F(0.13f, 0.17f, 0.18f, 0.55f));
    const ax::Vec2 lid[] = {
        ax::Vec2(x + 4.0f, y + h * 0.55f), ax::Vec2(x + w - 4.0f, y + h * 0.55f),
        ax::Vec2(x + w - 11.0f, y + h * 0.94f), ax::Vec2(x + 11.0f, y + h * 0.94f),
    };
    art->drawSolidPoly(lid, 4,
                       ax::Color4F(face.r * 1.15f, face.g * 1.15f, face.b * 1.15f, 1.0f));
    art->drawLine(lid[3], lid[2], trim, 2.0f);
    art->drawLine(lid[0], lid[3], trim, 1.5f);
    art->drawLine(lid[1], lid[2], trim, 1.5f);
    const ax::Vec2 opening[] = {
        ax::Vec2(x + 5.0f, y + h * 0.43f), ax::Vec2(x + w - 5.0f, y + h * 0.43f),
        ax::Vec2(x + w - 12.0f, y + h * 0.68f), ax::Vec2(x + 12.0f, y + h * 0.68f),
    };
    art->drawSolidPoly(opening, 4, ax::Color4F(0.09f, 0.13f, 0.15f, 1.0f));

    const float spots[][2] = {
        {0.20f, 0.56f}, {0.33f, 0.67f}, {0.48f, 0.60f}, {0.62f, 0.69f},
        {0.77f, 0.58f}, {0.27f, 0.79f}, {0.52f, 0.82f}, {0.72f, 0.77f},
        {0.12f, 0.67f}, {0.87f, 0.68f}, {0.40f, 0.75f}, {0.61f, 0.76f},
    };
    for (const auto& spot : spots)
    {
        const ax::Vec2 p(x + w * spot[0], y + h * spot[1]);
        if (coins)
        {
            art->drawSolidCircle(p, 4.3f, 0.0f, 12,
                                 ax::Color4F(0.94f, 0.67f, 0.16f, 1.0f));
            art->drawCircle(p, 3.0f, 0.0f, 12, false,
                            ax::Color4F(1.0f, 0.86f, 0.38f, 1.0f), 1.0f);
        }
        else
        {
            const ax::Vec2 gem[] = {
                p + ax::Vec2(0.0f, 6.0f), p + ax::Vec2(4.0f, 0.0f),
                p + ax::Vec2(0.0f, -5.0f), p + ax::Vec2(-4.0f, 0.0f),
            };
            art->drawSolidPoly(gem, 4, ax::Color4F(0.34f, 0.83f, 0.94f, 1.0f));
            art->drawLine(gem[0], gem[2], ax::Color4F(0.72f, 0.97f, 1.0f, 1.0f), 1.0f);
        }
    }

    art->drawSolidRect(ax::Vec2(x, y), ax::Vec2(x + w, y + h * 0.44f), face);
    art->drawSolidRect(ax::Vec2(x, y + h * 0.43f),
                       ax::Vec2(x + w, y + h * 0.49f), trim);
    art->drawRect(ax::Vec2(x, y), ax::Vec2(x + w, y + h * 0.44f), trim, 1.5f);
    for (float strapX : {x + 6.0f, x + w - 9.0f})
        art->drawSolidRect(ax::Vec2(strapX, y + 2.0f),
                           ax::Vec2(strapX + 2.5f, y + h * 0.42f), trim);

    for (int i = 0; i < 3; ++i)
    {
        const float looseX = coins ? x + w * (0.52f + i * 0.15f)
                                   : x + w * (0.18f + i * 0.15f);
        const ax::Vec2 p(looseX, y - 3.0f - (i % 2) * 3.0f);
        if (coins)
        {
            art->drawSolidCircle(p, 3.3f, 0.0f, 10,
                                 ax::Color4F(0.94f, 0.67f, 0.16f, 1.0f));
            art->drawCircle(p, 2.4f, 0.0f, 10, false,
                            ax::Color4F(1.0f, 0.86f, 0.38f, 1.0f), 1.0f);
        }
        else
        {
            const ax::Vec2 gem[] = {
                p + ax::Vec2(0.0f, 4.0f), p + ax::Vec2(3.0f, 0.0f),
                p + ax::Vec2(0.0f, -3.0f), p + ax::Vec2(-3.0f, 0.0f),
            };
            art->drawSolidPoly(gem, 4, ax::Color4F(0.34f, 0.83f, 0.94f, 1.0f));
        }
    }
}
}  // namespace wall_supply_art
