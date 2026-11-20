#include "ModeHomePanel.h"

#include "CastleWorldLayout.h"
#include "GameData.h"

#include <algorithm>

using namespace ax;

namespace
{
constexpr float PRIMARY_W = 300.0f;
constexpr float PRIMARY_H = 76.0f;
constexpr float SECONDARY_W = 260.0f;
constexpr float SECONDARY_H = 64.0f;

void drawButton(DrawNode* background, const Size& size, const Color4F& fill, const Color4F& line)
{
    background->clear();
    background->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height), fill);
    background->drawRect(Vec2::ZERO, Vec2(size.width, size.height), line, 2.0f);
}

struct BuildingSketch
{
    DrawNode* art;
    Rect bounds;

    Vec2 at(float u, float v) const
    {
        return Vec2(bounds.getMinX() + bounds.size.width * u,
                    bounds.getMinY() + bounds.size.height * v);
    }

    void rect(float x0, float y0, float x1, float y1, const Color4F& color) const
    {
        art->drawSolidRect(at(x0, y0), at(x1, y1), color);
    }

    void quad(float ax, float ay, float bx, float by, float cx, float cy,
              float dx, float dy, const Color4F& color) const
    {
        const Vec2 points[] = {at(ax, ay), at(bx, by), at(cx, cy), at(dx, dy)};
        art->drawSolidPoly(points, 4, color);
    }

    void line(float x0, float y0, float x1, float y1, const Color4F& color,
              float width = 2.0f) const
    {
        art->drawLine(at(x0, y0), at(x1, y1), color, width);
    }
};

Color4F darken(const Color4F& color, float amount)
{
    return Color4F(color.r * amount, color.g * amount, color.b * amount, color.a);
}

void drawBuildingShell(const BuildingSketch& b, const Color4F& wall, const Color4F& roof,
                       float wallTop, float roofBack, float roofWidthScale = 1.0f)
{
    b.quad(0.10f, -0.04f, 1.04f, -0.04f, 0.98f, 0.13f, 0.15f, 0.13f,
           Color4F(0.17f, 0.27f, 0.23f, 0.38f));

    // 屋面纵深比正面墙高，形成向下看的视角；墙体也向画面上方收分。
    b.quad(0.05f, 0.02f, 0.95f, 0.02f, 0.89f, wallTop, 0.11f, wallTop, wall);
    b.quad(0.05f, 0.02f, 0.95f, 0.02f, 0.94f, 0.10f, 0.06f, 0.10f,
           darken(wall, 0.72f));
    b.quad(0.05f, 0.02f, 0.12f, 0.02f, 0.18f, wallTop, 0.11f, wallTop,
           darken(wall, 0.81f));
    b.quad(0.88f, 0.02f, 0.95f, 0.02f, 0.89f, wallTop, 0.82f, wallTop,
           darken(wall, 0.81f));
    // 整体轮廓保持梯形，屋脊两端将其分成前后坡与左右侧面。
    const auto roofX = [roofWidthScale](float x) {
        return 0.5f + (x - 0.5f) * roofWidthScale;
    };
    const float frontLeft = roofX(-0.04f);
    const float frontRight = roofX(1.04f);
    const float ridgeLeft = roofX(0.14f);
    const float ridgeRight = roofX(0.86f);
    const float backLeft = roofX(0.04f);
    const float backRight = roofX(0.96f);
    const float ridge = wallTop + (roofBack - wallTop) * 0.68f;
    const Color4F backSlope(roof.r * 1.10f, roof.g * 1.10f, roof.b * 1.10f, roof.a);
    b.quad(frontLeft, wallTop, frontRight, wallTop, backRight, roofBack, backLeft, roofBack,
           darken(roof, 0.78f));
    b.art->drawSolidTriangle(b.at(backRight, roofBack), b.at(frontRight, wallTop),
                             b.at(ridgeRight, ridge), darken(roof, 0.84f),
                             darken(roof, 0.84f));
    b.quad(ridgeLeft, ridge, ridgeRight, ridge, backRight, roofBack, backLeft, roofBack,
           backSlope);
    b.quad(frontLeft, wallTop, frontRight, wallTop, ridgeRight, ridge, ridgeLeft, ridge,
           roof);
    b.line(frontLeft, wallTop, frontRight, wallTop, darken(roof, 0.56f), 3.0f);
    b.line(ridgeLeft, ridge, ridgeRight, ridge, darken(roof, 0.75f), 1.5f);
    b.line(backLeft, roofBack, ridgeLeft, ridge, darken(roof, 0.72f), 1.0f);
    b.line(ridgeLeft, ridge, frontLeft, wallTop, darken(roof, 0.72f), 1.0f);
    b.line(backRight, roofBack, ridgeRight, ridge, darken(roof, 0.72f), 1.0f);
    b.line(ridgeRight, ridge, frontRight, wallTop, darken(roof, 0.72f), 1.0f);
}

