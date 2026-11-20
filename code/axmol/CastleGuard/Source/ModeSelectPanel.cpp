#include "ModeSelectPanel.h"

#include <algorithm>

using namespace ax;

namespace
{
constexpr float BUTTON_W = 190.0f;
constexpr float BUTTON_H = 58.0f;

void drawButton(DrawNode* node, const Size& size, const Color4F& color)
{
    node->clear();
    node->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height), color);
    node->drawRect(Vec2::ZERO, Vec2(size.width, size.height), Color4F(0.78f, 0.92f, 1.0f, 1.0f), 2.0f);
}
}  // namespace

bool ModeSelectPanel::init()
{
    if (!Node::init()) return false;
    _backdrop = DrawNode::create();
    _panel    = DrawNode::create();
    addChild(_backdrop, 0);
    addChild(_panel, 1);
    _title = ui::Text::create("选择守城方式", "Arial", 44);
    _subtitle = ui::Text::create("两种模式的奖励不共享局内状态", "Arial", 21);
    _subtitle->setTextColor(Color4B(185, 198, 218, 255));
    addChild(_title, 2);
    addChild(_subtitle, 2);

    const char* titles[2] = {"推关养成", "无尽守城"};
    const char* descriptions[2] = {
        "持续闯关与首通战利品",
        "无尽波次与本局临时增益",
    };
    const Color4F colors[2] = {Color4F(0.20f, 0.42f, 0.68f, 1.0f),
                               Color4F(0.48f, 0.30f, 0.66f, 1.0f)};
    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        view.background = DrawNode::create();
        view.title = ui::Text::create(titles[i], "Arial", 31);
        view.title->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.description = ui::Text::create(descriptions[i], "Arial", 21);
        view.description->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.description->setTextColor(Color4B(205, 214, 228, 255));
        view.description->ignoreContentAdaptWithSize(false);
        view.description->setTextHorizontalAlignment(TextHAlignment::LEFT);
        view.button = ui::Button::create();
        view.button->ignoreContentAdaptWithSize(false);
        view.button->setContentSize(Size(BUTTON_W, BUTTON_H));
        view.button->setTitleText(i == 0 ? "开始推关" : "开始无尽");
        view.button->setTitleFontName("Arial");
        view.button->setTitleFontSize(24.0f);
        view.buttonBg = DrawNode::create();
        drawButton(view.buttonBg, view.button->getContentSize(), colors[i]);
        view.button->addClickEventListener([this, i](Object*) {
            if (_choiceCallback) _choiceCallback(i == 1);
        });
        addChild(view.background, 2);
        addChild(view.title, 3);
        addChild(view.description, 3);
        addChild(view.buttonBg, 3);
        addChild(view.button, 4);
    }
    setVisible(false);
    return true;
}

void ModeSelectPanel::layout(const Vec2& origin, const Size& visibleSize)
{
    const float panelW = std::min(680.0f, visibleSize.width * 0.92f);
    const float panelH = std::min(760.0f, visibleSize.height * 0.82f);
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);
    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.02f, 0.03f, 0.05f, 0.94f));
    _panel->clear();
    _panel->drawSolidRect(panelMin, panelMax, Color4F(0.08f, 0.10f, 0.14f, 1.0f));
    _panel->drawRect(panelMin, panelMax, Color4F(0.68f, 0.74f, 0.84f, 1.0f), 3.0f);
    _title->setPosition(Vec2(center.x, panelMax.y - 52.0f));
    _subtitle->setPosition(Vec2(center.x, panelMax.y - 92.0f));

    const float rowW = panelW - 48.0f;
    const float rowH = (panelH - 150.0f) * 0.5f - 12.0f;
    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        const float rowTop = panelMax.y - 122.0f - i * (rowH + 16.0f);
        const Vec2 rowMin(panelMin.x + 24.0f, rowTop - rowH);
        const Vec2 rowMax(rowMin.x + rowW, rowTop);
        view.background->clear();
        view.background->drawSolidRect(rowMin, rowMax, Color4F(0.13f, 0.16f, 0.21f, 1.0f));
        view.background->drawRect(rowMin, rowMax, Color4F(0.30f, 0.35f, 0.43f, 1.0f), 1.5f);
        view.title->setPosition(Vec2(rowMin.x + 24.0f, rowMax.y - 38.0f));
        view.description->setTextAreaSize(Size(std::max(190.0f, rowW - BUTTON_W - 72.0f), 82.0f));
        view.description->setPosition(Vec2(rowMin.x + 24.0f, rowMin.y + rowH * 0.43f));
        const Vec2 buttonPos(rowMax.x - BUTTON_W * 0.5f - 22.0f, rowMin.y + rowH * 0.5f);
        view.button->setPosition(buttonPos);
        view.buttonBg->setPosition(buttonPos - Vec2(BUTTON_W * 0.5f, BUTTON_H * 0.5f));
    }
}
