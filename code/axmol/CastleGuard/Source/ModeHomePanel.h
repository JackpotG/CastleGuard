#pragma once

#include "axmol.h"
#include "ui/CocosGUI.h"

#include <functional>
#include <utility>

enum class ModeHomeKind
{
    Campaign,
    Roguelike,
};

// 两个游戏模式各自持有一个主页实例；组件只复用排版，不持有玩法状态。
class ModeHomePanel : public ax::Node
{
public:
    static ModeHomePanel* create(ModeHomeKind kind);

    bool initWithKind(ModeHomeKind kind);
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void open(int campaignStageCount);
    void setStartCallback(std::function<void()> callback) { _startCallback = std::move(callback); }
    void setWorkshopCallback(std::function<void()> callback) { _workshopCallback = std::move(callback); }

private:
    void refresh(int campaignStageCount);

    ModeHomeKind _kind = ModeHomeKind::Campaign;
    ax::DrawNode* _backdrop       = nullptr;
    ax::DrawNode* _accent         = nullptr;
    ax::ui::Text* _eyebrow        = nullptr;
    ax::ui::Text* _title          = nullptr;
    ax::ui::Text* _subtitle       = nullptr;
    ax::ui::Text* _wallet         = nullptr;
    ax::ui::Text* _progress       = nullptr;
    ax::ui::Text* _combatSnapshot = nullptr;
    ax::ui::Button* _startButton  = nullptr;
    ax::DrawNode* _startButtonBg  = nullptr;
    ax::ui::Button* _workshopButton = nullptr;
    ax::DrawNode* _workshopButtonBg = nullptr;
    std::function<void()> _startCallback;
    std::function<void()> _workshopCallback;
};
