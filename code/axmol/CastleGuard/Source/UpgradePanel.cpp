#include "UpgradePanel.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace ax;

namespace
{
constexpr float PANEL_PADDING         = 24.0f;
constexpr float SECTION_HEIGHT        = 52.0f;
constexpr float NODE_RADIUS           = 31.0f;
constexpr float NODE_TARGET_SIZE      = 78.0f;
constexpr float CLOSE_BUTTON_SIZE     = 64.0f;

std::string valueText(const UpgradeDefinition& definition, int level)
{
    std::string result;
    for (const auto& effect : definition.effects)
    {
        if (!result.empty()) result += "\n";
        const auto& attributes = GameData::getInstance().upgradeAttributes();
        const auto found = std::find_if(attributes.begin(), attributes.end(), [&](const auto& a) { return a.key == effect.attribute; });
        if (found == attributes.end()) continue;
        const float value = effect.values[std::clamp(level, 0, definition.maxLevel())];
        static const std::array<const char*, 9> labels = {"伤害", "间隔倍率", "箭数", "生命", "击退", "暴击概率", "回蓝/秒", "护甲", "耗蓝倍率"};
        const auto index = static_cast<size_t>(std::distance(attributes.begin(), found));
        result += std::string(labels[index]) + StringUtils::format(effect.operation == "multiply" ? " ×%.3g" :
            effect.operation == "add" ? " %+.3g" : " %.3g", value);
    }
    return result;
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
    const auto& data = GameData::getInstance();
    const auto& effects = data.upgradeDefinition(static_cast<UpgradeId>(id)).effects;
    const auto& attributes = data.upgradeAttributes();
    const auto attribute = std::find_if(attributes.begin(), attributes.end(), [&](const auto& a) {
        return !effects.empty() && a.key == effects.front().attribute;
    });
    const auto icon = static_cast<UpgradeId>(std::distance(attributes.begin(), attribute));
    switch (icon)
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

    _closeButton = ui::Button::create();
    _closeButton->ignoreContentAdaptWithSize(false);
    _closeButton->setContentSize(Size(CLOSE_BUTTON_SIZE, CLOSE_BUTTON_SIZE));
    _closeButton->setAnchorPoint(Vec2(0.5f, 0.5f));
    auto* closeIcon = DrawNode::create();
    const Vec2 closeCenter(CLOSE_BUTTON_SIZE * 0.5f, CLOSE_BUTTON_SIZE * 0.5f);
    closeIcon->drawSolidCircle(closeCenter, 23.0f, 0.0f, 24, Color4F(0.18f, 0.22f, 0.28f, 1.0f));
    const Color4F closeColor(0.88f, 0.93f, 1.0f, 1.0f);
    closeIcon->drawLine(closeCenter + Vec2(-10.0f, -10.0f),
                        closeCenter + Vec2(10.0f, 10.0f), closeColor, 3.0f);
    closeIcon->drawLine(closeCenter + Vec2(-10.0f, 10.0f),
                        closeCenter + Vec2(10.0f, -10.0f), closeColor, 3.0f);
    _closeButton->addChild(closeIcon);
    _closeButton->addClickEventListener([this](Object*) {
        setVisible(false);
        if (_closeCallback) _closeCallback();
    });
    addChild(_closeButton, 3);

    setVisible(false);
    return true;
}

void UpgradePanel::layout(const Vec2& origin, const Size& visibleSize)
{
    _lastOrigin = origin;
    _lastVisibleSize = visibleSize;
    const float panelW = std::min(650.0f, visibleSize.width * 0.92f);
    const float preferredH = _section == Section::Defense ? 760.0f
                             : _section == Section::Weapons ? 690.0f
                             : _section == Section::All ? 1080.0f : 920.0f;
    const float panelH = std::min(preferredH, visibleSize.height * 0.90f);
    const Vec2 center(origin.x + visibleSize.width * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);
    const bool facility = _section != Section::All;

    _backdrop->clear();
    _backdrop->drawSolidRect(origin, origin + Vec2(visibleSize.width, visibleSize.height),
                             Color4F(0.02f, 0.03f, 0.05f, facility ? 0.60f : 0.90f));
    _panel->clear();
    _panel->drawSolidRect(panelMin, panelMax, Color4F(0.09f, 0.11f, 0.15f, 1.0f));
    _panel->drawRect(panelMin, panelMax, Color4F(0.64f, 0.72f, 0.84f, 1.0f), 3.0f);

    _title->setPosition(Vec2(center.x, panelMax.y - 42.0f));
    _wallet->setPosition(Vec2(center.x, panelMax.y - 82.0f));

    const float footerH      = facility ? 64.0f : 108.0f;
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
    _startButton->setVisible(!facility);
    _startButtonBg->setVisible(!facility);
    _closeButton->setVisible(facility);
    _closeButton->setPosition(panelMax - Vec2(CLOSE_BUTTON_SIZE * 0.5f + 6.0f,
                                             CLOSE_BUTTON_SIZE * 0.5f + 6.0f));
    _feedback->setPosition(Vec2(center.x, panelMin.y + (facility ? 32.0f : 90.0f)));
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

UpgradePanel::TreeNodeView UpgradePanel::createTreeNode(int id, bool skill, const Vec2& position, bool root, float extra)
{
    TreeNodeView node;
    node.id = id;
    node.skill = skill;
    node.graphic = DrawNode::create();
    node.graphic->setPosition(position);
    _scroll->addChild(node.graphic, 1);
    node.title = addText(_scroll, "", 17.0f, Vec2(0.5f, 0.5f), 3);
    node.title->setPosition(position + Vec2(0, root ? 90.0f + extra : -43.0f));
    node.status = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
    node.status->setPosition(position + Vec2(0, root ? 68.0f + extra * .5f : -64.0f - extra * .5f));
    if (!skill)
    {
        node.detail = addText(_scroll, "", 13.0f, Vec2(0.5f, 0.5f), 3);
        node.detail->setPosition(position + Vec2(0, root ? 46.0f : -84.0f - extra));
    }
    node.target = ui::Button::create();
    node.target->ignoreContentAdaptWithSize(false);
    node.target->setContentSize(Size(NODE_TARGET_SIZE, NODE_TARGET_SIZE));
    node.target->setPosition(position);
    node.target->addClickEventListener([this, id, skill](Object*) {
        if (skill) unlockSkill(static_cast<SkillId>(id));
        else purchaseUpgrade(static_cast<UpgradeId>(id));
    });
    _scroll->addChild(node.target, 4);
    return node;
}

void UpgradePanel::rebuildTree(float contentWidth)
{
    _scroll->removeAllChildren();
    _tracks.clear();
    const auto& data = GameData::getInstance();
    struct Track
    {
        const char* title;
        UpgradeTrack track;
        bool skill;
        bool visible;
        std::vector<int> ids;
        int maxRow = 0;
        float extra = 0;
        float height = 0;
    };
    std::array<Track, 4> tracks{{
        {"城防研修", UpgradeTrack::Defense, false, _section == Section::All || _section == Section::Defense, {}},
        {"弓箭研修", UpgradeTrack::Bow, false, _section == Section::All || _section == Section::Weapons, {}},
        {"法力研修", UpgradeTrack::Magic, false, _section == Section::All || _section == Section::Research, {}},
        {"法术研习", UpgradeTrack::Magic, true, _section == Section::All || _section == Section::Research, {}},
    }};
    float naturalH = 20.0f;
    for (auto& track : tracks)
    {
        if (!track.visible) continue;
        if (track.skill)
        {
            for (int i = 0; i < static_cast<int>(SkillId::Count); ++i)
            {
                track.ids.push_back(i);
                track.maxRow = std::max(track.maxRow, data.skillDefinition(static_cast<SkillId>(i)).treeRow);
            }
        }
        else
        {
            for (const auto& node : data.upgradeDefinitions())
            {
                if (node.track != track.track) continue;
                track.ids.push_back(static_cast<int>(node.id));
                track.maxRow = std::max(track.maxRow, node.treeRow);
                const auto lines = std::max(node.effects.size(), upgradeRequirements(node).size() + (node.unlockStage > 0 ? 1 : 0));
                track.extra = std::max(track.extra, (static_cast<float>(lines) - 1) * 22.0f);
            }
        }
        track.height = SECTION_HEIGHT + (track.maxRow + 1) * (160.0f + track.extra) + 55.0f + track.extra;
        naturalH += track.height;
    }
    const float innerH = std::max(_scroll->getContentSize().height, naturalH);
    _scroll->setInnerContainerSize(Size(contentWidth, innerH));
    float y = innerH;
    for (const auto& track : tracks)
    {
        if (!track.visible) continue;
        TreeTrackView view;
        view.top = y;
        view.bottom = y - track.height;
        view.connector = DrawNode::create();
        _scroll->addChild(view.connector, 0);
        view.heading = addText(_scroll, track.title, 25.0f, Vec2(0.0f, 0.5f));
        view.heading->setTextColor(Color4B(150, 205, 255, 255));
        view.heading->setPosition(Vec2(12.0f, y - SECTION_HEIGHT * 0.5f));
        y -= SECTION_HEIGHT;
        for (const int id : track.ids)
        {
            const int row = track.skill ? data.skillDefinition(static_cast<SkillId>(id)).treeRow
                                        : data.upgradeDefinition(static_cast<UpgradeId>(id)).treeRow;
            const float x = track.skill ? data.skillDefinition(static_cast<SkillId>(id)).treeX
                                        : data.upgradeDefinition(static_cast<UpgradeId>(id)).treeX;
            const Vec2 position(contentWidth * x, y - 95.0f - track.extra - row * (160.0f + track.extra));
            view.nodes.push_back(createTreeNode(id, track.skill, position, row == 0, track.extra));
        }
        if (track.ids.empty())
        {
            auto* empty = addText(_scroll, "暂无研修项目", 20.0f, Vec2(.5f, .5f));
            empty->setPosition(Vec2(contentWidth * .5f, y - 80.0f));
        }
        y = view.bottom;
        _tracks.push_back(std::move(view));
    }
}

void UpgradePanel::refresh()
{
    const auto& economy = GameData::getInstance().economy();
    _wallet->setString(StringUtils::format("金币 %d     水晶 %d", economy.totalCoins(), economy.totalCrystals()));
    for (auto& track : _tracks)
        for (auto& node : track.nodes)
            if (node.skill) refreshSkillNode(node);
            else refreshUpgradeNode(node);

    const auto& data = GameData::getInstance();
    const Color4F active(.24f,.69f,.92f,1), locked(.30f,.38f,.49f,1);
    const auto drawConnections = [&](DrawNode* layer, const std::vector<TreeNodeView*>& nodes,
        const std::vector<TreeConnections::Edge>& edges, const std::vector<Color4F>& colors,
        const std::vector<ui::Text*>& extraLabels, ConnectionCache& cache, float top, float bottom) {
        if (!layer) return;
        layer->clear();
        std::vector<TreeConnections::Rect> bodies, labels;
        std::vector<float> signature;
        const auto appendLabel = [&](ui::Text* text, const TreeConnections::Rect* body) {
            if (!text || text->getString().empty()) return;
            const auto r = text->getBoundingBox();
            const TreeConnections::Rect box{r.getMinX(),-r.getMaxY(),r.getMaxX(),-r.getMinY()};
            if (body && box.left >= body->left && box.right <= body->right && box.top >= body->top && box.bottom <= body->bottom) return;
            labels.push_back(box);
        };
        for (const auto* node : nodes)
        {
            const auto p = node->graphic->getPosition();
            bodies.push_back({p.x-node->radius,-p.y-node->radius,p.x+node->radius,-p.y+node->radius});
            for (auto* text : {node->title,node->status,node->detail}) appendLabel(text,&bodies.back());
        }
        for (auto* label : extraLabels) appendLabel(label,nullptr);
        const auto size = _scroll->getInnerContainerSize();
        for (const auto& boxes : {bodies,labels})
        {
            signature.push_back(static_cast<float>(boxes.size()));
            for (const auto r : boxes) signature.insert(signature.end(),{r.left,r.top,r.right,r.bottom});
        }
        signature.insert(signature.end(),{size.width,size.height});
        for (const auto edge : edges) signature.insert(signature.end(),{static_cast<float>(edge.from),static_cast<float>(edge.to)});
        if (signature != cache.geometry)
        {
            cache.geometry = std::move(signature);
            TreeConnections::Rect bounds{4,-top,size.width-4,-bottom};
            cache.routes = TreeConnections::route(bodies,edges,labels,bounds);
            if (std::any_of(cache.routes.begin(),cache.routes.end(),[](const auto& path){return path.points.empty();}))
                AXLOGW("[Progression][Research] tree connector blocked by dense node labels");
        }
        for (size_t i=0;i<cache.routes.size();++i)
            for (const auto& stroke : cache.routes[i].strokes)
                for (size_t j=1;j<stroke.size();++j)
                    layer->drawLine(Vec2(stroke[j-1].x,-stroke[j-1].y),Vec2(stroke[j].x,-stroke[j].y),colors[i],2.0f);
    };

    for (auto& track : _tracks)
    {
        std::vector<TreeNodeView*> views;
        std::vector<TreeConnections::Edge> edges;
        std::vector<Color4F> colors;
        for (auto& node : track.nodes) views.push_back(&node);
        for (size_t i = 0; i < views.size(); ++i)
        {
            const auto append = [&](int parentId, bool met) {
                const auto parent = std::find_if(views.begin(), views.end(), [&](const auto* node) { return node->id == parentId; });
                if (parent == views.end()) return;
                edges.push_back({static_cast<int>(parent - views.begin()), static_cast<int>(i)});
                colors.push_back(met ? active : locked);
            };
            if (views[i]->skill)
            {
                const auto id = static_cast<SkillId>(views[i]->id);
                for (const auto parent : skillRequirements(data.skillDefinition(id)))
                    append(static_cast<int>(parent), data.isSkillUnlocked(parent));
            }
            else
            {
                const auto id = static_cast<UpgradeId>(views[i]->id);
                for (const auto& requirement : upgradeRequirements(data.upgradeDefinition(id)))
                    append(static_cast<int>(requirement.id), data.upgradeLevel(requirement.id) >= requirement.level);
            }
        }
        drawConnections(track.connector, views, edges, colors, {track.heading}, track.connections, track.top, track.bottom);
    }
}

void UpgradePanel::refreshUpgradeNode(TreeNodeView& node)
{
    const auto& data = GameData::getInstance();
    const auto id = static_cast<UpgradeId>(node.id);
    const auto& definition = data.upgradeDefinition(id);
    const int level = std::min(data.upgradeLevel(id), definition.maxLevel());
    node.title->setString(definition.name);
    node.status->setString(valueText(definition, level));
    node.status->setTextAreaSize(Size(135, std::max(18.0f, static_cast<float>(definition.effects.size()) * 18.0f)));
    node.status->setTextHorizontalAlignment(TextHAlignment::CENTER);
    if (level >= definition.maxLevel())
    {
        node.detail->setString("已掌握");
        drawNode(node, NodeState::Completed);
        return;
    }
    if (!data.upgradePrerequisiteMet(id))
    {
        std::string missing;
        if (data.highestClearedStage() + 1 < definition.unlockStage)
            missing = StringUtils::format("第 %d 关解锁", definition.unlockStage);
        for (const auto& requirement : upgradeRequirements(definition))
        {
            if (data.upgradeLevel(requirement.id) >= requirement.level) continue;
            if (!missing.empty()) missing += "\n";
            missing += StringUtils::format("需%s %d级", data.upgradeDefinition(requirement.id).name.c_str(), requirement.level);
        }
        node.detail->setString(missing);
        node.detail->setTextHorizontalAlignment(TextHAlignment::CENTER);
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
    const bool parentReady = data.skillPrerequisiteMet(id);
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
