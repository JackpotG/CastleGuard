#pragma once

#include "GameData.h"
#include "TreeConnectionRouter.h"
#include "axmol.h"
#include "ui/CocosGUI.h"

#include <functional>
#include <utility>
#include <vector>

// 局外成长界面：只负责把 GameData 的配置/状态映射成可滚动 UI。
// 购买、扣款、存档都仍由 GameData 完成。
class UpgradePanel : public ax::Node
{
public:
    CREATE_FUNC(UpgradePanel);

    enum class Section
    {
        All,
        Weapons,
        Research,
        Defense,
    };

    bool init() override;
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void open();
    void openAt(Section section);
    void setStartRunCallback(std::function<void()> callback) { _startRunCallback = std::move(callback); }
    void setStartButtonText(const std::string& text) { _startButton->setTitleText(text); }
    void setCloseCallback(std::function<void()> callback) { _closeCallback = std::move(callback); }

private:
    enum class NodeState
    {
        Completed,
        Available,
        Locked,
    };

    struct TreeNodeView
    {
        int id          = 0;
        float radius    = 31.0f;
        bool skill      = false;
        ax::DrawNode* graphic  = nullptr;
        ax::ui::Text* title    = nullptr;
        ax::ui::Text* status   = nullptr;
        ax::ui::Text* detail   = nullptr;
        ax::ui::Button* target = nullptr;
    };

    void rebuildTree(float contentWidth);
    TreeNodeView createTreeNode(int id, bool skill, const ax::Vec2& position, bool root, float extra);
    void refresh();
    void refreshUpgradeNode(TreeNodeView& node);
    void refreshSkillNode(TreeNodeView& node);
    void drawNode(TreeNodeView& node, NodeState state, bool affordable = true);
    void purchaseUpgrade(UpgradeId id);
    void unlockSkill(SkillId id);
    void showResult(const PurchaseResult& result);

    ax::DrawNode* _backdrop       = nullptr;
    ax::DrawNode* _panel          = nullptr;
    ax::ui::Text* _title          = nullptr;
    ax::ui::Text* _wallet         = nullptr;
    ax::ui::Text* _feedback       = nullptr;
    ax::ui::ScrollView* _scroll   = nullptr;
    ax::ui::Button* _closeButton  = nullptr;
    ax::ui::Button* _startButton  = nullptr;
    ax::DrawNode* _startButtonBg  = nullptr;
    struct ConnectionCache
    {
        std::vector<float> geometry;
        std::vector<TreeConnections::Route> routes;
    };
    struct TreeTrackView
    {
        ax::DrawNode* connector = nullptr;
        ax::ui::Text* heading = nullptr;
        float top = 0, bottom = 0;
        std::vector<TreeNodeView> nodes;
        ConnectionCache connections;
    };
    std::vector<TreeTrackView> _tracks;
    std::function<void()> _startRunCallback;
    std::function<void()> _closeCallback;
    Section _section = Section::All;
    ax::Vec2 _lastOrigin;
    ax::Size _lastVisibleSize;
};
