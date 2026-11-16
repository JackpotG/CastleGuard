/****************************************************************************
 Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.
 Copyright (c) 2019-present Axmol Engine contributors (see AUTHORS.md).

 https://axmol.dev/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/

#pragma once

#include "axmol.h"

class MainScene : public ax::Scene
{
    enum class GameState
    {
        init = 0,
        update,
        pause,
        end,
        menu1,
        menu2,
    };

public:
    bool init() override;
    void update(float delta) override;

    // touch
    void onTouchesBegan(const std::vector<ax::Touch*>& touches, ax::Event* event);
    void onTouchesMoved(const std::vector<ax::Touch*>& touches, ax::Event* event);
    void onTouchesEnded(const std::vector<ax::Touch*>& touches, ax::Event* event);

    // Keyboard
    void onKeyPressed(ax::EventKeyboard::KeyCode code, ax::Event* event);
    void onKeyReleased(ax::EventKeyboard::KeyCode code, ax::Event* event);

    MainScene();
    ~MainScene() override;

private:
    // 战场布局（视角风格）：两个并行版本（2026-11-16 与 Stark 确认）
    //   Oblique25D 伪 2.5D 斜俯视 = 原作构图（主线）：城墙底、出怪线顶，车道纵排+纵深色带
    //   SideView   纯侧视角 = 新增风格版本：底部地面条、左侧灰房子，怪物从右边沿地面
    //              右→左进攻房子（单条地面轨迹）
    // 横竖屏适配不做成两份手工场景：布局全部按当前可见尺寸计算，尺寸变化（旋转/
    // 窗口缩放）时走同一条重建路径重排（微信端将来由 wx.onWindowResize 触发）
    enum class BattleLayout
    {
        SideView,
        Oblique25D,
    };
    // 按布局重建战场（销毁旧节点重画）；桌面版按 1/2 键切换对比
    void buildBattlefield(BattleLayout layout);

    // ---- 射箭系统 ----
    void initArrowPool();                       // 初始化对象池（init 时一次性建好）
    void fireArrow(const ax::Vec2& target);     // 从箭手位向目标点射出一支箭
    void updateArrows(float delta);             // 每帧驱动箭矢飞行与回收

    // 箭矢 = 池中的一个槽位。对象池核心思想：游戏循环里绝不 new/delete，
    // 频繁创建销毁会造成内存碎片和卡顿；开局一次建好，用完归还重复利用
    struct Arrow
    {
        ax::DrawNode* node = nullptr;  // 箭矢图形（池化复用）
        ax::Vec2 velocity;             // 当前速度向量（像素/秒）
        bool active = false;           // 该槽位是否正在飞行中
    };
    std::vector<Arrow> _arrowPool;
    ax::Vec2 _archerPos = ax::Vec2::ZERO;      // 箭手发射点（随布局重建更新）

    // ---- 调试层 ----
    // 常驻调试按钮（模拟器里没有键盘也无法双指，用它切换视角）
    void initDebugUI();
    ax::Node* _debugBtn = nullptr;
    ax::Rect _debugBtnRect;  // 按钮热区（世界坐标，随布局重建更新）

    // ---- 怪物系统 ----
    // 简单状态机：走（沿车道向城墙推进）→ 攻击（到墙啃咬扣血）→ 死（回池）
    enum class MonsterState
    {
        Walk,
        Attack,
        Dead,
    };
    // 怪物 = 池中的一个槽位。Prefab 思想的裸实现：定义一个"模板"（外观+行为），
    // 需要时从池里取一个实例按模板初始化，而不是每次 new 一个新对象
    struct Monster
    {
        ax::DrawNode* node = nullptr;   // 占位外观（池化复用）
        int lane           = 0;         // 车道编号（统一数据模型：车道 + 推进）
        ax::Vec2 targetPos;             // 推进终点（出生点+同一份抖动，保证走直线不纠偏）
        float speed        = 70.0f;     // 个体速度（怪群感：85%~115% 随机）
        MonsterState state = MonsterState::Walk;
        float attackTimer  = 0.0f;      // 攻击节奏计时（每秒啃一口）
        bool active        = false;
    };
    void initMonsterPool();
    void spawnMonster();                  // 从出怪线放一个怪（随机车道）
    void updateMonsters(float delta);     // 每帧驱动移动/攻击/死亡
    ax::Vec2 laneSpawnPos(int lane) const;  // 车道 → 出怪位置（按当前布局计算）
    ax::Vec2 laneWallPos(int lane) const;   // 车道 → 城墙目标点（按当前布局计算）
    int laneCount() const;                  // 当前布局的有效车道数

    std::vector<Monster> _monsterPool;
    float _spawnTimer = 0.0f;
    int _wallHP         = 100;            // 城墙血量（撞墙扣血）
    int _killCount      = 0;              // 击杀计数（调试用）

    GameState _gameState                            = GameState::init;
    ax::EventListenerTouchAllAtOnce* _touchListener = nullptr;
    ax::EventListenerKeyboard* _keyboardListener    = nullptr;
    int _sceneID                                    = 0;
    ax::Node* _battlefield                          = nullptr;
    BattleLayout _battleLayout                      = BattleLayout::Oblique25D;
    // 记录上次布局时的可见尺寸，尺寸变化（旋转/窗口缩放）时自动重排战场
    ax::Size _lastVisibleSize                       = ax::Size::ZERO;
};

