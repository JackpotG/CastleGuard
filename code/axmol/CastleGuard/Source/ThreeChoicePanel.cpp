#include "ThreeChoicePanel.h"

#include <algorithm>

using namespace ax;

namespace
{
constexpr float PANEL_PADDING = 24.0f;
constexpr float CHOICE_GAP    = 14.0f;
constexpr float BUTTON_W      = 132.0f;
constexpr float BUTTON_H      = 54.0f;

void drawButton(DrawNode* background, const Size& size)
{
    background->clear();
    background->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height),
                              Color4F(0.18f, 0.56f, 0.36f, 1.0f));
    background->drawRect(Vec2::ZERO, Vec2(size.width, size.height),
                         Color4F(0.72f, 1.0f, 0.82f, 1.0f), 2.0f);
}

void drawChoiceIcon(DrawNode* art, ChoiceIcon icon)
{
    art->clear();
    const Color4F gold(0.96f, 0.72f, 0.22f, 1.0f);
    const Color4F crystal(0.39f, 0.82f, 0.94f, 1.0f);
    if (icon == ChoiceIcon::Coins || icon == ChoiceIcon::Supplies)
    {
        const float x = icon == ChoiceIcon::Supplies ? -14.0f : 0.0f;
        art->drawSolidCircle(Vec2(x, 0.0f), 22.0f, 0.0f, 24, gold);
        art->drawCircle(Vec2(x, 0.0f), 16.0f, 0.0f, 24, false,
                        Color4F(1.0f, 0.90f, 0.46f, 1.0f), 3.0f);
    }
    if (icon == ChoiceIcon::Crystals || icon == ChoiceIcon::Supplies)
    {
        const float x = icon == ChoiceIcon::Supplies ? 17.0f : 0.0f;
        const Vec2 gem[] = {Vec2(x, 25.0f), Vec2(x + 20.0f, 4.0f),
                            Vec2(x + 12.0f, -20.0f), Vec2(x - 12.0f, -20.0f),
                            Vec2(x - 20.0f, 4.0f)};
        art->drawSolidPoly(gem, 5, crystal);
        art->drawLine(gem[0], gem[2], Color4F(0.80f, 0.97f, 1.0f, 1.0f), 2.0f);
    }
    if (icon == ChoiceIcon::WallKit)
    {
        const Vec2 shield[] = {Vec2(-23.0f, 20.0f), Vec2(23.0f, 20.0f),
                               Vec2(19.0f, -12.0f), Vec2(0.0f, -27.0f),
                               Vec2(-19.0f, -12.0f)};
        art->drawSolidPoly(shield, 5, Color4F(0.62f, 0.69f, 0.67f, 1.0f));
        art->drawLine(Vec2(-11.0f, 3.0f), Vec2(11.0f, 3.0f),
                      Color4F(0.24f, 0.38f, 0.36f, 1.0f), 4.0f);
        art->drawLine(Vec2(0.0f, 14.0f), Vec2(0.0f, -9.0f),
                      Color4F(0.24f, 0.38f, 0.36f, 1.0f), 4.0f);
    }
    if (icon == ChoiceIcon::ManaCharm)
    {
        art->drawCircle(Vec2::ZERO, 25.0f, 0.0f, 28, false, crystal, 3.0f);
        const Vec2 rune[] = {Vec2(0.0f, 20.0f), Vec2(15.0f, 0.0f),
                             Vec2(0.0f, -20.0f), Vec2(-15.0f, 0.0f)};
        art->drawSolidPoly(rune, 4, crystal);
        art->drawLine(rune[0], rune[2], Color4F(0.84f, 1.0f, 1.0f, 1.0f), 2.0f);
    }
}
}  // namespace

bool ThreeChoicePanel::init()
{
    if (!Node::init())
    {
        return false;
    }

    _backdrop = DrawNode::create();
    addChild(_backdrop, 0);
    _panel = DrawNode::create();
    addChild(_panel, 1);
    _title = ui::Text::create("", "Arial", 42);
    addChild(_title, 2);
    _subtitle = ui::Text::create("", "Arial", 21);
    _subtitle->setTextColor(Color4B(185, 198, 218, 255));
    addChild(_subtitle, 2);

    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        view.background = DrawNode::create();
        addChild(view.background, 2);
        view.icon = DrawNode::create();
        addChild(view.icon, 3);
        view.title = ui::Text::create("", "Arial", 29);
        view.title->setAnchorPoint(Vec2(0.0f, 0.5f));
        addChild(view.title, 3);
        view.description = ui::Text::create("", "Arial", 21);
        view.description->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.description->setTextColor(Color4B(205, 214, 228, 255));
        view.description->ignoreContentAdaptWithSize(false);
        view.description->setTextHorizontalAlignment(TextHAlignment::LEFT);
        addChild(view.description, 3);
        view.detail = ui::Text::create("", "Arial", 19);
        view.detail->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.detail->setTextColor(Color4B(250, 220, 125, 255));
        addChild(view.detail, 3);

        view.button = ui::Button::create();
        view.button->ignoreContentAdaptWithSize(false);
        view.button->setContentSize(Size(BUTTON_W, BUTTON_H));
        view.button->setTitleText("选择");
        view.button->setTitleFontName("Arial");
        view.button->setTitleFontSize(24.0f);
        view.buttonBg = DrawNode::create();
        drawButton(view.buttonBg, view.button->getContentSize());
        addChild(view.buttonBg, 3);
        view.button->addClickEventListener([this, i](Object*) {
            if (_choiceCallback)
            {
                _choiceCallback(i);
            }
        });
        addChild(view.button, 4);
    }

    setVisible(false);
    return true;
}