void drawArmory(DrawNode* art, const Rect& bounds)
{
    const BuildingSketch b{art, bounds};
    const Color4F iron(0.20f, 0.25f, 0.25f, 1.0f);
    const Color4F brass(0.78f, 0.65f, 0.40f, 1.0f);
    drawBuildingShell(b, Color4F(0.61f, 0.54f, 0.46f, 1.0f),
                      Color4F(0.56f, 0.25f, 0.20f, 1.0f), 0.38f, 0.90f, 0.92f);

    b.quad(0.16f, 0.80f, 0.34f, 0.80f, 0.30f, 0.88f, 0.20f, 0.88f,
           Color4F(0.30f, 0.21f, 0.19f, 1.0f));
    b.quad(0.20f, 0.83f, 0.29f, 0.83f, 0.28f, 1.01f, 0.21f, 1.01f,
           Color4F(0.45f, 0.37f, 0.31f, 1.0f));
    b.quad(0.20f, 0.83f, 0.23f, 0.83f, 0.24f, 1.01f, 0.21f, 1.01f,
           Color4F(0.36f, 0.29f, 0.25f, 1.0f));
    b.rect(0.18f, 0.99f, 0.31f, 1.04f, iron);

    b.quad(0.37f, 0.08f, 0.63f, 0.08f, 0.60f, 0.35f, 0.40f, 0.35f, iron);
    b.quad(0.41f, 0.08f, 0.59f, 0.08f, 0.57f, 0.31f, 0.43f, 0.31f,
           Color4F(0.52f, 0.27f, 0.16f, 1.0f));
    b.line(0.50f, 0.08f, 0.50f, 0.31f, brass);
    b.rect(0.43f, 0.15f, 0.47f, 0.18f, brass);
    b.rect(0.53f, 0.15f, 0.57f, 0.18f, brass);
    b.line(0.19f, 0.10f, 0.19f, 0.32f, iron, 3.0f);
    b.line(0.25f, 0.10f, 0.25f, 0.32f, iron, 3.0f);
    b.line(0.15f, 0.15f, 0.29f, 0.15f, brass, 3.0f);
    for (float x : {0.19f, 0.25f})
        art->drawSolidTriangle(b.at(x, 0.36f), b.at(x - 0.035f, 0.30f),
                               b.at(x + 0.035f, 0.30f), iron, iron);
    art->drawSolidCircle(b.at(0.79f, 0.24f), bounds.size.width * 0.07f,
                         0.0f, 14, iron);
    b.line(0.74f, 0.24f, 0.84f, 0.24f, brass);
    b.line(0.79f, 0.18f, 0.79f, 0.30f, brass);
}

void drawAcademy(DrawNode* art, const Rect& bounds)
{
    const BuildingSketch b{art, bounds};
    const Color4F glass(0.26f, 0.57f, 0.66f, 1.0f);
    const Color4F frame(0.73f, 0.66f, 0.44f, 1.0f);
    drawBuildingShell(b, Color4F(0.76f, 0.76f, 0.69f, 1.0f),
                      Color4F(0.20f, 0.43f, 0.49f, 1.0f), 0.38f, 0.87f);

    b.quad(0.33f, 0.76f, 0.67f, 0.76f, 0.62f, 0.84f, 0.38f, 0.84f,
           Color4F(0.15f, 0.34f, 0.39f, 1.0f));
    art->drawSolidCircle(b.at(0.50f, 1.01f), bounds.size.width * 0.12f,
                         0.0f, 24, glass);
    b.quad(0.39f, 0.79f, 0.61f, 0.79f, 0.60f, 1.02f, 0.40f, 1.02f,
           Color4F(0.60f, 0.67f, 0.65f, 1.0f));
    b.quad(0.39f, 0.79f, 0.43f, 0.79f, 0.44f, 1.02f, 0.40f, 1.02f,
           Color4F(0.48f, 0.56f, 0.56f, 1.0f));
    b.line(0.38f, 1.02f, 0.62f, 1.02f, frame, 3.0f);

    for (float x : {0.24f, 0.76f})
    {
        b.quad(x - 0.07f, 0.12f, x + 0.07f, 0.12f,
               x + 0.05f, 0.33f, x - 0.05f, 0.33f, glass);
        b.line(x, 0.12f, x, 0.33f, frame);
        b.line(x - 0.07f, 0.22f, x + 0.07f, 0.22f, frame);
    }
    b.quad(0.43f, 0.07f, 0.57f, 0.07f, 0.55f, 0.33f, 0.45f, 0.33f,
           Color4F(0.27f, 0.39f, 0.44f, 1.0f));
    b.line(0.50f, 1.02f, 0.50f, 1.16f, frame, 2.0f);
    b.line(0.43f, 1.09f, 0.57f, 1.09f, frame, 2.0f);
    art->drawSolidCircle(b.at(0.50f, 1.09f), bounds.size.width * 0.035f,
                         0.0f, 12, frame);
}

