#pragma once

#include "axmol.h"
#include "ui/CocosGUI.h"

#include <array>
#include <functional>
#include <utility>

#ifndef CG_DEBUG_TOOLS
#define CG_DEBUG_TOOLS 1
#endif

#if CG_DEBUG_TOOLS
enum class DevelopmentMenuEntry
{
    Core,
    Campaign,
    Roguelike,
};

// 开发构建的玩法总入口。它只负责展示和选择，不持有任何玩法状态。
class DevelopmentMenuPanel : public ax::Node
{
public:
    CREATE_FUNC(DevelopmentMenuPanel);

    bool init() override;
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void setChoiceCallback(std::function<void(DevelopmentMenuEntry)> callback)
    {
        _choiceCallback = std::move(callback);
    }

private:
    struct EntryView
    {
        ax::DrawNode* background     = nullptr;
        ax::ui::Text* title          = nullptr;
        ax::ui::Text* status         = nullptr;
        ax::ui::Text* description    = nullptr;
        ax::ui::Button* button       = nullptr;
        ax::DrawNode* buttonBg       = nullptr;
    };

    ax::DrawNode* _backdrop = nullptr;
    ax::DrawNode* _panel    = nullptr;
    ax::ui::Text* _title    = nullptr;
    ax::ui::Text* _subtitle = nullptr;
    std::array<EntryView, 3> _views;
    std::function<void(DevelopmentMenuEntry)> _choiceCallback;
};
#endif
