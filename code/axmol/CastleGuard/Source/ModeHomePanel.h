#pragma once

#include "axmol.h"
#include "ui/CocosGUI.h"

#include <array>
#include <functional>
#include <utility>

enum class ModeHomeKind
{
    Campaign,
    Roguelike,
};

enum class CityFacility
{
    Armory,
    Academy,
    Wallworks,
};

// 两个模式各持有一个主页实例；闯关城内属于共享世界，肉鸽主页保持屏幕面板。
class ModeHomePanel : public ax::Node
{
public:
    static ModeHomePanel* create(ModeHomeKind kind);

    bool initWithKind(ModeHomeKind kind);
    static float cityPanDistance(const ax::Size& visibleSize);
    void layout(const ax::Vec2& origin, const ax::Size& visibleSize);
    void open(int campaignStageCount);
    bool handleCityTouch(const ax::Vec2& screenPoint);
    void setStartCallback(std::function<void()> callback) { _startCallback = std::move(callback); }
    void setFacilityCallback(std::function<void(CityFacility)> callback)
    {
        _facilityCallback = std::move(callback);
    }

private:
    void refresh(int campaignStageCount);
    void layoutCity(const ax::Vec2& origin, const ax::Size& visibleSize);

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
    ax::DrawNode* _cityArt = nullptr;
    ax::Rect _wallHitRect;
    struct FacilityView
    {
        ax::Rect hitRect;
        ax::ui::Text* title = nullptr;
        ax::ui::Text* detail = nullptr;
    };
    std::array<FacilityView, 3> _facilities;
    std::function<void()> _startCallback;
    std::function<void(CityFacility)> _facilityCallback;
};