void ThreeChoicePanel::layout(const Vec2& origin, const Size& visibleSize)
{
    _lastOrigin      = origin;
    _lastVisibleSize = visibleSize;
    const float panelW = std::min(_iconLayout ? 590.0f : 660.0f,
                                  visibleSize.width * 0.92f);
    const float panelH = std::min(_iconLayout ? 610.0f : 880.0f,
                                  visibleSize.height * 0.86f);
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.02f, 0.03f, 0.05f, 0.92f));
    _panel->clear();
    _panel->drawSolidRect(panelMin, panelMax, Color4F(0.08f, 0.10f, 0.14f, 1.0f));
    _panel->drawRect(panelMin, panelMax, Color4F(0.72f, 0.78f, 0.88f, 1.0f), 3.0f);

    _title->setPosition(Vec2(center.x, panelMax.y - 48.0f));
    _subtitle->setPosition(Vec2(center.x, panelMax.y - 86.0f));
    _subtitle->setVisible(!_iconLayout);
    const float listTop    = panelMax.y - (_iconLayout ? 94.0f : 116.0f);
    const float listBottom = panelMin.y + 28.0f;
    const float rowH = (listTop - listBottom - CHOICE_GAP * 2.0f) / 3.0f;
    const float rowW = panelW - PANEL_PADDING * 2.0f;
    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        const float rowTop = listTop - i * (rowH + CHOICE_GAP);
        const Vec2 rowMin(panelMin.x + PANEL_PADDING, rowTop - rowH);
        const Vec2 rowMax(rowMin.x + rowW, rowTop);
        const auto accent8 = _cards[i].accent;
        const Color4F accent(accent8.r / 255.0f, accent8.g / 255.0f, accent8.b / 255.0f, 1.0f);
        view.background->clear();
        view.background->drawSolidRect(rowMin, rowMax, Color4F(0.13f, 0.16f, 0.21f, 1.0f));
        view.background->drawSolidRect(rowMin, Vec2(rowMin.x + 7.0f, rowMax.y), accent);
        view.background->drawRect(rowMin, rowMax, Color4F(0.30f, 0.35f, 0.43f, 1.0f), 1.5f);

        if (_iconLayout)
        {
            drawChoiceIcon(view.icon, _cards[i].icon);
            view.icon->setPosition(Vec2(rowMin.x + 62.0f, rowMin.y + rowH * 0.5f));
            view.icon->setVisible(true);
            view.title->setPosition(Vec2(rowMin.x + 112.0f, rowMin.y + rowH * 0.64f));
            view.description->setTextAreaSize(Size(rowW - 132.0f, 42.0f));
            view.description->setPosition(Vec2(rowMin.x + 112.0f, rowMin.y + rowH * 0.36f));
            view.detail->setVisible(false);
            view.button->setContentSize(Size(rowW, rowH));
            view.button->setPosition(Vec2(rowMin.x + rowW * 0.5f, rowMin.y + rowH * 0.5f));
            view.button->setTitleText("");
            view.buttonBg->setVisible(false);
            continue;
        }
        view.icon->setVisible(false);
        view.detail->setVisible(true);
        view.buttonBg->setVisible(true);
        view.button->setContentSize(Size(BUTTON_W, BUTTON_H));
        view.button->setTitleText("选择");

        const float textX = rowMin.x + 24.0f;
        view.title->setPosition(Vec2(textX, rowMax.y - 34.0f));
        view.title->setTextColor(accent8);
        const float textW = std::max(160.0f, rowW - BUTTON_W - 68.0f);
        view.description->setTextAreaSize(Size(textW, 58.0f));
        view.description->setPosition(Vec2(textX, rowMax.y - 80.0f));
        view.detail->setPosition(Vec2(textX, rowMin.y + 24.0f));

        const Vec2 buttonPos(rowMax.x - BUTTON_W * 0.5f - 20.0f, rowMin.y + rowH * 0.5f);
        view.button->setPosition(buttonPos);
        view.buttonBg->setPosition(buttonPos - Vec2(BUTTON_W * 0.5f, BUTTON_H * 0.5f));
    }
}

void ThreeChoicePanel::open(const std::string& title, const std::string& subtitle,
                            const std::array<ThreeChoiceCard, CHOICE_COUNT>& cards,
                            bool iconLayout)
{
    _cards = cards;
    _iconLayout = iconLayout;
    _title->setString(title);
    _subtitle->setString(subtitle);
    for (size_t i = 0; i < _views.size(); ++i)
    {
        _views[i].title->setString(_cards[i].title);
        _views[i].description->setString(_cards[i].description);
        _views[i].detail->setString(_cards[i].detail);
    }
    if (_lastVisibleSize.width > 0.0f && _lastVisibleSize.height > 0.0f)
    {
        layout(_lastOrigin, _lastVisibleSize);
    }
    setVisible(true);
}
