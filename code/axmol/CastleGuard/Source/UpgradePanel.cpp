#include "UpgradePanel.h"

#include <algorithm>

using namespace ax;

namespace
{
constexpr float PANEL_PADDING  = 24.0f;
constexpr float ROW_HEIGHT     = 112.0f;
constexpr float ROW_GAP        = 10.0f;
constexpr float SECTION_HEIGHT = 48.0f;
constexpr float BUY_BUTTON_W   = 150.0f;
constexpr float BUY_BUTTON_H   = 58.0f;

std::string valueText(UpgradeId id, float value)
{
    switch (id)
    {
    case UpgradeId::ArrowDamage:
        return StringUtils::format("箭矢伤害  %.0f", value);
    case UpgradeId::FireRate:
        return StringUtils::format("射击间隔  %.3f 秒", value);
    case UpgradeId::MultiShot:
        return StringUtils::format("每次射出  %.0f 支", value);
    case UpgradeId::WallHealth:
        return StringUtils::format("城墙生命  %.0f", value);
    default:
        return "";
    }
}

void drawButtonBackground(DrawNode* background, const Size& size, bool enabled, bool primary = false)
{
    background->clear();
    const Color4F fill = !enabled ? Color4F(0.22f, 0.24f, 0.28f, 1.0f)
                                  : primary ? Color4F(0.18f, 0.58f, 0.36f, 1.0f)
                                            : Color4F(0.20f, 0.42f, 0.68f, 1.0f);
    const Color4F line = enabled ? Color4F(0.76f, 0.90f, 1.0f, 1.0f)
                                 : Color4F(0.45f, 0.48f, 0.54f, 1.0f);
    background->drawSolidRect(Vec2::ZERO, Vec2(size.width, size.height), fill);
    background->drawRect(Vec2::ZERO, Vec2(size.width, size.height), line, 2.0f);
}
}  // namespace

bool UpgradePanel::init()
{
    if (!Node::init())
    {
        return false;
    }

    _backdrop = DrawNode::create();
    addChild(_backdrop, 0);
    _panel = DrawNode::create();
    addChild(_panel, 1);

    _title = ui::Text::create("城堡工坊", "Arial", 42);
    addChild(_title, 2);
    _wallet = ui::Text::create("", "Arial", 25);
    _wallet->setTextColor(Color4B(235, 220, 145, 255));
    addChild(_wallet, 2);
    _feedback = ui::Text::create("", "Arial", 21);
    _feedback->setTextColor(Color4B(255, 220, 120, 255));
    addChild(_feedback, 2);

    _scroll = ui::ScrollView::create();
    _scroll->setDirection(ui::ScrollView::Direction::VERTICAL);
    _scroll->setBounceEnabled(true);
    _scroll->setScrollBarWidth(5.0f);
    _scroll->setScrollBarColor(Color3B(190, 205, 225));
    _scroll->setSwallowTouches(true);
    addChild(_scroll, 2);

    _startButton = ui::Button::create();
    _startButton->ignoreContentAdaptWithSize(false);
    _startButton->setContentSize(Size(250.0f, 66.0f));
    _startButton->setTitleText("开始下一局");
    _startButton->setTitleFontName("Arial");
    _startButton->setTitleFontSize(27.0f);
    _startButtonBg = DrawNode::create();
    drawButtonBackground(_startButtonBg, _startButton->getContentSize(), true, true);
    addChild(_startButtonBg, 2);
    _startButton->addClickEventListener([this](Object*) {
        if (_startRunCallback)
        {
            _startRunCallback();
        }
    });
    addChild(_startButton, 3);

    setVisible(false);
    return true;
}