void drawWallworks(DrawNode* art, const Rect& bounds)
{
    const BuildingSketch b{art, bounds};
    const Color4F stone(0.48f, 0.51f, 0.47f, 1.0f);
    const Color4F darkStone(0.30f, 0.35f, 0.34f, 1.0f);
    drawBuildingShell(b, Color4F(0.68f, 0.65f, 0.56f, 1.0f),
                      Color4F(0.40f, 0.43f, 0.31f, 1.0f), 0.38f, 0.87f);

    b.quad(0.55f, 0.76f, 0.82f, 0.76f, 0.79f, 0.84f, 0.60f, 0.84f,
           darkStone);
    b.quad(0.60f, 0.79f, 0.79f, 0.79f, 0.77f, 1.06f, 0.62f, 1.06f, stone);
    b.quad(0.60f, 0.79f, 0.64f, 0.79f, 0.66f, 1.06f, 0.62f, 1.06f,
           darkStone);
    for (float x : {0.60f, 0.67f, 0.74f})
        b.rect(x, 1.04f, x + 0.05f, 1.13f, darkStone);
    b.line(0.70f, 0.83f, 0.70f, 1.02f, darkStone, 2.0f);

    for (float x : {0.07f, 0.82f})
    {
        b.quad(x, 0.10f, x + 0.11f, 0.10f,
               x + 0.10f, 0.37f, x + 0.03f, 0.37f, stone);
        b.rect(x - 0.02f, 0.35f, x + 0.13f, 0.40f, darkStone);
    }
    b.quad(0.33f, 0.09f, 0.67f, 0.09f, 0.63f, 0.33f, 0.37f, 0.33f,
           Color4F(0.31f, 0.28f, 0.23f, 1.0f));
    b.line(0.50f, 0.09f, 0.50f, 0.33f, stone, 2.0f);
    b.line(0.35f, 0.19f, 0.65f, 0.19f, darkStone, 3.0f);
    art->drawSolidCircle(b.at(0.50f, 0.32f), bounds.size.width * 0.06f,
                         0.0f, 14, darkStone);
    b.line(0.46f, 0.32f, 0.54f, 0.32f, Color4F(0.75f, 0.69f, 0.47f, 1.0f));
}

void drawCottage(DrawNode* art, const Rect& bounds)
{
    const BuildingSketch b{art, bounds};
    drawBuildingShell(b, Color4F(0.72f, 0.69f, 0.59f, 1.0f),
                      Color4F(0.42f, 0.46f, 0.34f, 1.0f), 0.41f, 0.96f);
    b.quad(0.65f, 0.82f, 0.82f, 0.82f, 0.79f, 0.89f, 0.68f, 0.89f,
           Color4F(0.29f, 0.34f, 0.27f, 1.0f));
    b.quad(0.69f, 0.85f, 0.78f, 0.85f, 0.77f, 1.05f, 0.70f, 1.05f,
           Color4F(0.48f, 0.43f, 0.37f, 1.0f));
    b.quad(0.22f, 0.12f, 0.36f, 0.12f, 0.34f, 0.36f, 0.24f, 0.36f,
           Color4F(0.31f, 0.52f, 0.57f, 1.0f));
    b.quad(0.61f, 0.08f, 0.77f, 0.08f, 0.75f, 0.35f, 0.63f, 0.35f,
           Color4F(0.36f, 0.30f, 0.25f, 1.0f));
    b.line(0.13f, 0.10f, 0.13f, 0.38f, Color4F(0.48f, 0.37f, 0.26f, 1.0f));
    b.line(0.87f, 0.10f, 0.87f, 0.38f, Color4F(0.48f, 0.37f, 0.26f, 1.0f));
}

