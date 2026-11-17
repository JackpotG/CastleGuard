#pragma once

#include "GameData.h"
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

    bool init() override;
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void open();
    void setStartRunCallback(std::function<void()> callback) { _startRunCallback = std::move(callback); }

private:
    struct RowView
    {
        bool isSkill = false;
        int id       = 0;
        ax::ui::Text* title      = nullptr;
        ax::ui::Text* detail     = nullptr;
        ax::ui::Button* button   = nullptr;
        ax::DrawNode* buttonBg   = nullptr;
    };

    void rebuildRows(float contentWidth);
    void refresh();
    void refreshRow(RowView& row);
    void purchaseUpgrade(UpgradeId id);
    void unlockSkill(SkillId id);
    void showResult(const PurchaseResult& result);

    ax::DrawNode* _backdrop       = nullptr;
    ax::DrawNode* _panel          = nullptr;
    ax::ui::Text* _title          = nullptr;
    ax::ui::Text* _wallet         = nullptr;
    ax::ui::Text* _feedback       = nullptr;
    ax::ui::ScrollView* _scroll   = nullptr;
    ax::ui::Button* _startButton  = nullptr;
    ax::DrawNode* _startButtonBg  = nullptr;
    std::vector<RowView> _rows;
    std::function<void()> _startRunCallback;
};