void UpgradePanel::layout(const Vec2& origin, const Size& visibleSize)
{
    const float panelW = std::min(650.0f, visibleSize.width * 0.92f);
    const float panelH = std::min(1080.0f, visibleSize.height * 0.90f);
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.02f, 0.03f, 0.05f, 0.90f));
    _panel->clear();
    _panel->drawSolidRect(panelMin, panelMax, Color4F(0.09f, 0.11f, 0.15f, 1.0f));
    _panel->drawRect(panelMin, panelMax, Color4F(0.64f, 0.72f, 0.84f, 1.0f), 3.0f);

    _title->setPosition(Vec2(center.x, panelMax.y - 42.0f));
    _wallet->setPosition(Vec2(center.x, panelMax.y - 82.0f));

    const float footerH = 108.0f;
    const float scrollTop = panelMax.y - 112.0f;
    const float scrollBottom = panelMin.y + footerH;
    const float scrollW = panelW - PANEL_PADDING * 2.0f;
    const float scrollH = std::max(180.0f, scrollTop - scrollBottom);
    _scroll->setContentSize(Size(scrollW, scrollH));
    _scroll->setPosition(Vec2(panelMin.x + PANEL_PADDING, scrollBottom));
    _scroll->setScrollBarPositionFromCorner(Vec2(3.0f, 3.0f));
    rebuildRows(scrollW);

    _startButton->setPosition(Vec2(center.x, panelMin.y + 42.0f));
    _startButtonBg->setPosition(_startButton->getPosition() -
                                Vec2(_startButton->getContentSize().width * 0.5f,
                                     _startButton->getContentSize().height * 0.5f));
    _feedback->setPosition(Vec2(center.x, panelMin.y + 90.0f));
    refresh();
}

void UpgradePanel::open()
{
    _feedback->setString("");
    refresh();
    setVisible(true);
    _scroll->jumpToTop();
}

void UpgradePanel::rebuildRows(float contentWidth)
{
    _scroll->removeAllChildren();
    _rows.clear();

    const int upgradeCount = static_cast<int>(UpgradeId::Count);
    const int skillCount   = static_cast<int>(SkillId::Count);
    const float innerH = SECTION_HEIGHT * 2.0f + (upgradeCount + skillCount) * (ROW_HEIGHT + ROW_GAP) + ROW_GAP;
    _scroll->setInnerContainerSize(Size(contentWidth, innerH));
    float y = innerH;

    auto addSection = [&](const std::string& text) {
        auto label = ui::Text::create(text, "Arial", 25);
        label->setAnchorPoint(Vec2(0.0f, 0.5f));
        label->setTextColor(Color4B(150, 205, 255, 255));
        y -= SECTION_HEIGHT * 0.5f;
        label->setPosition(Vec2(12.0f, y));
        _scroll->addChild(label);
        y -= SECTION_HEIGHT * 0.5f;
    };

    auto addRow = [&](bool isSkill, int id) {
        y -= ROW_HEIGHT;
        auto background = DrawNode::create();
        background->drawSolidRect(Vec2(4.0f, 0.0f), Vec2(contentWidth - 10.0f, ROW_HEIGHT),
                                  Color4F(0.14f, 0.17f, 0.22f, 1.0f));
        background->drawRect(Vec2(4.0f, 0.0f), Vec2(contentWidth - 10.0f, ROW_HEIGHT),
                             Color4F(0.30f, 0.35f, 0.43f, 1.0f), 1.5f);
        background->setPosition(Vec2(0.0f, y));
        _scroll->addChild(background, 0);

        RowView row;
        row.isSkill = isSkill;
        row.id      = id;
        row.title   = ui::Text::create("", "Arial", 25);
        row.title->setAnchorPoint(Vec2(0.0f, 0.5f));
        row.title->setPosition(Vec2(22.0f, y + 78.0f));
        _scroll->addChild(row.title, 1);
        row.detail = ui::Text::create("", "Arial", 19);
        row.detail->setAnchorPoint(Vec2(0.0f, 0.5f));
        row.detail->setTextColor(Color4B(190, 200, 215, 255));
        row.detail->setPosition(Vec2(22.0f, y + 37.0f));
        _scroll->addChild(row.detail, 1);

        row.button = ui::Button::create();
        row.button->ignoreContentAdaptWithSize(false);
        row.button->setContentSize(Size(BUY_BUTTON_W, BUY_BUTTON_H));
        row.button->setTitleFontName("Arial");
        row.button->setTitleFontSize(21.0f);
        row.button->setPosition(Vec2(contentWidth - BUY_BUTTON_W * 0.5f - 24.0f, y + ROW_HEIGHT * 0.5f));
        row.buttonBg = DrawNode::create();
        row.buttonBg->setPosition(row.button->getPosition() - Vec2(BUY_BUTTON_W * 0.5f, BUY_BUTTON_H * 0.5f));
        _scroll->addChild(row.buttonBg, 1);
        if (isSkill)
        {
            row.button->addClickEventListener([this, id](Object*) { unlockSkill(static_cast<SkillId>(id)); });
        }
        else
        {
            row.button->addClickEventListener([this, id](Object*) { purchaseUpgrade(static_cast<UpgradeId>(id)); });
        }
        _scroll->addChild(row.button, 2);
        _rows.push_back(row);
        y -= ROW_GAP;
    };

    addSection("弓箭与城墙");
    for (int i = 0; i < upgradeCount; ++i)
    {
        addRow(false, i);
    }
    addSection("技能解锁");
    for (int i = 0; i < skillCount; ++i)
    {
        addRow(true, i);
    }
}

