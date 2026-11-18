#include "DevelopmentMenuPanel.h"

#if CG_DEBUG_TOOLS

#include <algorithm>

using namespace ax;

namespace
{
constexpr float BUTTON_W = 166.0f;
constexpr float BUTTON_H = 56.0f;

void drawButton(DrawNode* node, const Size& size, const Color4F& color)
{
    node->clear();
    node->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height), color);
    node->drawRect(Vec2::ZERO, Vec2(size.width, size.height), Color4F(0.90f, 0.94f, 1.0f, 1.0f), 2.0f);
}
}  // namespace

bool DevelopmentMenuPanel::init()
{
    if (!Node::init()) return false;

    _backdrop = DrawNode::create();
    _panel    = DrawNode::create();
    addChild(_backdrop, 0);
    addChild(_panel, 1);

    _title = ui::Text::create("CastleGuard 开发模式", "Arial", 40);
    _subtitle = ui::Text::create("选择本次要验证的玩法分支", "Arial", 20);
    _subtitle->setTextColor(Color4B(184, 196, 215, 255));
    addChild(_title, 2);
    addChild(_subtitle, 2);

    const char* titles[3] = {"玩法核心", "闯关模式", "肉鸽模式"};
    const char* statuses[3] = {"稳定基线", "V1 可玩", "V1 实验"};
    const char* descriptions[3] = {
        "射箭、技能、怪物与完整波次巡检\n练习局不写入永久收益或关卡进度",
        "永久养成 + 五关战役\n首次通关获得随机养成奖励",
        "当前：无尽波次 + 局内随机增益\n规划：武器掉落、解锁与局外养成",
    };
    const char* buttons[3] = {"进入验证", "开始闯关", "开始肉鸽"};
    const Color4F colors[3] = {
        Color4F(0.18f, 0.52f, 0.44f, 1.0f),
        Color4F(0.20f, 0.42f, 0.68f, 1.0f),
        Color4F(0.55f, 0.30f, 0.48f, 1.0f),
    };
    const Color4B statusColors[3] = {
        Color4B(128, 235, 190, 255),
        Color4B(145, 202, 255, 255),
        Color4B(244, 175, 220, 255),
    };

    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        view.background = DrawNode::create();
        view.title = ui::Text::create(titles[i], "Arial", 29);
        view.title->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.status = ui::Text::create(statuses[i], "Arial", 17);
        view.status->setAnchorPoint(Vec2(1.0f, 0.5f));
        view.status->setTextColor(statusColors[i]);
        view.description = ui::Text::create(descriptions[i], "Arial", 19);
        view.description->setAnchorPoint(Vec2(0.0f, 0.5f));
        view.description->setTextColor(Color4B(205, 214, 228, 255));
        view.description->ignoreContentAdaptWithSize(false);
        view.description->setTextHorizontalAlignment(TextHAlignment::LEFT);
        view.button = ui::Button::create();
        view.button->ignoreContentAdaptWithSize(false);
        view.button->setContentSize(Size(BUTTON_W, BUTTON_H));
        view.button->setTitleText(buttons[i]);
        view.button->setTitleFontName("Arial");
        view.button->setTitleFontSize(22.0f);
        view.buttonBg = DrawNode::create();
        drawButton(view.buttonBg, view.button->getContentSize(), colors[i]);
        view.button->addClickEventListener([this, i](Object*) {
            if (!_choiceCallback) return;
            const DevelopmentMenuEntry entries[3] = {
                DevelopmentMenuEntry::Core,
                DevelopmentMenuEntry::Campaign,
                DevelopmentMenuEntry::Roguelike,
            };
            _choiceCallback(entries[i]);
        });

        addChild(view.background, 2);
        addChild(view.title, 3);
        addChild(view.status, 3);
        addChild(view.description, 3);
        addChild(view.buttonBg, 3);
        addChild(view.button, 4);
    }

    setVisible(false);
    return true;
}

void DevelopmentMenuPanel::layout(const Vec2& origin, const Size& visibleSize)
{
    const float panelW = std::min(690.0f, visibleSize.width * 0.94f);
    const float panelH = std::min(920.0f, visibleSize.height * 0.88f);
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.02f, 0.03f, 0.05f, 0.96f));
    _panel->clear();
    _panel->drawSolidRect(panelMin, panelMax, Color4F(0.075f, 0.09f, 0.12f, 1.0f));
    _panel->drawRect(panelMin, panelMax, Color4F(0.64f, 0.70f, 0.80f, 1.0f), 3.0f);
    _title->setPosition(Vec2(center.x, panelMax.y - 48.0f));
    _subtitle->setPosition(Vec2(center.x, panelMax.y - 86.0f));

    const float rowW = panelW - 40.0f;
    const float rowH = (panelH - 144.0f) / 3.0f - 12.0f;
    for (size_t i = 0; i < _views.size(); ++i)
    {
        auto& view = _views[i];
        const float rowTop = panelMax.y - 116.0f - i * (rowH + 14.0f);
        const Vec2 rowMin(panelMin.x + 20.0f, rowTop - rowH);
        const Vec2 rowMax(rowMin.x + rowW, rowTop);
        view.background->clear();
        view.background->drawSolidRect(rowMin, rowMax, Color4F(0.12f, 0.145f, 0.19f, 1.0f));
        view.background->drawRect(rowMin, rowMax, Color4F(0.29f, 0.34f, 0.42f, 1.0f), 1.5f);

        const float textRight = rowMax.x - BUTTON_W - 48.0f;
        view.title->setPosition(Vec2(rowMin.x + 20.0f, rowMax.y - 34.0f));
        view.status->setPosition(Vec2(textRight, rowMax.y - 34.0f));
        view.description->setTextAreaSize(Size(std::max(190.0f, textRight - rowMin.x - 20.0f), 76.0f));
        view.description->setPosition(Vec2(rowMin.x + 20.0f, rowMin.y + rowH * 0.42f));

        const Vec2 buttonPos(rowMax.x - BUTTON_W * 0.5f - 18.0f, rowMin.y + rowH * 0.5f);
        view.button->setPosition(buttonPos);
        view.buttonBg->setPosition(buttonPos - Vec2(BUTTON_W * 0.5f, BUTTON_H * 0.5f));
    }
}

#endif
