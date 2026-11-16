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

#include "MainScene.h"

using namespace ax;

// 场景实例计数（模板自带，调试用：每创建一个场景 +1，析构时置 -1）
static int s_sceneID = 1000;

// ---------------------------------------------------------------------------
// 战场布局常量（单位：设计分辨率比例，不是绝对像素！）
//
// 两个并行的视角风格版本（2026-11-16 与 Stark 确认）：
//   Oblique25D 伪 2.5D 斜俯视 = 原作构图（主线）：城墙在底部窄边、出怪线在顶部
//              窄边，怪物沿多车道（纵排）上→下推进；车道即轨迹，地面纵深色带表远近
//   SideView   纯侧视角 = 新增风格版本：底部一条地面、左侧灰色房子为防守目标，
//              怪物从右缘进入，沿地面右→左进攻房子（单条地面轨迹）
//
// 横竖屏适配原则：不做两份手工场景。所有布局都按【当前可见尺寸】计算，
// 尺寸变化（手机旋转 / 桌面窗口缩放）时走同一条 buildBattlefield 重建路径。
// 微信端将来在 game.json 放开方向后，由 wx.onWindowResize 触发同一机制。
//
// 为什么用比例：设计分辨率 720x1280，但 SHOW_ALL 策略下瘦长屏可见高度更大
// （模拟器 iPhone 12/13 Pro 可见 720x1558）。用比例布局，任何屏幕比例下
// 城墙、出怪线、车道的相对位置都一致 —— 这就是"分辨率适配"的第一课。
// ---------------------------------------------------------------------------
namespace
{
// ---- 两版共用 ----
// 出怪线位置占可见区域的比例（对面窄边）
constexpr float SPAWN_RATIO = 0.96f;
// Oblique25D：城墙高度占比；车道数量（纵排，原作就是多车道出怪）
constexpr float WALL_RATIO  = 0.12f;
constexpr int LANE_COUNT    = 6;
// Oblique25D：地面纵深色带数量（远亮近暗，用纯色带先模拟 2.5D 纵深）
constexpr int GROUND_BANDS = 3;
// SideView：地面条高度占比；房子中心 x 占比（靠左）
constexpr float SIDE_GROUND_RATIO  = 0.25f;
constexpr float SIDE_HOUSE_X_RATIO = 0.10f;

// ---- 射箭系统 ----
constexpr int   ARROW_POOL_SIZE = 32;       // 对象池容量：同时在屏箭矢上限
constexpr float ARROW_SPEED     = 1000.0f;  // 箭矢初速（像素/秒）——手感调优清单第 1 项
// 重力加速度（像素/秒²）。0 = 直线弹道；调成 600~900 就是抛物线——
// 手感调优清单第 2 项"抛物线 vs 直线"，改这一个常量即可对比
constexpr float ARROW_GRAVITY   = 0.0f;
constexpr float RAD2DEG         = 57.2957795f;  // 弧度→角度（180/π）

// ---- 调试层 ----
constexpr float DEBUG_BTN_SIZE  = 88.0f;    // 左上角"切换视角"按钮边长（设计像素）
}  // namespace

