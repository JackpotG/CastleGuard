#include "ModeHomePanel.h"

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
        if (_workshopCallback) _workshopCallback();
    });
    _workshopButton->setVisible(campaign);
    _workshopButtonBg->setVisible(campaign);
    addChild(_workshopButtonBg, 2);
    addChild(_workshopButton, 3);

    setVisible(false);
    return true;
}

void ModeHomePanel::layout(const Vec2& origin, const Size& visibleSize)
{
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

void ModeHomePanel::open(int campaignStageCount)
{
    refresh(campaignStageCount);
    setVisible(true);
}

void ModeHomePanel::refresh(int campaignStageCount)
{
    const auto& data = GameData::getInstance();
    const auto& economy = data.economy();
    _wallet->setString(StringUtils::format("金币 %d     水晶 %d", economy.totalCoins(),
                                           economy.totalCrystals()));
    _combatSnapshot->setString(StringUtils::format("箭矢伤害 %d     多重箭 %d     城墙生命 %d",
                                                    data.arrowDamage(), data.multiShotCount(),
                                                    data.wallMaxHealth()));

    if (_kind == ModeHomeKind::Campaign)
    {
        const int stageCount = std::max(1, campaignStageCount);
        const int cleared = std::min(data.highestClearedStage(), stageCount);
        const int next = data.nextStage(stageCount);
        _progress->setString(StringUtils::format("主线进度  %d / %d", cleared, stageCount));
        _startButton->setTitleText(cleared >= stageCount
                                       ? StringUtils::format("重玩第 %d 关", stageCount)
                                       : StringUtils::format("进入第 %d 关", next));
    }
    else
    {
        _progress->setString("无尽远征  等待出发");
        _startButton->setTitleText("开始新局");
    }
}