void drawTownShop(DrawNode* art, const Rect& bounds)
{
    const BuildingSketch b{art, bounds};
    drawBuildingShell(b, Color4F(0.70f, 0.72f, 0.67f, 1.0f),
                      Color4F(0.42f, 0.36f, 0.32f, 1.0f), 0.45f, 0.95f);
    b.rect(0.17f, 0.25f, 0.83f, 0.29f, Color4F(0.28f, 0.44f, 0.47f, 1.0f));
    b.quad(0.14f, 0.22f, 0.86f, 0.22f, 0.83f, 0.26f, 0.17f, 0.26f,
           Color4F(0.68f, 0.32f, 0.27f, 1.0f));
    b.quad(0.25f, 0.29f, 0.38f, 0.29f, 0.37f, 0.42f, 0.26f, 0.42f,
           Color4F(0.31f, 0.53f, 0.57f, 1.0f));
    b.quad(0.62f, 0.29f, 0.75f, 0.29f, 0.74f, 0.42f, 0.63f, 0.42f,
           Color4F(0.31f, 0.53f, 0.57f, 1.0f));
    b.quad(0.24f, 0.11f, 0.43f, 0.11f, 0.41f, 0.22f, 0.26f, 0.22f,
           Color4F(0.23f, 0.39f, 0.41f, 1.0f));
    b.quad(0.56f, 0.08f, 0.74f, 0.08f, 0.72f, 0.22f, 0.58f, 0.22f,
           Color4F(0.42f, 0.31f, 0.25f, 1.0f));
}
}  // namespace

ModeHomePanel* ModeHomePanel::create(ModeHomeKind kind)
{
    auto* panel = new ModeHomePanel();
    if (panel && panel->initWithKind(kind))
    {
        panel->autorelease();
        return panel;
    }
    AX_SAFE_DELETE(panel);
    return nullptr;
}

float ModeHomePanel::cityPanDistance(const Size& visibleSize)
{
    return castle_world_layout::cityPanDistance(visibleSize);
}

bool ModeHomePanel::initWithKind(ModeHomeKind kind)
{
    if (!Node::init()) return false;
    _kind = kind;

    const bool campaign = _kind == ModeHomeKind::Campaign;
    const Color4F accentColor = campaign ? Color4F(0.18f, 0.56f, 0.72f, 1.0f)
                                         : Color4F(0.72f, 0.30f, 0.42f, 1.0f);

    _backdrop = DrawNode::create();
    _accent   = DrawNode::create();
    addChild(_backdrop, 0);
    addChild(_accent, 1);

    _eyebrow = ui::Text::create(campaign ? "CASTLE CAMPAIGN" : "ENDLESS ROGUELIKE", "Arial", 18);
    _eyebrow->setTextColor(campaign ? Color4B(120, 215, 245, 255) : Color4B(245, 150, 175, 255));
    _title = ui::Text::create(campaign ? "闯关模式" : "肉鸽模式", "Arial", 52);
    _subtitle = ui::Text::create(campaign ? "王城远征" : "无尽守城", "Arial", 24);
    _subtitle->setTextColor(Color4B(190, 202, 220, 255));
    _wallet = ui::Text::create("", "Arial", 24);
    _wallet->setTextColor(Color4B(242, 220, 135, 255));
    _progress = ui::Text::create("", "Arial", 32);
    _combatSnapshot = ui::Text::create("", "Arial", 21);
    _combatSnapshot->setTextColor(Color4B(180, 193, 212, 255));
    addChild(_eyebrow, 2);
    addChild(_title, 2);
    addChild(_subtitle, 2);
    addChild(_wallet, 2);
    addChild(_progress, 2);
    addChild(_combatSnapshot, 2);

    _startButton = ui::Button::create();
    _startButton->ignoreContentAdaptWithSize(false);
    _startButton->setContentSize(Size(PRIMARY_W, PRIMARY_H));
    _startButton->setTitleFontName("Arial");
    _startButton->setTitleFontSize(29.0f);
    _startButtonBg = DrawNode::create();
    drawButton(_startButtonBg, _startButton->getContentSize(), accentColor,
               Color4F(0.88f, 0.94f, 1.0f, 1.0f));
    _startButton->addClickEventListener([this](Object*) {
        if (_startCallback) _startCallback();
    });
    addChild(_startButtonBg, 2);
    addChild(_startButton, 3);

    _workshopButton = ui::Button::create();
    _workshopButton->ignoreContentAdaptWithSize(false);
    _workshopButton->setContentSize(Size(SECONDARY_W, SECONDARY_H));
    _workshopButton->setTitleText("城堡工坊");
    _workshopButton->setTitleFontName("Arial");
    _workshopButton->setTitleFontSize(25.0f);
    _workshopButtonBg = DrawNode::create();
    drawButton(_workshopButtonBg, _workshopButton->getContentSize(),
               Color4F(0.14f, 0.20f, 0.28f, 1.0f), Color4F(0.55f, 0.68f, 0.82f, 1.0f));
    _workshopButton->addClickEventListener([this](Object*) {
        if (_facilityCallback) _facilityCallback(CityFacility::Armory);
    });
    _workshopButton->setVisible(campaign);
    _workshopButtonBg->setVisible(campaign);
    addChild(_workshopButtonBg, 2);
    addChild(_workshopButton, 3);

    if (campaign)
    {
        for (auto* text : {_eyebrow, _title, _subtitle, _wallet, _progress, _combatSnapshot})
            text->setVisible(false);
        _accent->setVisible(false);
        _startButton->setVisible(false);
        _startButtonBg->setVisible(false);
        _workshopButton->setVisible(false);
        _workshopButtonBg->setVisible(false);

        _cityArt = DrawNode::create();
        addChild(_cityArt, 1);
        _stageNumber = ui::Text::create("1", "Arial", 24);
        _stageNumber->setTextColor(Color4B(243, 226, 165, 255));
        addChild(_stageNumber, 3);

        const char* names[] = {"军械所", "研究院", "城防营"};
        const char* details[] = {"弓箭强化", "技能科技", "城墙加固"};
        for (size_t i = 0; i < _facilities.size(); ++i)
        {
            auto& view = _facilities[i];
            view.title = ui::Text::create(names[i], "Arial", 26);
            view.title->setTextColor(Color4B(20, 44, 43, 255));
            view.detail = ui::Text::create(details[i], "Arial", 18);
            view.detail->setTextColor(Color4B(43, 69, 62, 255));
            addChild(view.title, 3);
            addChild(view.detail, 3);
        }
    }

    setVisible(false);
    return true;
}