// on "init" you need to initialize your instance
bool MainScene::init()
{
    //////////////////////////////
    // 1. super init first
    if (!Scene::init())
    {
        return false;
    }

    /////////////////////////////
    // 2. 调试层：常驻"切换视角"按钮（先建好，buildBattlefield 会负责定位）
    // （12:04 模拟器黑屏曾误以为是它，实乃开发者工具实例损坏，已平反）
    initDebugUI();

    /////////////////////////////
    // 3. 战场骨架：全部用 DrawNode 矢量绘制占位，不依赖美术资源
    //    DrawNode 是引擎自带的"画板节点"：直线/矩形/圆/多边形直接画，
    //    原型验证和调试可视化的利器。默认伪 2.5D 斜俯视，按 1/2 键切换。
    buildBattlefield(_battleLayout);

    /////////////////////////////
    // 4. 射箭系统：对象池在 init 时一次性建好（第一天就上，别后补）
    initArrowPool();

    /////////////////////////////
    // 5. 输入监听：触摸射箭，键盘桌面调试用
    _touchListener                 = EventListenerTouchAllAtOnce::create();
    _touchListener->onTouchesBegan = AX_CALLBACK_2(MainScene::onTouchesBegan, this);
    _touchListener->onTouchesMoved = AX_CALLBACK_2(MainScene::onTouchesMoved, this);
    _touchListener->onTouchesEnded = AX_CALLBACK_2(MainScene::onTouchesEnded, this);
    _eventDispatcher->addEventListenerWithSceneGraphPriority(_touchListener, this);

    _keyboardListener                = EventListenerKeyboard::create();
    _keyboardListener->onKeyPressed  = AX_CALLBACK_2(MainScene::onKeyPressed, this);
    _keyboardListener->onKeyReleased = AX_CALLBACK_2(MainScene::onKeyReleased, this);
    _eventDispatcher->addEventListenerWithFixedPriority(_keyboardListener, 11);

    // scheduleUpdate() is required to ensure update(float) is called on every loop
    scheduleUpdate();

    return true;
}

