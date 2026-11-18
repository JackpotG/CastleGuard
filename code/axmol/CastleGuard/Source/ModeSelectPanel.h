#pragma once

#include "axmol.h"
#include "ui/CocosGUI.h"

#include <array>
#include <functional>
#include <utility>

class ModeSelectPanel : public ax::Node
{
public:
    CREATE_FUNC(ModeSelectPanel);

    bool init() override;
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void setChoiceCallback(std::function<void(bool)> callback) { _choiceCallback = std::move(callback); }

private:
    struct ModeView
    {
        ax::DrawNode* background = nullptr;
        ax::ui::Text* title      = nullptr;
        ax::ui::Text* description = nullptr;
        ax::ui::Button* button   = nullptr;
        ax::DrawNode* buttonBg   = nullptr;
    };

    ax::DrawNode* _backdrop = nullptr;
    ax::DrawNode* _panel    = nullptr;
    ax::ui::Text* _title    = nullptr;
    ax::ui::Text* _subtitle = nullptr;
    std::array<ModeView, 2> _views;
    std::function<void(bool)> _choiceCallback;
};