void ModeHomePanel::layout(const Vec2& origin, const Size& visibleSize)
{
    if (_kind == ModeHomeKind::Campaign)
    {
        layoutCity(origin, visibleSize);
        return;
    }
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const float top = origin.y + visibleSize.height;
    const float bottom = origin.y;

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.035f, 0.045f, 0.065f, 1.0f));
    _accent->clear();
    const Color4F accentColor = _kind == ModeHomeKind::Campaign
                                    ? Color4F(0.18f, 0.56f, 0.72f, 1.0f)
                                    : Color4F(0.72f, 0.30f, 0.42f, 1.0f);
    _accent->drawSolidRect(Vec2(origin.x, top - 8.0f), Vec2(origin.x + visibleSize.width, top), accentColor);
    _accent->drawSolidRect(Vec2(center.x - 150.0f, center.y + 62.0f),
                           Vec2(center.x + 150.0f, center.y + 65.0f), accentColor);

    _eyebrow->setPosition(Vec2(center.x, top - 90.0f));
    _title->setPosition(Vec2(center.x, top - 145.0f));
    _subtitle->setPosition(Vec2(center.x, top - 195.0f));
    _wallet->setPosition(Vec2(center.x, top - 242.0f));
    const float progressY = std::min(center.y + 65.0f, top - 305.0f);
    _progress->setPosition(Vec2(center.x, progressY));
    _combatSnapshot->setPosition(Vec2(center.x, progressY - 75.0f));

    const float primaryY = std::max(bottom + 175.0f,
                                    std::min(center.y - 165.0f, progressY - 190.0f));
    _startButton->setPosition(Vec2(center.x, primaryY));
    _startButtonBg->setPosition(_startButton->getPosition() - Vec2(PRIMARY_W * 0.5f, PRIMARY_H * 0.5f));
    _workshopButton->setPosition(Vec2(center.x, primaryY - 92.0f));
    _workshopButtonBg->setPosition(_workshopButton->getPosition() -
                                   Vec2(SECONDARY_W * 0.5f, SECONDARY_H * 0.5f));
}