// ---------------------------------------------------------------------------
// 按布局重建战场：所有战场节点挂在一个容器 Node 下，切换/旋转时整体销毁重画。
// 这是一种很常用的"场景重组"手法——容器节点的孩子是相对容器定位的，
// 这里容器放原点不动，孩子直接用世界坐标，简单直观。
// ---------------------------------------------------------------------------
void MainScene::buildBattlefield(BattleLayout layout)
{
    if (_battlefield)
    {
        removeChild(_battlefield);
        _battlefield = nullptr;
    }
    _battleLayout     = layout;
    _battlefield      = Node::create();
    addChild(_battlefield, 0);

    // 坐标系约定：原点在左下角，x 向右，y 向上（和浏览器 DOM 相反！）
    auto visibleSize  = _director->getVisibleSize();
    auto origin       = _director->getVisibleOrigin();
    _lastVisibleSize  = visibleSize;  // 记录本次布局尺寸，供旋转/缩放检测
    const float right = origin.x + visibleSize.width;
    const float top   = origin.y + visibleSize.height;

    // 画虚线段的工具常量。注意：drawLine 只有 1 像素宽，手机高缩放比下会被
    // 抗锯齿"吞掉"（上次看不到出怪线的原因），虚线段一律用 drawSolidRect
    const float dash = 26.0f, gap = 16.0f, lineW = 6.0f;

    // 布局重建后箭手位变了，飞行中的箭全部回收（回池，不销毁）
    for (auto& a : _arrowPool)
    {
        a.active = false;
        a.node->setVisible(false);
    }

    if (layout == BattleLayout::SideView)
    {
        // ======================= 纯侧视角（新增风格版本） =======================
        // 底部一条地面，左侧灰色房子是防守目标，怪物从右缘进入沿地面右→左进攻
        const float groundTop = origin.y + visibleSize.height * SIDE_GROUND_RATIO;
        const float spawnX    = origin.x + visibleSize.width * SPAWN_RATIO;

        // 地面条（贯穿全宽）
        auto ground = DrawNode::create();
        ground->drawSolidRect(origin, Vec2(right, groundTop), Color4F(0.29f, 0.20f, 0.12f, 1.0f));
        // 地面顶边一条亮线 = 怪物的行进轨迹（单车道）
        ground->drawSolidRect(Vec2(origin.x, groundTop - 1.5f), Vec2(right, groundTop + 1.5f),
                              Color4F(0.6f, 0.8f, 0.4f, 0.9f));
        _battlefield->addChild(ground, 0);

        // 房子（左）：灰色小屋立在地面顶边上——这就是怪物要攻击的目标
        // DrawNode 的几何都是以自身 (0,0) 画的，position 就是这块"画布"的局部原点
        auto house = DrawNode::create();
        house->drawSolidRect(Vec2(-34, 0), Vec2(34, 96), Color4F(0.55f, 0.55f, 0.60f, 1.0f));   // 墙体
        house->drawSolidRect(Vec2(-42, 96), Vec2(42, 112), Color4F(0.45f, 0.45f, 0.50f, 1.0f));  // 檐口
        house->drawSolidRect(Vec2(-10, 0), Vec2(10, 36), Color4F(0.30f, 0.22f, 0.14f, 1.0f));    // 门
        house->drawLine(Vec2(0, 112), Vec2(0, 150), Color4F::WHITE);                             // 旗杆
        house->drawSolidTriangle(Vec2(0, 150), Vec2(0, 132), Vec2(28, 141),
                                 Color4F::RED, Color4F::RED);                                      // 旗面
        house->setPosition(Vec2(origin.x + visibleSize.width * SIDE_HOUSE_X_RATIO, groundTop));
        _battlefield->addChild(house, 1);

        // 箭手位：屋顶旗杆下（箭从这里射出）
        _archerPos = house->getPosition() + Vec2(0, 112);

        // 出怪口（右缘，地面高度处的一小段红色虚线"门"）
        const float gateH = visibleSize.height * 0.10f;
        auto spawnGate = DrawNode::create();
        for (float y = groundTop; y + dash <= groundTop + gateH; y += dash + gap)
        {
            spawnGate->drawSolidRect(Vec2(spawnX, y), Vec2(spawnX + lineW, y + dash),
                                     Color4F(0.9f, 0.15f, 0.15f, 1.0f));
        }
        _battlefield->addChild(spawnGate, 1);
    }
    else
    {
        // ==================== 伪 2.5D 斜俯视（原作构图，主线） ====================
        // 城墙在底（横条），地面在上，出怪线在顶缘；车道纵排，色带上浅下深
        const float wallTop = origin.y + visibleSize.height * WALL_RATIO;
        const float spawnY  = origin.y + visibleSize.height * SPAWN_RATIO;

        // 地面：三条色带上浅下深模拟纵深（上远下近）
        // 怪物在平面内移动；后续按 y 排序绘制（远的先画、近的后画）
        auto ground = DrawNode::create();
        const Color4F bandColors[GROUND_BANDS] = {
            Color4F(0.24f, 0.18f, 0.11f, 1.0f),  // 近处（底部）：深棕
            Color4F(0.30f, 0.24f, 0.15f, 1.0f),  // 中间
            Color4F(0.36f, 0.30f, 0.20f, 1.0f),  // 远处（顶部）：浅棕
        };
        const float bandH = (top - wallTop) / GROUND_BANDS;
        for (int i = 0; i < GROUND_BANDS; ++i)
        {
            ground->drawSolidRect(Vec2(origin.x, wallTop + i * bandH),
                                  Vec2(right, wallTop + (i + 1) * bandH), bandColors[i]);
        }
        // 色带分界线描亮线，方便看清"远近"
        for (int i = 1; i < GROUND_BANDS; ++i)
        {
            float y = wallTop + i * bandH;
            ground->drawSolidRect(Vec2(origin.x, y - 1.5f), Vec2(right, y + 1.5f),
                                  Color4F(0.55f, 0.48f, 0.32f, 0.6f));
        }
        _battlefield->addChild(ground, 0);

        // 车道：LANE_COUNT 条纵向分隔（怪物沿车道上→下推进，原作就是多车道出怪）
        auto lanes = DrawNode::create();
        const float laneW = visibleSize.width / LANE_COUNT;
        for (int i = 1; i < LANE_COUNT; ++i)
        {
            float x = origin.x + i * laneW;
            lanes->drawSolidRect(Vec2(x - 1.5f, wallTop), Vec2(x + 1.5f, top),
                                 Color4F(0.55f, 0.48f, 0.32f, 0.6f));
        }
        _battlefield->addChild(lanes, 1);

        // 城墙（底）：横条墙体 + 顶缘墙垛 + 中央箭手位小旗（箭矢从这里射出）
        auto wall = DrawNode::create();
        wall->drawSolidRect(origin, Vec2(right, wallTop), Color4F(0.50f, 0.50f, 0.55f, 1.0f));
        const float merlonW = 22.0f, merlonH = 14.0f, merlonGap = 14.0f;
        for (float x = origin.x; x + merlonW <= right; x += merlonW + merlonGap)
        {
            wall->drawSolidRect(Vec2(x, wallTop), Vec2(x + merlonW, wallTop + merlonH),
                                Color4F(0.62f, 0.62f, 0.67f, 1.0f));
        }
        const float centerX = (origin.x + right) * 0.5f;
        wall->drawSolidCircle(Vec2(centerX, wallTop), 10.0f, 0.0f, 16,
                              Color4F(0.85f, 0.70f, 0.30f, 1.0f));
        wall->drawLine(Vec2(centerX, wallTop), Vec2(centerX, wallTop + 46), Color4F::WHITE);
        wall->drawSolidTriangle(Vec2(centerX, wallTop + 46), Vec2(centerX, wallTop + 28),
                                Vec2(centerX + 26, wallTop + 37), Color4F::RED, Color4F::RED);
        _battlefield->addChild(wall, 1);

        // 箭手位：城墙顶缘中央（金点处）
        _archerPos = Vec2(centerX, wallTop);

        // 出怪线（顶）：横向红色虚线，贯穿全宽
        auto spawnLine = DrawNode::create();
        for (float x = origin.x; x + dash <= right; x += dash + gap)
        {
            spawnLine->drawSolidRect(Vec2(x, spawnY), Vec2(x + dash, spawnY + lineW),
                                     Color4F(0.9f, 0.15f, 0.15f, 1.0f));
        }
        _battlefield->addChild(spawnLine, 1);
    }

    // 调试按钮跟随布局定位：左上角留边区（避开右上角的微信胶囊菜单）
    if (_debugBtn)
    {
        const float s = DEBUG_BTN_SIZE;
        Vec2 p(origin.x + 16.0f, top - 16.0f - s);
        _debugBtn->setPosition(p);
        _debugBtnRect = Rect(p.x, p.y, s, s);
    }

    AXLOGD("battlefield rebuilt: {}, visibleSize=({:.0f}, {:.0f})",
           layout == BattleLayout::Oblique25D ? "Oblique25D" : "SideView",
           visibleSize.width, visibleSize.height);
}

