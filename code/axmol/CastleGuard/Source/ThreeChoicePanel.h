#pragma once

#include "axmol.h"
#include "ui/CocosGUI.h"

#include <array>
#include <functional>
#include <string>
#include <utility>

struct ThreeChoiceCard
{
    std::string title;
    std::string description;
    std::string detail;
    ax::Color4B accent = ax::Color4B::WHITE;
};

// 通用三选一展示层：只负责卡片和点击回传，不持有任何玩法状态。
class ThreeChoicePanel : public ax::Node
{
public:
    static constexpr size_t CHOICE_COUNT = 3;

    CREATE_FUNC(ThreeChoicePanel);

    bool init() override;
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void open(const std::string& title, const std::string& subtitle,
              const std::array<ThreeChoiceCard, CHOICE_COUNT>& cards);
    void setChoiceCallback(std::function<void(size_t)> callback)
    {
        _choiceCallback = std::move(callback);
    }

private:
    struct ChoiceView
    {
        ax::DrawNode* background  = nullptr;
        ax::ui::Text* title       = nullptr;
        ax::ui::Text* description = nullptr;
        ax::ui::Text* detail      = nullptr;
        ax::ui::Button* button    = nullptr;
        ax::DrawNode* buttonBg    = nullptr;
    };

    ax::DrawNode* _backdrop = nullptr;
    ax::DrawNode* _panel    = nullptr;
    ax::ui::Text* _title    = nullptr;
    ax::ui::Text* _subtitle = nullptr;
    std::array<ChoiceView, CHOICE_COUNT> _views;
    std::array<ThreeChoiceCard, CHOICE_COUNT> _cards;
    std::function<void(size_t)> _choiceCallback;
    ax::Vec2 _lastOrigin       = ax::Vec2::ZERO;
    ax::Size _lastVisibleSize  = ax::Size::ZERO;
};