void ModeHomePanel::layoutCity(const Vec2& origin, const Size& visibleSize)
{
    const float left = origin.x;
    const float bottom = origin.y;
    const float w = visibleSize.width;
    const float h = visibleSize.height;
    const float cityTop = bottom + h;
    const float right = left + w;
    const float centerX = left + w * 0.5f;

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, Vec2(right, cityTop),
                             Color4F(0.40f, 0.51f, 0.38f, 1.0f));
    _cityArt->clear();
    const float wallBase = bottom + castle_world_layout::cityWallBase(visibleSize);
    const float roadHalfW = w * 0.065f;
    const float sideWallW = w * 0.025f;
    _cityArt->drawSolidRect(Vec2(left, bottom), Vec2(left + sideWallW, wallBase),
                            Color4F(0.43f, 0.46f, 0.46f, 1.0f));
    _cityArt->drawSolidRect(Vec2(right - sideWallW, bottom), Vec2(right, wallBase),
                            Color4F(0.43f, 0.46f, 0.46f, 1.0f));
    _cityArt->drawSolidRect(Vec2(left, bottom),
                            Vec2(centerX - roadHalfW, bottom + h * 0.018f),
                            Color4F(0.43f, 0.46f, 0.46f, 1.0f));
    _cityArt->drawSolidRect(Vec2(centerX + roadHalfW, bottom),
                            Vec2(right, bottom + h * 0.018f),
                            Color4F(0.43f, 0.46f, 0.46f, 1.0f));
    _cityArt->drawSolidRect(Vec2(centerX - roadHalfW, bottom),
                            Vec2(centerX + roadHalfW, wallBase),
                            Color4F(0.60f, 0.59f, 0.49f, 1.0f));
    _cityArt->drawSolidRect(Vec2(left + w * 0.08f, bottom + h * 0.45f),
                            Vec2(right - w * 0.08f, bottom + h * 0.51f),
                            Color4F(0.60f, 0.59f, 0.49f, 1.0f));
    _cityArt->drawSolidCircle(Vec2(centerX, bottom + h * 0.48f), w * 0.12f,
                               0.0f, 28, Color4F(0.68f, 0.65f, 0.55f, 1.0f));
    _cityArt->drawSolidCircle(Vec2(centerX, bottom + h * 0.48f), w * 0.043f,
                               0.0f, 24, Color4F(0.30f, 0.48f, 0.51f, 1.0f));
    _cityArt->drawSolidCircle(Vec2(centerX, bottom + h * 0.48f), w * 0.020f,
                               0.0f, 18, Color4F(0.76f, 0.79f, 0.72f, 1.0f));
    for (float x : {left + w * 0.11f, right - w * 0.11f})
    {
        for (float y : {bottom + h * 0.09f, bottom + h * 0.50f, bottom + h * 0.79f})
        {
            _cityArt->drawSolidCircle(Vec2(x, y), w * 0.035f, 0.0f, 16,
                                       Color4F(0.18f, 0.36f, 0.29f, 1.0f));
            _cityArt->drawSolidCircle(Vec2(x - 4.0f, y + 5.0f), w * 0.025f,
                                       0.0f, 16, Color4F(0.31f, 0.50f, 0.34f, 1.0f));
        }
    }

    // 墙顶平面接到战场；面向城内的门墙用深一层的立面表达高度。
    const float wallFaceTop = cityTop - castle_world_layout::platformDepth(visibleSize);
    _cityArt->drawSolidRect(Vec2(left, wallBase), Vec2(right, wallFaceTop),
                            Color4F(0.41f, 0.44f, 0.48f, 1.0f));
    _cityArt->drawSolidRect(Vec2(left, wallFaceTop), Vec2(right, cityTop),
                            Color4F(0.50f, 0.50f, 0.55f, 1.0f));
    _cityArt->drawSolidRect(Vec2(left, wallBase), Vec2(right, wallBase + h * 0.014f),
                            Color4F(0.32f, 0.37f, 0.39f, 1.0f));
    for (float x = left + w * 0.04f; x < right; x += w * 0.12f)
        _cityArt->drawLine(Vec2(x, wallBase + h * 0.018f),
                           Vec2(x, wallFaceTop - h * 0.006f),
                           Color4F(0.39f, 0.41f, 0.45f, 0.62f), 2.0f);
    _cityArt->drawLine(Vec2(left, wallFaceTop), Vec2(right, wallFaceTop),
                       Color4F(0.25f, 0.30f, 0.33f, 1.0f), 3.0f);
    _cityArt->drawLine(Vec2(left, wallFaceTop + h * 0.032f),
                       Vec2(right, wallFaceTop + h * 0.032f),
                       Color4F(0.65f, 0.65f, 0.67f, 0.60f), 2.0f);
    const float towerW = w * 0.105f;
    for (float towerX : {left + w * 0.15f, right - w * 0.15f - towerW})
    {
        _cityArt->drawSolidRect(Vec2(towerX, wallBase - h * 0.025f),
                                Vec2(towerX + towerW, wallFaceTop),
                                Color4F(0.39f, 0.44f, 0.47f, 1.0f));
        _cityArt->drawSolidRect(Vec2(towerX, wallFaceTop),
                                Vec2(towerX + towerW, cityTop),
                                Color4F(0.57f, 0.59f, 0.60f, 1.0f));
        _cityArt->drawRect(Vec2(towerX, wallBase - h * 0.025f), Vec2(towerX + towerW, cityTop),
                           Color4F(0.24f, 0.31f, 0.35f, 1.0f), 3.0f);
    }
    const float gateHalfW = w * 0.085f;
    const Vec2 gate[] = {
        Vec2(centerX - gateHalfW, wallBase), Vec2(centerX + gateHalfW, wallBase),
        Vec2(centerX + gateHalfW, wallFaceTop - h * 0.018f),
        Vec2(centerX + gateHalfW * 0.70f, wallFaceTop),
        Vec2(centerX - gateHalfW * 0.70f, wallFaceTop),
        Vec2(centerX - gateHalfW, wallFaceTop - h * 0.018f),
    };
    _cityArt->drawSolidPoly(gate, 6, Color4F(0.23f, 0.27f, 0.28f, 1.0f));
    _cityArt->drawSolidRect(Vec2(centerX - gateHalfW * 0.8f, wallBase + h * 0.010f),
                            Vec2(centerX + gateHalfW * 0.8f, wallBase + h * 0.043f),
                            Color4F(0.47f, 0.35f, 0.25f, 1.0f));
    _cityArt->drawLine(Vec2(centerX, wallBase + h * 0.010f),
                       Vec2(centerX, wallBase + h * 0.043f),
                       Color4F(0.80f, 0.69f, 0.46f, 1.0f), 2.0f);
    const Vec2 stageCenter(centerX - w * 0.14f, wallBase + h * 0.030f);
    const float plaqueHalfW = std::min(w * 0.043f, 32.0f);
    const float plaqueHalfH = std::min(h * 0.016f, 20.0f);
    _cityArt->drawSolidRect(stageCenter - Vec2(plaqueHalfW, plaqueHalfH),
                            stageCenter + Vec2(plaqueHalfW, plaqueHalfH),
                            Color4F(0.24f, 0.30f, 0.32f, 1.0f));
    _cityArt->drawRect(stageCenter - Vec2(plaqueHalfW, plaqueHalfH),
                       stageCenter + Vec2(plaqueHalfW, plaqueHalfH),
                       Color4F(0.77f, 0.69f, 0.48f, 1.0f), 2.5f);
    for (float dx : {-plaqueHalfW + 7.0f, plaqueHalfW - 7.0f})
        _cityArt->drawSolidCircle(stageCenter + Vec2(dx, 0.0f), 2.0f, 0.0f, 12,
                                  Color4F(0.77f, 0.69f, 0.48f, 1.0f));
    _stageNumber->setPosition(stageCenter);
    const float parapetY = wallFaceTop -
                            h * castle_world_layout::INNER_PARAPET_RATIO;
    const float merlonW = w * 0.040f;
    for (float x = left; x + merlonW <= right; x += merlonW * 1.7f)
        _cityArt->drawSolidRect(Vec2(x, parapetY),
                                Vec2(x + merlonW, wallFaceTop + h * 0.013f),
                                Color4F(0.66f, 0.68f, 0.69f, 1.0f));
    _cityArt->drawSolidRect(Vec2(centerX - w * 0.16f, wallFaceTop - h * 0.028f),
                            Vec2(centerX + w * 0.16f, wallFaceTop + h * 0.017f),
                            Color4F(0.34f, 0.39f, 0.42f, 1.0f));
    _cityArt->drawSolidRect(Vec2(centerX - w * 0.16f, wallFaceTop + h * 0.003f),
                            Vec2(centerX + w * 0.16f, wallFaceTop + h * 0.017f),
                            Color4F(0.70f, 0.72f, 0.72f, 1.0f));
    for (float x = centerX - w * 0.15f; x < centerX + w * 0.15f; x += merlonW * 1.5f)
        _cityArt->drawSolidRect(Vec2(x, wallFaceTop - h * 0.028f),
                                Vec2(x + merlonW, wallFaceTop - h * 0.007f),
                                Color4F(0.61f, 0.64f, 0.66f, 1.0f));
    const float outerMerlonW = 22.0f;
    const float outerMerlonH = 14.0f;
    const float outerMerlonGap = 14.0f;
    for (float x = left; x + outerMerlonW <= right;
         x += outerMerlonW + outerMerlonGap)
        _cityArt->drawSolidRect(Vec2(x, cityTop),
                                Vec2(x + outerMerlonW, cityTop + outerMerlonH),
                                Color4F(0.62f, 0.62f, 0.67f, 1.0f));
    _wallHitRect = Rect(left, wallBase, w, cityTop - wallBase);

    const float buildingW = std::min(w * 0.22f, 165.0f);
    const float buildingH = std::min(h * 0.125f, 160.0f);
    const Rect bounds[] = {
        Rect(left + w * 0.23f - buildingW * 0.5f,
             bottom + h * 0.67f - buildingH * 0.5f, buildingW, buildingH),
        Rect(left + w * 0.77f - buildingW * 0.5f,
             bottom + h * 0.67f - buildingH * 0.5f, buildingW, buildingH),
        Rect(left + w * 0.23f - buildingW * 0.5f,
             bottom + h * 0.29f - buildingH * 0.5f, buildingW, buildingH),
    };
    using BuildingDrawer = void (*)(DrawNode*, const Rect&);
    const BuildingDrawer buildings[] = {drawArmory, drawAcademy, drawWallworks};
    for (size_t i = 0; i < _facilities.size(); ++i)
    {
        buildings[i](_cityArt, bounds[i]);
        auto& view = _facilities[i];
        const float buildingCenterX = bounds[i].getMidX();
        view.title->setFontSize(std::min(26.0f, buildingW / 4.4f));
        view.detail->setFontSize(std::min(18.0f, buildingW / 4.5f));
        view.title->setPosition(Vec2(buildingCenterX, bounds[i].getMinY() - 20.0f));
        view.detail->setPosition(Vec2(buildingCenterX, bounds[i].getMinY() - 43.0f));
        view.hitRect = Rect(bounds[i].getMinX() - 12.0f, bounds[i].getMinY() - 54.0f,
                            buildingW + 24.0f, buildingH * 1.20f + 68.0f);
    }
    const float futureX = left + w * 0.77f;
    const float futureY = bottom + h * 0.29f;
    _cityArt->drawRect(Vec2(futureX - buildingW * 0.55f, futureY - buildingH * 0.55f),
                       Vec2(futureX + buildingW * 0.55f, futureY + buildingH * 0.55f),
                       Color4F(0.30f, 0.41f, 0.33f, 0.86f), 2.0f);
    _cityArt->drawSolidCircle(Vec2(futureX, futureY), w * 0.028f, 0.0f, 14,
                               Color4F(0.25f, 0.43f, 0.32f, 1.0f));
    const float houseW = std::min(w * 0.11f, 80.0f);
    const float houseH = std::min(h * 0.055f, 70.0f);
    const float houseY = bottom + h * 0.10f - houseH * 0.5f;
    drawCottage(_cityArt, Rect(left + w * 0.23f - houseW * 0.5f,
                               houseY, houseW, houseH));
    drawTownShop(_cityArt, Rect(left + w * 0.77f - houseW * 0.5f,
                                houseY, houseW, houseH));
}