// ---------------------------------------------------------------------------
// 调试层：左上角常驻"切换视角"按钮。
// 微信模拟器里鼠标无法模拟双指、也没有键盘，调试入口必须是触屏可点的。
// 按钮不挂在战场容器里（战场重建时不会被一起销毁），位置由 buildBattlefield 更新。
// ---------------------------------------------------------------------------
void MainScene::initDebugUI()
{
    const float s = DEBUG_BTN_SIZE;
    _debugBtn     = Node::create();

    auto bg = DrawNode::create();
    bg->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.0f, 0.0f, 0.0f, 0.45f));  // 半透明底
    bg->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.7f));        // 边框
    // 图标：一右一左两个箭头（⇄），表示"切换"
    const Color4F w(1.0f, 1.0f, 1.0f, 0.9f);
    bg->drawSolidRect(Vec2(s * 0.20f, s * 0.60f), Vec2(s * 0.66f, s * 0.68f), w);
    bg->drawSolidTriangle(Vec2(s * 0.66f, s * 0.76f), Vec2(s * 0.66f, s * 0.52f), Vec2(s * 0.84f, s * 0.64f), w, w);
    bg->drawSolidRect(Vec2(s * 0.34f, s * 0.32f), Vec2(s * 0.80f, s * 0.40f), w);
    bg->drawSolidTriangle(Vec2(s * 0.34f, s * 0.48f), Vec2(s * 0.34f, s * 0.24f), Vec2(s * 0.16f, s * 0.36f), w, w);
    _debugBtn->addChild(bg);

    addChild(_debugBtn, 10);  // zOrder 10：调试层永远在最上面
}

