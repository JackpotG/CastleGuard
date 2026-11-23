#include "UpgradePanel.h"

#include <algorithm>
#include <array>

using namespace ax;

namespace
{
constexpr float PANEL_PADDING         = 24.0f;
constexpr float SECTION_HEIGHT        = 52.0f;
constexpr float UPGRADE_BRANCH_HEIGHT = 162.0f;
constexpr float NODE_RADIUS           = 31.0f;
constexpr float NODE_TARGET_SIZE      = 78.0f;

std::string valueText(UpgradeId id, float value)
{
    switch (id)
    {
    case UpgradeId::ArrowDamage:
        return StringUtils::format("伤害 %.0f", value);
    case UpgradeId::FireRate:
        return StringUtils::format("间隔 %.3f 秒", value);
    case UpgradeId::MultiShot:
        return StringUtils::format("每次 %.0f 支", value);
    case UpgradeId::WallHealth:
        return StringUtils::format("生命 %.0f", value);
    case UpgradeId::HeavyStrike:
        return StringUtils::format("击退 %.0f", value);
    case UpgradeId::Precision:
        return StringUtils::format("暴击 %.0f%%", value * 100.0f);
    case UpgradeId::ManaFlow:
        return StringUtils::format("回蓝 +%.1f/秒", value);
    case UpgradeId::WallArmor:
        return StringUtils::format("减伤 %.0f", value);
    case UpgradeId::SpellEfficiency:
        return StringUtils::format("耗蓝 %.0f%%", value * 100.0f);
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

ui::Text* addText(Node* parent, const std::string& text, float fontSize, const Vec2& anchor, int z = 2)
{
    auto* label = ui::Text::create(text, "Arial", fontSize);
    label->setAnchorPoint(anchor);
    parent->addChild(label, z);
    return label;
}

void drawUpgradeIcon(DrawNode* graphic, int id, bool skill, float radius, const Color4F& color)
{
    const float s = radius * 0.48f;
    if (skill)
    {
        switch (static_cast<SkillId>(id))
        {
        case SkillId::Fireball:
            graphic->drawSolidCircle(Vec2::ZERO, s * 0.55f, 0, 16, color);
            graphic->drawSolidTriangle(Vec2(-s, -s), Vec2(0, s), Vec2(s, -s), color, color);
            return;
        case SkillId::Freeze:
            graphic->drawLine(Vec2(-s, 0), Vec2(s, 0), color, 3);
            graphic->drawLine(Vec2(0, -s), Vec2(0, s), color, 3);
            graphic->drawLine(Vec2(-s * 0.7f, -s * 0.7f), Vec2(s * 0.7f, s * 0.7f), color, 2);
            graphic->drawLine(Vec2(-s * 0.7f, s * 0.7f), Vec2(s * 0.7f, -s * 0.7f), color, 2);
            return;
        case SkillId::ArrowRain:
            for (int i = -1; i <= 1; ++i)
            {
                const float x = i * s * 0.7f;
                graphic->drawLine(Vec2(x, s), Vec2(x, -s * 0.55f), color, 2);
                graphic->drawSolidTriangle(Vec2(x - 4, -s * 0.45f), Vec2(x + 4, -s * 0.45f),
                                           Vec2(x, -s), color, color);
            }
            return;
        case SkillId::Lightning:
            graphic->drawSolidTriangle(Vec2(0, s), Vec2(-s * 0.65f, -s * 0.1f),
                                       Vec2(s * 0.15f, -s * 0.1f), color, color);
            graphic->drawSolidTriangle(Vec2(-s * 0.1f, s * 0.1f), Vec2(s * 0.65f, s * 0.1f),
                                       Vec2(-s * 0.25f, -s), color, color);
            return;
        default: break;
        }
    }
    switch (static_cast<UpgradeId>(id))
    {
    case UpgradeId::ArrowDamage:
        graphic->drawLine(Vec2(-s * 0.7f, -s), Vec2(s * 0.6f, s * 0.6f), color, 4);
        graphic->drawSolidTriangle(Vec2(s * 0.2f, s), Vec2(s, s * 0.2f),
                                   Vec2(s * 0.65f, s * 0.7f), color, color);
        graphic->drawLine(Vec2(-s, -s * 0.5f), Vec2(-s * 0.3f, -s * 0.8f), color, 3);
        break;
    case UpgradeId::HeavyStrike:
        graphic->drawLine(Vec2(-s * 0.45f, -s), Vec2(s * 0.35f, s * 0.6f), color, 4);
        graphic->drawSolidRect(Vec2(-s * 0.55f, s * 0.35f), Vec2(s, s), color);
        break;
    case UpgradeId::FireRate:
        for (int i = -1; i <= 1; ++i)
            graphic->drawLine(Vec2(-s, i * s * 0.55f), Vec2(s * 0.6f, i * s * 0.55f), color, 2);
        graphic->drawSolidTriangle(Vec2(s * 0.35f, s), Vec2(s, 0), Vec2(s * 0.35f, -s), color, color);
        break;
    case UpgradeId::ManaFlow:
        graphic->drawCircle(Vec2::ZERO, s * 0.8f, 0, 24, false, color, 3);
        graphic->drawSolidCircle(Vec2::ZERO, s * 0.2f, 0, 12, color);
        graphic->drawLine(Vec2(0, s * 0.25f), Vec2(0, s), color, 3);
        break;
    case UpgradeId::SpellEfficiency:
        graphic->drawCircle(Vec2::ZERO, s * 0.85f, 0, 24, false, color, 3);
        graphic->drawSolidTriangle(Vec2(-s * 0.2f, s * 0.7f), Vec2(s * 0.5f, s * 0.1f),
                                   Vec2(-s * 0.1f, -s * 0.1f), color, color);
        graphic->drawLine(Vec2(-s * 0.45f, -s * 0.55f), Vec2(s * 0.45f, -s * 0.55f), color, 3);
        break;
    case UpgradeId::Precision:
        graphic->drawCircle(Vec2::ZERO, s, 0, 24, false, color, 2);
        graphic->drawCircle(Vec2::ZERO, s * 0.55f, 0, 24, false, color, 2);
        graphic->drawSolidCircle(Vec2::ZERO, s * 0.18f, 0, 12, color);
        break;
    case UpgradeId::MultiShot:
        for (int i = -1; i <= 1; ++i)
        {
            graphic->drawLine(Vec2(-s, i * s * 0.55f), Vec2(s, i * s * 0.55f), color, 2);
            graphic->drawSolidTriangle(Vec2(s * 0.4f, i * s * 0.55f + 4),
                                       Vec2(s * 0.4f, i * s * 0.55f - 4),
                                       Vec2(s, i * s * 0.55f), color, color);
        }
        break;
    case UpgradeId::WallHealth:
        graphic->drawSolidRect(Vec2(-s, -s * 0.8f), Vec2(s, s * 0.4f), color);
        for (int i = -1; i <= 1; ++i)
            graphic->drawSolidRect(Vec2(i * s * 0.7f - 3, s * 0.3f),
                                   Vec2(i * s * 0.7f + 3, s), color);
        break;
    case UpgradeId::WallArmor:
        graphic->drawSolidTriangle(Vec2(-s, s), Vec2(s, s), Vec2(0, -s), color, color);
        graphic->drawLine(Vec2(0, s * 0.55f), Vec2(0, -s * 0.55f),
                          Color4F(0.20f, 0.24f, 0.29f, 1.0f), 2);
        break;
    default: break;
    }
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
    _lastOrigin = origin;
    _lastVisibleSize = visibleSize;
    const float panelW = std::min(650.0f, visibleSize.width * 0.92f);
    const float preferredH = _section == Section::Defense ? 560.0f
                             : _section == Section::Weapons ? 690.0f
                             : _section == Section::All ? 1080.0f : 920.0f;
    const float panelH = std::min(preferredH, visibleSize.height * 0.90f);
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

    const float footerH      = _section == Section::Defense ? 82.0f : 108.0f;
    const float scrollTop    = panelMax.y - 112.0f;
    const float scrollBottom = panelMin.y + footerH;
    const float scrollW      = panelW - PANEL_PADDING * 2.0f;
    const float scrollH      = std::max(180.0f, scrollTop - scrollBottom);
    _scroll->setContentSize(Size(scrollW, scrollH));
    _scroll->setPosition(Vec2(panelMin.x + PANEL_PADDING, scrollBottom));
    _scroll->setScrollBarPositionFromCorner(Vec2(3.0f, 3.0f));
    rebuildTree(scrollW);

    _startButton->setPosition(Vec2(center.x, panelMin.y + 42.0f));
    _startButtonBg->setPosition(_startButton->getPosition() -
                                Vec2(_startButton->getContentSize().width * 0.5f,
                                     _startButton->getContentSize().height * 0.5f));
    _feedback->setPosition(Vec2(center.x, panelMin.y + 90.0f));
    refresh();
}

void UpgradePanel::open()
{
    openAt(Section::All);
}

void UpgradePanel::openAt(Section section)
{
    _section = section;
    switch (section)
    {
    case Section::All:
        _title->setString("成长总览");
        break;
    case Section::Weapons:
        _title->setString("军械所");
        break;
    case Section::Research:
        _title->setString("研究院");
        break;
    case Section::Defense:
        _title->setString("城防营");
        break;
    }
    _feedback->setString("");
    if (_lastVisibleSize.width > 0.0f && _lastVisibleSize.height > 0.0f)
        layout(_lastOrigin, _lastVisibleSize);
    refresh();
    setVisible(true);
    _scroll->jumpToTop();
}

void UpgradePanel::rebuildTree(float contentWidth)
{
    _scroll->removeAllChildren();
    _upgradeBranches.clear();
    _researchNodes.clear();
    _skillNodes.clear();
    _skillConnector = nullptr;
    _skillRoot      = nullptr;
    _researchConnector = nullptr;

    const bool showBow = _section == Section::All || _section == Section::Weapons;
    const bool showMagic = _section == Section::All || _section == Section::Research;
    std::vector<UpgradeId> upgradeIds;
    if (_section == Section::All || _section == Section::Defense)
    {
        upgradeIds.push_back(UpgradeId::WallHealth);
        upgradeIds.push_back(UpgradeId::WallArmor);
    }
    int maxSkillRow = 0;
    int maxBowRow = 0;
    int maxMagicRow = 0;
    if (showMagic)
    {
        for (int i = 0; i < static_cast<int>(SkillId::Count); ++i)
            maxSkillRow = std::max(maxSkillRow, GameData::getInstance()
                .skillDefinition(static_cast<SkillId>(i)).treeRow);
    }
    if (showBow || showMagic)
    {
        for (int i = 0; i < static_cast<int>(UpgradeId::Count); ++i)
        {
            const auto& definition = GameData::getInstance().upgradeDefinition(static_cast<UpgradeId>(i));
            if (definition.track == UpgradeTrack::Bow)
                maxBowRow = std::max(maxBowRow, definition.treeRow);
            else if (definition.track == UpgradeTrack::Magic)
                maxMagicRow = std::max(maxMagicRow, definition.treeRow);
        }
    }
    const float bowHeight = showBow ? SECTION_HEIGHT + (maxBowRow + 1) * 160.0f + 55.0f : 0.0f;
    const float magicHeight = showMagic ?
        SECTION_HEIGHT + (maxMagicRow + 1) * 160.0f + 55.0f +
        SECTION_HEIGHT + 145.0f + maxSkillRow * 138.0f + 100.0f +
        SECTION_HEIGHT + 100.0f : 0.0f;
    const float naturalH = (!upgradeIds.empty() ? SECTION_HEIGHT : 0.0f) +
        upgradeIds.size() * UPGRADE_BRANCH_HEIGHT + bowHeight + magicHeight + 20.0f;
    const float innerH = std::max(_scroll->getContentSize().height, naturalH);
    _scroll->setInnerContainerSize(Size(contentWidth, innerH));
    float y = innerH;

    auto addSection = [&](const std::string& text) {
        y -= SECTION_HEIGHT * 0.5f;
        auto* label = addText(_scroll, text, 25.0f, Vec2(0.0f, 0.5f));
        label->setTextColor(Color4B(150, 205, 255, 255));
        label->setPosition(Vec2(12.0f, y));
        y -= SECTION_HEIGHT * 0.5f;
    };

    if (!upgradeIds.empty())
        addSection("城墙工事");
    for (const auto id : upgradeIds)
    {
        const auto& definition = GameData::getInstance().upgradeDefinition(id);
        const float branchTop = y;

        UpgradeBranchView branch;
        branch.id        = id;
        branch.connector = DrawNode::create();
        _scroll->addChild(branch.connector, 0);
        branch.title = addText(_scroll, definition.name, 24.0f, Vec2(0.0f, 0.5f));
        branch.title->setPosition(Vec2(12.0f, branchTop - 25.0f));
        branch.detail = addText(_scroll, "", 17.0f, Vec2(0.0f, 0.5f));
        branch.detail->setTextColor(Color4B(185, 198, 215, 255));
        branch.detail->setPosition(Vec2(12.0f, branchTop - 56.0f));

        const int levelCount = std::max(1, definition.maxLevel());
        const float startX = levelCount == 1 ? contentWidth * 0.5f : 36.0f;
        const float endX   = levelCount == 1 ? contentWidth * 0.5f : contentWidth - 36.0f;
        const float nodeY  = branchTop - 111.0f;
        for (int level = 1; level <= levelCount; ++level)
        {
            const float t = levelCount == 1 ? 0.0f : static_cast<float>(level - 1) / (levelCount - 1);
            const Vec2 position(startX + (endX - startX) * t, nodeY);

            TreeNodeView node;
            node.id          = static_cast<int>(id);
            node.targetLevel = level;
            node.radius      = std::clamp((contentWidth - 58.0f) / (levelCount * 2.0f), 16.0f, NODE_RADIUS);
            node.graphic     = DrawNode::create();
            node.graphic->setPosition(position);
            _scroll->addChild(node.graphic, 1);
            node.title = addText(_scroll, "", 18.0f, Vec2(0.5f, 0.5f), 3);
            node.title->setPosition(position);
            node.status = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
            node.status->setPosition(position + Vec2(0.0f, -37.0f));

            node.target = ui::Button::create();
            node.target->ignoreContentAdaptWithSize(false);
            node.target->setContentSize(Size(node.radius * 2.0f + 10.0f, node.radius * 2.0f + 10.0f));
            node.target->setPosition(position);
            node.target->addClickEventListener([this, id, level](Object*) {
                selectUpgradeNode(id, level);
            });
            _scroll->addChild(node.target, 4);
            branch.nodes.push_back(node);
        }
        _upgradeBranches.push_back(std::move(branch));
        y -= UPGRADE_BRANCH_HEIGHT;
    }

    if (!showBow && !showMagic) return;
    _researchConnector = DrawNode::create();
    _scroll->addChild(_researchConnector, 0);
    auto addUpgradeTrack = [&](UpgradeTrack track, float top) {
        for (int i = 0; i < static_cast<int>(UpgradeId::Count); ++i)
        {
            const UpgradeId id = static_cast<UpgradeId>(i);
            const auto& definition = GameData::getInstance().upgradeDefinition(id);
            if (definition.track != track) continue;
            const Vec2 position(contentWidth * definition.treeX, top - 95.0f - definition.treeRow * 160.0f);
            TreeNodeView node;
            node.id = i;
            node.graphic = DrawNode::create();
            node.graphic->setPosition(position);
            _scroll->addChild(node.graphic, 1);
            node.title = addText(_scroll, "", 17.0f, Vec2(0.5f, 0.5f), 3);
            node.title->setPosition(position + Vec2(0, -43));
            node.status = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
            node.status->setPosition(position + Vec2(0, -64));
            node.detail = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
            node.detail->setPosition(position + Vec2(0, -84));
            node.target = ui::Button::create();
            node.target->ignoreContentAdaptWithSize(false);
            node.target->setContentSize(Size(NODE_TARGET_SIZE, NODE_TARGET_SIZE));
            node.target->setPosition(position);
            node.target->addClickEventListener([this, id](Object*) { purchaseUpgrade(id); });
            _scroll->addChild(node.target, 4);
            _researchNodes.push_back(node);
        }
    };
    if (showBow)
    {
        addSection("弓箭基础");
        const float bowTop = y;
        addUpgradeTrack(UpgradeTrack::Bow, bowTop);
        auto* tacticsLabel = addText(_scroll, "箭术专精", 23.0f, Vec2(0, 0.5f));
        tacticsLabel->setTextColor(Color4B(150, 205, 255, 255));
        tacticsLabel->setPosition(Vec2(12, bowTop - 207.0f));
        y -= (maxBowRow + 1) * 160.0f + 55.0f;
    }
    if (!showMagic) return;
    addSection("法力研修");
    const float magicTop = y;
    addUpgradeTrack(UpgradeTrack::Magic, magicTop);
    y -= (maxMagicRow + 1) * 160.0f + 55.0f;

    addSection("法术研习");
    const float skillTop = y;
    const Vec2 rootPosition(contentWidth * 0.5f, skillTop - 48.0f);
    _skillConnector = DrawNode::create();
    _scroll->addChild(_skillConnector, 0);
    _skillRoot = DrawNode::create();
    _skillRoot->setPosition(rootPosition);
    _skillRoot->drawSolidCircle(Vec2::ZERO, 24.0f, 0.0f, 28, Color4F(0.23f, 0.30f, 0.40f, 1.0f));
    _skillRoot->drawCircle(Vec2::ZERO, 24.0f, 0.0f, 28, false,
                           Color4F(0.64f, 0.78f, 0.94f, 1.0f), 2.0f);
    _scroll->addChild(_skillRoot, 1);
    auto* rootLabel = addText(_scroll, "法术", 16.0f, Vec2(0.5f, 0.5f), 2);
    rootLabel->setPosition(rootPosition);

    const int skillCount = static_cast<int>(SkillId::Count);
    for (int i = 0; i < skillCount; ++i)
    {
        const auto& definition = GameData::getInstance().skillDefinition(static_cast<SkillId>(i));
        const Vec2 position(contentWidth * definition.treeX,
                            skillTop - 145.0f - definition.treeRow * 138.0f);

        TreeNodeView node;
        node.id      = i;
        node.skill   = true;
        node.graphic = DrawNode::create();
        node.graphic->setPosition(position);
        _scroll->addChild(node.graphic, 1);
        node.title = addText(_scroll, "", 17.0f, Vec2(0.5f, 0.5f), 3);
        node.title->setPosition(position + Vec2(0, -43));
        node.status = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
        node.status->setPosition(position + Vec2(0.0f, -65.0f));
        node.target = ui::Button::create();
        node.target->ignoreContentAdaptWithSize(false);
        node.target->setContentSize(Size(NODE_TARGET_SIZE, NODE_TARGET_SIZE));
        node.target->setPosition(position);
        node.target->addClickEventListener([this, i](Object*) { unlockSkill(static_cast<SkillId>(i)); });
        _scroll->addChild(node.target, 4);
        _skillNodes.push_back(node);
    }
    y -= 145.0f + maxSkillRow * 138.0f + 100.0f;

    addSection("魔法设施");
    auto* tower = DrawNode::create();
    tower->setPosition(Vec2(56.0f, y - 49.0f));
    tower->drawSolidRect(Vec2(-19, -24), Vec2(19, 14), Color4F(0.20f, 0.25f, 0.32f, 1.0f));
    tower->drawSolidTriangle(Vec2(-24, 14), Vec2(0, 34), Vec2(24, 14),
                             Color4F(0.34f, 0.39f, 0.48f, 1.0f), Color4F(0.34f, 0.39f, 0.48f, 1.0f));
    _scroll->addChild(tower, 1);
    auto* towerLabel = addText(_scroll, "魔法塔", 19.0f, Vec2(0, 0.5f));
    towerLabel->setPosition(Vec2(95.0f, y - 42.0f));
    auto* towerState = addText(_scroll, "待建造", 14.0f, Vec2(0, 0.5f));
    towerState->setTextColor(Color4B(145, 153, 166, 255));
    towerState->setPosition(Vec2(95.0f, y - 66.0f));
}

void UpgradePanel::refresh()
{
    const auto& economy = GameData::getInstance().economy();
    _wallet->setString(StringUtils::format("金币 %d     水晶 %d", economy.totalCoins(), economy.totalCrystals()));
    for (auto& branch : _upgradeBranches)
    {
        refreshUpgradeBranch(branch);
    }
    if (_researchConnector) _researchConnector->clear();
    for (auto& node : _researchNodes)
    {
        refreshResearchUpgradeNode(node);
        const auto& definition = GameData::getInstance().upgradeDefinition(static_cast<UpgradeId>(node.id));
        if (!_researchConnector || definition.prerequisite == UpgradeId::Count) continue;
        const auto parent = std::find_if(_researchNodes.begin(), _researchNodes.end(),
            [&](const TreeNodeView& candidate) {
                return candidate.id == static_cast<int>(definition.prerequisite);
            });
        if (parent == _researchNodes.end()) continue;
        const bool ready = GameData::getInstance().upgradePrerequisiteMet(static_cast<UpgradeId>(node.id));
        _researchConnector->drawLine(parent->graphic->getPosition(), node.graphic->getPosition(),
                                     ready ? Color4F(0.24f, 0.69f, 0.92f, 1.0f)
                                           : Color4F(0.30f, 0.38f, 0.49f, 1.0f), 3);
    }
    if (_skillConnector)
    {
        _skillConnector->clear();
    }
    for (auto& node : _skillNodes)
    {
        refreshSkillNode(node);
        const auto id          = static_cast<SkillId>(node.id);
        const auto& definition = GameData::getInstance().skillDefinition(id);
        const bool unlocked    = GameData::getInstance().isSkillUnlocked(id);
        const bool parentReady = definition.prerequisite == SkillId::Count ||
            GameData::getInstance().isSkillUnlocked(definition.prerequisite);
        const bool available   = !unlocked && parentReady &&
            definition.unlockStage == 0 && definition.unlockCost > 0;
        const Color4F color    = unlocked ? Color4F(0.82f, 0.70f, 0.28f, 1.0f)
                               : available ? Color4F(0.24f, 0.69f, 0.92f, 1.0f)
                                           : Color4F(0.30f, 0.38f, 0.49f, 1.0f);
        if (_skillConnector && _skillRoot)
        {
            Vec2 parentPosition = _skillRoot->getPosition();
            if (definition.prerequisite != SkillId::Count)
            {
                const auto parent = std::find_if(_skillNodes.begin(), _skillNodes.end(),
                    [&](const TreeNodeView& candidate) {
                        return candidate.id == static_cast<int>(definition.prerequisite);
                    });
                if (parent != _skillNodes.end()) parentPosition = parent->graphic->getPosition();
            }
            _skillConnector->drawLine(parentPosition, node.graphic->getPosition(), color, 3.0f);
        }
    }
}

void UpgradePanel::refreshUpgradeBranch(UpgradeBranchView& branch)
{
    const auto& data       = GameData::getInstance();
    const auto& definition = data.upgradeDefinition(branch.id);
    const int level        = std::min(data.upgradeLevel(branch.id), definition.maxLevel());
    const float current    = definition.values[level];
    if (level >= definition.maxLevel())
    {
        branch.detail->setString(valueText(branch.id, current) + "  ·  已满级");
    }
    else
    {
        const float next = definition.values[level + 1];
        branch.detail->setString(valueText(branch.id, current) + "  ->  " + valueText(branch.id, next));
    }
    branch.title->setString(StringUtils::format("%s  %d/%d", definition.name.c_str(), level,
                                                 definition.maxLevel()));

    branch.connector->clear();
    for (size_t i = 1; i < branch.nodes.size(); ++i)
    {
        const int targetLevel = branch.nodes[i].targetLevel;
        const Color4F color = targetLevel <= level
                                  ? Color4F(0.82f, 0.70f, 0.28f, 1.0f)
                              : targetLevel == level + 1
                                  ? Color4F(0.24f, 0.69f, 0.92f, 1.0f)
                                  : Color4F(0.28f, 0.32f, 0.39f, 1.0f);
        branch.connector->drawLine(branch.nodes[i - 1].graphic->getPosition(),
                                   branch.nodes[i].graphic->getPosition(), color);
    }

    const auto& economy = data.economy();
    for (auto& node : branch.nodes)
    {
        if (node.targetLevel <= level)
        {
            node.status->setString("");
            drawNode(node, NodeState::Completed);
            continue;
        }
        if (node.targetLevel == level + 1)
        {
            const int cost = definition.costs[level];
            const bool coins = definition.currency == Currency::Coins;
            const bool affordable = coins ? economy.totalCoins() >= cost : economy.totalCrystals() >= cost;
            node.status->setString(StringUtils::format("%d%s", cost, coins ? "金" : "晶"));
            drawNode(node, NodeState::Available, affordable);
            continue;
        }
        node.status->setString("");
        drawNode(node, NodeState::Locked);
    }
}

void UpgradePanel::refreshResearchUpgradeNode(TreeNodeView& node)
{
    const auto& data = GameData::getInstance();
    const auto id = static_cast<UpgradeId>(node.id);
    const auto& definition = data.upgradeDefinition(id);
    const int level = std::min(data.upgradeLevel(id), definition.maxLevel());
    node.title->setString(definition.name);
    node.status->setString(valueText(id, definition.values[
        definition.prerequisite == UpgradeId::Count ? level : definition.maxLevel()]));
    if (level >= definition.maxLevel())
    {
        node.detail->setString("已掌握");
        drawNode(node, NodeState::Completed);
        return;
    }
    if (!data.upgradePrerequisiteMet(id))
    {
        node.detail->setString(StringUtils::format("需%s %d", data.upgradeDefinition(
            definition.prerequisite).name.c_str(), definition.requiredLevel));
        drawNode(node, NodeState::Locked);
        return;
    }
    const int cost = definition.costs[level];
    const bool coins = definition.currency == Currency::Coins;
    const bool affordable = coins ? data.economy().totalCoins() >= cost
                                  : data.economy().totalCrystals() >= cost;
    node.detail->setString(StringUtils::format("%d%s · %d/%d", cost, coins ? "金" : "晶",
                                               level, definition.maxLevel()));
    drawNode(node, NodeState::Available, affordable);
}

void UpgradePanel::refreshSkillNode(TreeNodeView& node)
{
    const auto& data       = GameData::getInstance();
    const auto id          = static_cast<SkillId>(node.id);
    const auto& definition = data.skillDefinition(id);
    const bool unlocked    = data.isSkillUnlocked(id);
    const bool parentReady = definition.prerequisite == SkillId::Count ||
        data.isSkillUnlocked(definition.prerequisite);
    const bool purchasable = !unlocked && parentReady &&
        definition.unlockStage == 0 && definition.unlockCost > 0;
    const bool affordable  = data.economy().totalCrystals() >= definition.unlockCost;

    node.title->setString(definition.name);
    if (unlocked)
    {
        node.status->setString("已解锁");
        drawNode(node, NodeState::Completed);
    }
    else if (purchasable)
    {
        node.status->setString(StringUtils::format("%d 水晶", definition.unlockCost));
        drawNode(node, NodeState::Available, affordable);
    }
    else if (!parentReady && definition.unlockStage == 0)
    {
        node.status->setString("需前置");
        drawNode(node, NodeState::Locked);
    }
    else
    {
        node.status->setString(definition.unlockStage > 0
            ? StringUtils::format("第 %d 关", definition.unlockStage) : "未开放");
        drawNode(node, NodeState::Locked);
    }
}

void UpgradePanel::drawNode(TreeNodeView& node, NodeState state, bool affordable)
{
    Color4F fill;
    Color4F line;
    Color4B text;
    switch (state)
    {
    case NodeState::Completed:
        fill = Color4F(0.30f, 0.53f, 0.33f, 1.0f);
        line = Color4F(0.94f, 0.79f, 0.30f, 1.0f);
        text = Color4B(255, 246, 205, 255);
        break;
    case NodeState::Available:
        fill = affordable ? Color4F(0.16f, 0.48f, 0.68f, 1.0f)
                          : Color4F(0.36f, 0.28f, 0.22f, 1.0f);
        line = affordable ? Color4F(0.45f, 0.88f, 1.0f, 1.0f)
                          : Color4F(0.74f, 0.48f, 0.35f, 1.0f);
        text = affordable ? Color4B(225, 248, 255, 255) : Color4B(255, 178, 150, 255);
        break;
    case NodeState::Locked:
    default:
        fill = Color4F(0.18f, 0.20f, 0.24f, 1.0f);
        line = Color4F(0.36f, 0.39f, 0.46f, 1.0f);
        text = Color4B(132, 140, 153, 255);
        break;
    }

    node.graphic->clear();
    node.graphic->drawSolidCircle(Vec2::ZERO, node.radius, 0.0f, 32, fill);
    node.graphic->drawCircle(Vec2::ZERO, node.radius, 0.0f, 32, false, line,
                             state == NodeState::Available ? 4.0f : 2.5f);
    drawUpgradeIcon(node.graphic, node.id, node.skill, node.radius,
                    Color4F(text.r / 255.0f, text.g / 255.0f, text.b / 255.0f, 1.0f));
    node.title->setTextColor(text);
    node.status->setTextColor(text);
    if (node.detail) node.detail->setTextColor(text);
    node.target->setTouchEnabled(state == NodeState::Available);
    node.target->setBright(state == NodeState::Available);
}

void UpgradePanel::selectUpgradeNode(UpgradeId id, int targetLevel)
{
    const int currentLevel = GameData::getInstance().upgradeLevel(id);
    if (targetLevel != currentLevel + 1)
    {
        return;
    }
    purchaseUpgrade(id);
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
        _feedback->setString("节点已解锁，下一局立即生效");
        _feedback->setTextColor(Color4B(125, 240, 160, 255));
        break;
    case PurchaseStatus::InsufficientFunds:
        _feedback->setString(result.currency == Currency::Coins ? "金币不足" : "水晶不足");
        _feedback->setTextColor(Color4B(255, 145, 120, 255));
        break;
    case PurchaseStatus::MaxLevel:
        _feedback->setString("这一条成长分支已经满级");
        break;
    case PurchaseStatus::AlreadyUnlocked:
        _feedback->setString("这个技能已经解锁");
        break;
    case PurchaseStatus::PrerequisiteLocked:
        _feedback->setString("请先解锁前置技能");
        break;
    default:
        _feedback->setString("配置异常，购买未执行");
        _feedback->setTextColor(Color4B(255, 145, 120, 255));
        break;
    }
}