bool ModeHomePanel::handleCityTouch(const Vec2& screenPoint)
{
    if (_kind != ModeHomeKind::Campaign || !isVisible()) return false;
    const Vec2 localPoint = convertToNodeSpace(screenPoint);
    if (_wallHitRect.containsPoint(localPoint))
    {
        if (_startCallback) _startCallback();
        return true;
    }
    for (size_t i = 0; i < _facilities.size(); ++i)
    {
        if (_facilities[i].hitRect.containsPoint(localPoint))
        {
            if (_facilityCallback) _facilityCallback(static_cast<CityFacility>(i));
            return true;
        }
    }
    return false;
}

void ModeHomePanel::open(int campaignStageCount)
{
    refresh(campaignStageCount);
    stopAllActions();
    setPosition(Vec2::ZERO);
    setVisible(true);
}

void ModeHomePanel::setStageNumber(int stage)
{
    if (_stageNumber)
    {
        const std::string number = std::to_string(std::max(1, stage));
        _stageNumber->setString(number);
        _stageNumber->setFontSize(std::min(24.0f, 70.0f / number.size()));
    }
}

void ModeHomePanel::refresh(int campaignStageCount)
{
    const auto& data = GameData::getInstance();
    if (_kind == ModeHomeKind::Campaign)
        setStageNumber(data.nextStage(campaignStageCount));
    const auto& economy = data.economy();
    _wallet->setString(StringUtils::format("金币 %d     水晶 %d", economy.totalCoins(),
                                           economy.totalCrystals()));
    _combatSnapshot->setString(StringUtils::format("箭矢伤害 %d     多重箭 %d     城墙生命 %d",
                                                    data.arrowDamage(), data.multiShotCount(),
                                                    data.wallMaxHealth()));

    if (_kind == ModeHomeKind::Roguelike)
    {
        _progress->setString("无尽远征  等待出发");
        _startButton->setTitleText("开始新局");
    }
}