// ---------------------------------------------------------------------------
// 对象池初始化：开局一次性创建 ARROW_POOL_SIZE 支箭，全部隐藏。
// 为什么第一天就上对象池：游戏里箭矢每秒创建/销毁几十次，若每次都 new 节点
// 再 removeFromParent，内存分配抖动会造成帧率毛刺；池化后"发射"只是
// 把一个闲置槽位 setVisible(true)，"回收"只是 setVisible(false)。
// ---------------------------------------------------------------------------
void MainScene::initArrowPool()
{
    _arrowPool.reserve(ARROW_POOL_SIZE);
    for (int i = 0; i < ARROW_POOL_SIZE; ++i)
    {
        Arrow a;
        a.node = DrawNode::create();
        // 箭矢形状朝 +x 方向画：发射时按弹道角旋转节点即可（不用重画几何）
        a.node->drawSolidRect(Vec2(-12, -1.5f), Vec2(8, 1.5f), Color4F(0.85f, 0.72f, 0.40f, 1.0f));  // 箭杆
        a.node->drawSolidTriangle(Vec2(8, 4.5f), Vec2(8, -4.5f), Vec2(18, 0),
                                  Color4F(0.92f, 0.90f, 0.85f, 1.0f), Color4F(0.92f, 0.90f, 0.85f, 1.0f));  // 箭头
        a.node->setVisible(false);
        addChild(a.node, 3);  // zOrder 3：在战场（0/1）之上
        _arrowPool.push_back(a);
    }
}

// ---------------------------------------------------------------------------
// 发射一支箭：向量数学是这里的主角——
//   方向向量 dir = (target - archer).normalize()  （指向目标、长度为 1）
//   速度向量 velocity = dir * ARROW_SPEED          （方向 × 速率）
//   每帧积分   position += velocity * delta         （欧拉积分，游戏物理的根基）
// ---------------------------------------------------------------------------
void MainScene::fireArrow(const ax::Vec2& target)
{
    Vec2 dir = target - _archerPos;
    if (dir.lengthSquared() < 1.0f)
    {
        return;  // 点就点在箭手位上，无方向可射
    }
    dir.normalize();

    for (auto& a : _arrowPool)
    {
        if (a.active)
        {
            continue;
        }
        // 找到一个闲置槽位：激活它
        a.active   = true;
        a.velocity = dir * ARROW_SPEED;
        a.node->setPosition(_archerPos);
        // 节点图形朝 +x 画，旋转到弹道方向。注意 axmol 的 rotation 是
        // 【顺时针】为正的角度制，而 atan2 是数学系逆时针弧度 → 取负再转角度
        a.node->setRotation(-atan2f(dir.y, dir.x) * RAD2DEG);
        a.node->setVisible(true);
        return;
    }
    // 池满了：说明射速超过回收速度——手感调优清单"连点射速上限"的入口
    AXLOGD("arrow pool full ({} in flight), shot dropped", ARROW_POOL_SIZE);
}

// ---------------------------------------------------------------------------
// 每帧驱动箭矢：欧拉积分移动，出界回收。
// 命中判定目前只有"出界/落地"一种；等怪物系统进来后，在这里加
// "箭尖位置 vs 怪物包围盒"的点判定即可——判定逻辑都集中在这个函数里。
// ---------------------------------------------------------------------------
void MainScene::updateArrows(float delta)
{
    const auto visibleSize = _director->getVisibleSize();
    const auto origin      = _director->getVisibleOrigin();
    const float margin     = 80.0f;  // 出界多少像素后才回收（避免在边缘突然消失）

    for (auto& a : _arrowPool)
    {
        if (!a.active)
        {
            continue;
        }

        // 重力（ARROW_GRAVITY = 0 时这步什么都不做，直线弹道）
        if (ARROW_GRAVITY != 0.0f)
        {
            a.velocity.y -= ARROW_GRAVITY * delta;
            // 抛物线时箭头方向要跟着速度方向走
            a.node->setRotation(-atan2f(a.velocity.y, a.velocity.x) * RAD2DEG);
        }

        Vec2 pos = a.node->getPosition() + a.velocity * delta;
        a.node->setPosition(pos);

        // 出界回收（归池）
        if (pos.x < origin.x - margin || pos.x > origin.x + visibleSize.width + margin ||
            pos.y < origin.y - margin || pos.y > origin.y + visibleSize.height + margin)
        {
            a.active = false;
            a.node->setVisible(false);
        }
    }
}