void UpgradePanel::refresh()
{
    const auto& economy = GameData::getInstance().economy();
    _wallet->setString(StringUtils::format("金币 %d     水晶 %d", economy.totalCoins(), economy.totalCrystals()));
    for (auto& row : _rows)
    {
        refreshRow(row);
    }
}

void UpgradePanel::refreshRow(RowView& row)
{
    auto& data = GameData::getInstance();
    if (row.isSkill)
    {
        const auto id          = static_cast<SkillId>(row.id);
        const auto& definition = data.skillDefinition(id);
        const bool unlocked    = data.isSkillUnlocked(id);
        row.title->setString(definition.name);
        row.detail->setString(definition.description);
        const bool stageUnlock = !unlocked && definition.unlockStage > 0;
        row.button->setTitleText(unlocked ? "已解锁"
                                         : stageUnlock ? StringUtils::format("第 %d 关解锁", definition.unlockStage)
                                                       : StringUtils::format("%d 水晶", definition.unlockCost));
        row.button->setTouchEnabled(!unlocked && !stageUnlock && definition.unlockCost > 0);
        row.button->setBright(!unlocked && !stageUnlock);
        drawButtonBackground(row.buttonBg, row.button->getContentSize(),
                             !unlocked && !stageUnlock && definition.unlockCost > 0);
        return;
    }

    const auto id          = static_cast<UpgradeId>(row.id);
    const auto& definition = data.upgradeDefinition(id);
    const int level        = data.upgradeLevel(id);
    const bool atMax       = level >= definition.maxLevel();
    const float current    = definition.values[std::min(level, definition.maxLevel())];
    row.title->setString(StringUtils::format("%s   Lv.%d/%d", definition.name.c_str(), level,
                                             definition.maxLevel()));
    if (atMax)
    {
        row.detail->setString(valueText(id, current) + "   已达上限");
        row.button->setTitleText("已满级");
    }
    else
    {
        const float next = definition.values[level + 1];
        row.detail->setString(valueText(id, current) + "  ->  " + valueText(id, next));
        const char* unit = definition.currency == Currency::Coins ? "金币" : "水晶";
        row.button->setTitleText(StringUtils::format("%d %s", definition.costs[level], unit));
    }
    row.button->setTouchEnabled(!atMax);
    row.button->setBright(!atMax);
    drawButtonBackground(row.buttonBg, row.button->getContentSize(), !atMax);
}

void UpgradePanel::purchaseUpgrade(UpgradeId id)
{
    showResult(GameData::getInstance().purchaseUpgrade(id));
    refresh();
}

void UpgradePanel::unlockSkill(SkillId id)
{
    showResult(GameData::getInstance().unlockSkill(id));
    refresh();
}

void UpgradePanel::showResult(const PurchaseResult& result)
{
    switch (result.status)
    {
    case PurchaseStatus::Purchased:
        _feedback->setString("购买成功，下一局立即生效");
        _feedback->setTextColor(Color4B(125, 240, 160, 255));
        break;
    case PurchaseStatus::InsufficientFunds:
        _feedback->setString(result.currency == Currency::Coins ? "金币不足" : "水晶不足");
        _feedback->setTextColor(Color4B(255, 145, 120, 255));
        break;
    case PurchaseStatus::MaxLevel:
        _feedback->setString("这一项已经满级");
        break;
    case PurchaseStatus::AlreadyUnlocked:
        _feedback->setString("这个技能已经解锁");
        break;
    default:
        _feedback->setString("配置异常，购买未执行");
        _feedback->setTextColor(Color4B(255, 145, 120, 255));
        break;
    }
}