void MainScene::onTouchesBegan(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    // 输入优先级：调试按钮 > 双指手势 > 射箭。
    // 命中测试：Rect::containsPoint 判断触点是否在按钮热区内——
    // 这就是最朴素的"UI 按钮"实现（一个矩形 + 点包含判定）
    for (auto&& t : touches)
    {
        if (_debugBtnRect.containsPoint(t->getLocation()))
        {
            buildBattlefield(_battleLayout == BattleLayout::Oblique25D ? BattleLayout::SideView
                                                                       : BattleLayout::Oblique25D);
            return;
        }
    }

    // 双指点击 = 切换视角版本（真机手势；模拟器请用左上角调试按钮）
    // EventListenerTouchAllAtOnce 的 touches 数组长度 = 当前按下的手指数
    if (touches.size() >= 2)
    {
        buildBattlefield(_battleLayout == BattleLayout::Oblique25D ? BattleLayout::SideView
                                                                   : BattleLayout::Oblique25D);
        return;
    }
    for (auto&& t : touches)
    {
        // 单指点击屏幕 → 从箭手位向点击方向射箭
        // t->getLocation() 是世界坐标（已含分辨率适配的换算）
        fireArrow(t->getLocation());
    }
}

void MainScene::onTouchesMoved(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
}

void MainScene::onTouchesEnded(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
}

void MainScene::onKeyPressed(ax::EventKeyboard::KeyCode code, ax::Event* event)
{
    // 桌面调试：按 1 = 纯侧视角（新增风格版），按 2 = 伪 2.5D 斜俯视（原作构图）
    if (code == EventKeyboard::KeyCode::KEY_1 && _battleLayout != BattleLayout::SideView)
    {
        buildBattlefield(BattleLayout::SideView);
    }
    else if (code == EventKeyboard::KeyCode::KEY_2 && _battleLayout != BattleLayout::Oblique25D)
    {
        buildBattlefield(BattleLayout::Oblique25D);
    }
}

void MainScene::onKeyReleased(ax::EventKeyboard::KeyCode code, ax::Event* event)
{
}

void MainScene::update(float delta)
{
    switch (_gameState)
    {
    case GameState::init:
    {
        _gameState = GameState::update;
        break;
    }

    case GameState::update:
    {
        // 横竖屏/窗口缩放自适应：可见尺寸变了就重排战场（每帧一次比较，开销可忽略）
        // 微信端将来由 wx.onWindowResize 触发的也是同一条路径
        const auto& vs = _director->getVisibleSize();
        if (vs.width != _lastVisibleSize.width || vs.height != _lastVisibleSize.height)
        {
            buildBattlefield(_battleLayout);
        }

        // 游戏主循环：怪物移动、箭矢飞行、碰撞判定都将在这里驱动
        updateArrows(delta);
        break;
    }

    case GameState::pause:
    case GameState::menu1:
    case GameState::menu2:
    {
        break;
    }

    case GameState::end:
    {
        break;
    }

    }  // switch
}

MainScene::MainScene()
{
    _sceneID = ++s_sceneID;
}

MainScene::~MainScene()
{
    if (_touchListener)
        _eventDispatcher->removeEventListener(_touchListener);
    if (_keyboardListener)
        _eventDispatcher->removeEventListener(_keyboardListener);
    _sceneID = -1;
}
