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

#include "rapidjson/document.h"  // 引擎自带 JSON 解析库（波次配置用）

#include <algorithm>  // std::max
#include <cstdlib>    // rand / srand
#include <cstring>    // strcmp
#include <ctime>      // time

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
constexpr int LANE_COUNT    = 12;   // 加密车道：怪群感的第 1 步（配合出怪频率与抖动）
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

// ---- 怪物系统 ----
constexpr int   MONSTER_POOL_SIZE     = 48;     // 对象池容量：同时在屏怪物上限（怪群）
constexpr float MONSTER_SPEED         = 70.0f;  // 推进速度基准（像素/秒），个体有 ±15% 差异
constexpr int   MONSTER_ATTACK_DAMAGE = 5;      // 啃墙伤害（每秒一口）
constexpr float MONSTER_BODY_R        = 11.0f;  // 身体半径（占位圆，脚底为节点原点）
// 怪物类型数值表：加新怪先加这里（数值），再去 applyMonsterSkin 加外观
constexpr float FAST_SPEED_MUL        = 1.8f;   // 快速怪速度倍率
constexpr float FAST_BODY_SCALE       = 0.8f;   // 快速怪体型倍率
constexpr float RANGED_SPEED_MUL      = 0.9f;   // 远程怪走得稍慢
constexpr float RANGED_STOP_RATIO     = 0.62f;  // 停火位：战场纵深的 62% 处 ≈ 远区（最远色带）下缘
constexpr float RANGED_ATTACK_INTERVAL= 1.8f;   // 远程攻击间隔（秒）
constexpr int   RANGED_ATTACK_DAMAGE  = 4;      // 每发弹丸伤害
// ---- 敌方弹丸 ----
constexpr int   ENEMY_BOLT_POOL_SIZE  = 16;     // 同时在屏弹丸上限
constexpr float ENEMY_BOLT_SPEED      = 350.0f; // 弹丸速度（像素/秒，比箭慢，看得见飞）
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
    // 4. 射箭系统 + 怪物系统：对象池都在 init 时一次性建好（第一天就上，别后补）
    initArrowPool();
    initMonsterPool();
    initEnemyBoltPool();
    srand((unsigned int)time(nullptr));  // 随机车道用（真随机源，调试期足够）

    /////////////////////////////
    // 5. 波次管理：JSON 数据驱动（改波次配置不用碰代码）
    loadWaves();
    // HUD：顶部居中波次提示。位置由 buildBattlefield 负责（旋转/缩放时重排）
    _waveLabel = Label::createWithSystemFont("", "Arial", 30);
    _waveLabel->setTextColor(Color4B::WHITE);
    addChild(_waveLabel, 5);
    {
        auto vs = _director->getVisibleSize();
        auto o  = _director->getVisibleOrigin();
        _waveLabel->setPosition(Vec2(o.x + vs.width * 0.5f, o.y + vs.height - 30.0f));
    }

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

    // 布局重建后战场几何变了，飞行中的箭和在场怪物全部回收（回池，不销毁）
    for (auto& a : _arrowPool)
    {
        a.active = false;
        a.node->setVisible(false);
    }
    for (auto& m : _monsterPool)
    {
        m.active = false;
        m.node->setVisible(false);
    }
    for (auto& b : _boltPool)
    {
        b.active = false;
        b.node->setVisible(false);
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
    // 重开按钮：切换视角按钮正下方
    if (_debugRestartBtn)
    {
        const float s = DEBUG_BTN_SIZE;
        Vec2 p(origin.x + 16.0f, top - 16.0f - s - 12.0f - s);
        _debugRestartBtn->setPosition(p);
        _debugRestartBtnRect = Rect(p.x, p.y, s, s);
    }
    // ◀ ▶ 跳波按钮：第三排，左右并排
    if (_debugPrevBtn && _debugNextBtn)
    {
        const float s  = DEBUG_BTN_SIZE;
        const float y  = top - 16.0f - (s + 12.0f) * 2 - s;
        _debugPrevBtn->setPosition(Vec2(origin.x + 16.0f, y));
        _debugPrevBtnRect = Rect(origin.x + 16.0f, y, s, s);
        _debugNextBtn->setPosition(Vec2(origin.x + 16.0f + s + 12.0f, y));
        _debugNextBtnRect = Rect(origin.x + 16.0f + s + 12.0f, y, s, s);
    }
    // 波次 HUD：顶部居中
    if (_waveLabel)
    {
        _waveLabel->setPosition(Vec2((origin.x + right) * 0.5f, top - 30.0f));
    }

    // 战场重建回收了在场怪物（见函数开头）→ 重打当前波。
    // 配置是只读的，重置运行时状态即可；AllClear（全部打完）就不重开了
    if (!_waves.empty() && (_waveState == WaveState::Spawning || _waveState == WaveState::Resting))
    {
        beginWave(_waveIndex);
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

    // 重开按钮：切换视角按钮正下方，图标 ↻（圆弧+箭头）
    _debugRestartBtn = Node::create();
    auto bg2 = DrawNode::create();
    bg2->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.0f, 0.0f, 0.0f, 0.45f));
    bg2->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.7f));
    const Vec2 c(s * 0.5f, s * 0.5f);
    bg2->drawCircle(c, s * 0.26f, 0.0f, 24, false, w, 5.0f);  // 圆弧
    bg2->drawSolidTriangle(Vec2(s * 0.50f, s * 0.86f), Vec2(s * 0.70f, s * 0.70f),
                           Vec2(s * 0.48f, s * 0.66f), w, w);  // 箭头
    _debugRestartBtn->addChild(bg2);
    addChild(_debugRestartBtn, 10);

    // ◀ 上一波 / ▶ 下一波：第三排，左右并排
    _debugPrevBtn = Node::create();
    auto bg3 = DrawNode::create();
    bg3->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.0f, 0.0f, 0.0f, 0.45f));
    bg3->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.7f));
    bg3->drawSolidTriangle(Vec2(s * 0.70f, s * 0.72f), Vec2(s * 0.70f, s * 0.28f),
                           Vec2(s * 0.30f, s * 0.50f), w, w);  // ◀
    _debugPrevBtn->addChild(bg3);
    addChild(_debugPrevBtn, 10);

    _debugNextBtn = Node::create();
    auto bg4 = DrawNode::create();
    bg4->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.0f, 0.0f, 0.0f, 0.45f));
    bg4->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.7f));
    bg4->drawSolidTriangle(Vec2(s * 0.30f, s * 0.72f), Vec2(s * 0.30f, s * 0.28f),
                           Vec2(s * 0.70f, s * 0.50f), w, w);  // ▶
    _debugNextBtn->addChild(bg4);
    addChild(_debugNextBtn, 10);
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

        // 命中判定：箭尖点 vs 怪物圆（点-圆距离判定，最便宜的碰撞形状）
        // 判据 |arrowPos - monsterCenter|² < R² —— 用平方比较，省掉开根号
        for (auto& m : _monsterPool)
        {
            if (!m.active || m.state == MonsterState::Dead)
            {
                continue;
            }
            // 命中判定：箭尖点 vs 怪身圆。体型随类型缩放，所以用个体半径
            const float hitR    = m.bodyR + 6.0f;  // 身体半径 + 箭尖余量
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            if ((pos - bodyCenter).lengthSquared() < hitR * hitR)
            {
                m.state = MonsterState::Dead;  // 怪进 Dead 状态，updateMonsters 回收
                _killCount++;
                a.active = false;              // 箭也回收（命中即消失）
                a.node->setVisible(false);
                break;
            }
        }
        if (!a.active)
        {
            continue;  // 命中已回收，跳过出界检查
        }

        // 出界回收（归池）
        if (pos.x < origin.x - margin || pos.x > origin.x + visibleSize.width + margin ||
            pos.y < origin.y - margin || pos.y > origin.y + visibleSize.height + margin)
        {
            a.active = false;
            a.node->setVisible(false);
        }
    }
}

// ---------------------------------------------------------------------------
// 车道几何：统一数据模型的核心——怪物只关心"我在哪条车道、推进到哪"，
// 车道的世界坐标由当前布局换算，呈现层（两种视角）差异全部关在这三个函数里
// ---------------------------------------------------------------------------
int MainScene::laneCount() const
{
    // 2.5D 版是多车道（纵排），侧视版是单条地面轨迹
    return _battleLayout == BattleLayout::Oblique25D ? LANE_COUNT : 1;
}

ax::Vec2 MainScene::laneSpawnPos(int lane) const
{
    auto vs = _director->getVisibleSize();
    auto o  = _director->getVisibleOrigin();
    if (_battleLayout == BattleLayout::Oblique25D)
    {
        float laneW = vs.width / LANE_COUNT;
        return Vec2(o.x + laneW * (lane + 0.5f), o.y + vs.height * SPAWN_RATIO);
    }
    // SideView：右缘出怪口，站在地面顶边上
    return Vec2(o.x + vs.width * SPAWN_RATIO, o.y + vs.height * SIDE_GROUND_RATIO);
}

ax::Vec2 MainScene::laneWallPos(int lane) const
{
    auto vs = _director->getVisibleSize();
    auto o  = _director->getVisibleOrigin();
    if (_battleLayout == BattleLayout::Oblique25D)
    {
        float laneW = vs.width / LANE_COUNT;
        return Vec2(o.x + laneW * (lane + 0.5f), o.y + vs.height * WALL_RATIO);
    }
    // SideView：房子右缘（檐口探出 42px，加一点余量）
    return Vec2(o.x + vs.width * SIDE_HOUSE_X_RATIO + 46.0f, o.y + vs.height * SIDE_GROUND_RATIO);
}

// ---------------------------------------------------------------------------
// 怪物对象池：与箭矢池同一个思想——开局一次建好，出怪=激活，死亡=隐藏归池
// ---------------------------------------------------------------------------
void MainScene::initMonsterPool()
{
    _monsterPool.reserve(MONSTER_POOL_SIZE);
    for (int i = 0; i < MONSTER_POOL_SIZE; ++i)
    {
        Monster m;
        m.node = DrawNode::create();  // 空壳：外观推迟到出生时按类型画（槽位与外观分离）
        m.node->setVisible(false);
        addChild(m.node, 2);  // zOrder 2：战场之上、箭矢（3）之下
        _monsterPool.push_back(m);
    }
}

// 按类型画皮：普通怪=红色，快速怪=橙色偏小，远程怪=紫色
void MainScene::applyMonsterSkin(Monster& m)
{
    m.node->clear();  // DrawNode 可以清空重画，池化复用的关键
    const float r = m.bodyR;
    const float k = r / MONSTER_BODY_R;  // 五官随体型等比缩放
    Color4F body;
    switch (m.type)
    {
    case MonsterType::Fast:   body = Color4F(0.90f, 0.50f, 0.15f, 1.0f); break;  // 橙
    case MonsterType::Ranged: body = Color4F(0.62f, 0.30f, 0.72f, 1.0f); break;  // 紫
    default:                  body = Color4F(0.72f, 0.22f, 0.18f, 1.0f); break;  // 红
    }
    const Color4F horn(0.50f, 0.12f, 0.10f, 1.0f);
    m.node->drawSolidCircle(Vec2(0, r), r, 0.0f, 20, body);  // 身体
    m.node->drawSolidTriangle(Vec2(-r * 0.7f, r * 1.7f), Vec2(-r * 0.2f, r * 1.8f),
                              Vec2(-r * 0.5f, r * 2.3f), horn, horn);  // 左角
    m.node->drawSolidTriangle(Vec2(r * 0.7f, r * 1.7f), Vec2(r * 0.2f, r * 1.8f),
                              Vec2(r * 0.5f, r * 2.3f), horn, horn);   // 右角
    m.node->drawSolidCircle(Vec2(-5 * k, r + 3 * k), 2.5f * k, 0.0f, 8, Color4F::WHITE);  // 左眼
    m.node->drawSolidCircle(Vec2(5 * k, r + 3 * k), 2.5f * k, 0.0f, 8, Color4F::WHITE);   // 右眼
}

// 出生点按出生方向算：spawn=出怪线（默认）；left/right=侧翼，从屏外滑入
ax::Vec2 MainScene::groupSpawnPos(const std::string& from, int lane) const
{
    if (_battleLayout == BattleLayout::Oblique25D && (from == "left" || from == "right"))
    {
        auto vs = _director->getVisibleSize();
        auto o  = _director->getVisibleOrigin();
        const float wallTop = o.y + vs.height * WALL_RATIO;
        const float spawnY  = o.y + vs.height * SPAWN_RATIO;
        const float r1      = static_cast<float>(rand()) / RAND_MAX;
        // 战场中段高度随机一点，避免侧翼怪排队从同一个高度进来
        const float y = wallTop + 60.0f + r1 * (spawnY - wallTop - 120.0f);
        const float x = from == "left" ? o.x - 20.0f : o.x + vs.width + 20.0f;
        return Vec2(x, y);
    }
    return laneSpawnPos(lane);  // 默认：出怪线
}

void MainScene::spawnMonster(MonsterType type, int lane, const std::string& from)
{
    for (auto& m : _monsterPool)
    {
        if (m.active)
        {
            continue;
        }
        m.active      = true;
        m.state       = MonsterState::Walk;
        m.attackTimer = 0.0f;
        m.type        = type;
        m.lane        = lane >= 0 ? lane % laneCount() : (laneCount() > 1 ? rand() % laneCount() : 0);
        // 怪群感关键：同车道内横向抖动 + 个体速度差异。
        // 抖动对出生点和终点施加同一份偏移，怪物沿"平移后的车道"走直线，
        // 不会在半途被拉回车道中心线；侧翼滑入的不抖，保持轨迹干净
        const float r1 = static_cast<float>(rand()) / RAND_MAX;  // 0~1
        const float r2 = static_cast<float>(rand()) / RAND_MAX;
        Vec2 jitter;
        if (from == "spawn")
        {
            if (_battleLayout == BattleLayout::Oblique25D)
            {
                const float laneW = _director->getVisibleSize().width / LANE_COUNT;
                jitter = Vec2((r1 - 0.5f) * laneW * 0.56f, 0);  // 行进方向垂直 → 左右散开
            }
            else
            {
                jitter = Vec2(0, (r1 - 0.5f) * 24.0f);          // 侧视行进水平 → 上下散开
            }
        }
        // 类型数值：速度/体型/攻击方式都由类型表决定
        const float speedMul  = type == MonsterType::Fast ? FAST_SPEED_MUL
                              : type == MonsterType::Ranged ? RANGED_SPEED_MUL : 1.0f;
        const float bodyScale = type == MonsterType::Fast ? FAST_BODY_SCALE : 1.0f;
        m.speed          = MONSTER_SPEED * speedMul * (0.85f + r2 * 0.3f);  // 0.85~1.15 倍
        m.bodyR          = MONSTER_BODY_R * bodyScale;
        // 开火距离与攻击间隔：近战贴墙啃；远程走到远区边缘就停（按战场纵深
        // 比例算，横竖屏/任意屏幕都停在"最远色带"下缘，而不是写死像素）
        if (type == MonsterType::Ranged)
        {
            const float fieldDepth = _director->getVisibleSize().height * (SPAWN_RATIO - WALL_RATIO);
            m.attackRange    = fieldDepth * RANGED_STOP_RATIO;
            m.attackInterval = RANGED_ATTACK_INTERVAL;
        }
        else
        {
            m.attackRange    = 6.0f;
            m.attackInterval = 1.0f;
        }
        m.targetPos = laneWallPos(m.lane) + jitter;
        m.node->setPosition(groupSpawnPos(from, m.lane) + jitter);
        applyMonsterSkin(m);
        m.node->setVisible(true);
        return;
    }
    AXLOGD("monster pool full ({} active), spawn dropped", MONSTER_POOL_SIZE);
}

// ---------------------------------------------------------------------------
// 波次管理：数据驱动的核心实现。
// 三层结构：JSON 文件（策划层）→ WaveConfig（只读内存镜像）→ SpawnGroupRuntime
// （运行状态）。波次状态机：Idle → Spawning（按组出怪）→ Resting（清场休整）
// → 下一波 → AllClear（全部打完）
// ---------------------------------------------------------------------------
bool MainScene::loadWaves()
{
    _waves.clear();
    const std::string text = FileUtils::getInstance()->getStringFromFile("config/waves.json");
    rapidjson::Document doc;
    doc.Parse(text.c_str());
    // 兜底：配置系统的自我修养——文件缺失/格式错误，游戏也不能瘫
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("waves") || !doc["waves"].IsArray())
    {
        AXLOGD("waves.json missing or invalid, fallback to built-in wave");
        WaveConfig w;
        w.name      = "兜底波（waves.json 加载失败）";
        w.restAfter = 3.0f;
        w.groups.push_back({MonsterType::Normal, 10, 0.8f, 0.0f, -1, "spawn"});
        _waves.push_back(std::move(w));
        return false;
    }
    for (const auto& wv : doc["waves"].GetArray())
    {
        WaveConfig w;
        if (wv.HasMember("name") && wv["name"].IsString())
        {
            w.name = wv["name"].GetString();
        }
        if (wv.HasMember("restAfter") && wv["restAfter"].IsNumber())
        {
            w.restAfter = static_cast<float>(wv["restAfter"].GetDouble());
        }
        if (wv.HasMember("groups") && wv["groups"].IsArray())
        {
            for (const auto& g : wv["groups"].GetArray())
            {
                SpawnGroupConfig cfg;
                if (g.HasMember("type") && g["type"].IsString())
                {
                    const char* t = g["type"].GetString();
                    cfg.type      = strcmp(t, "fast") == 0 ? MonsterType::Fast
                                  : strcmp(t, "ranged") == 0 ? MonsterType::Ranged
                                                             : MonsterType::Normal;
                }
                if (g.HasMember("count") && g["count"].IsInt())
                {
                    cfg.count = g["count"].GetInt();
                }
                if (g.HasMember("interval") && g["interval"].IsNumber())
                {
                    cfg.interval = static_cast<float>(g["interval"].GetDouble());
                }
                if (g.HasMember("delay") && g["delay"].IsNumber())
                {
                    cfg.delay = static_cast<float>(g["delay"].GetDouble());
                }
                if (g.HasMember("lane") && g["lane"].IsInt())
                {
                    cfg.lane = g["lane"].GetInt();
                }
                if (g.HasMember("from") && g["from"].IsString())
                {
                    cfg.from = g["from"].GetString();
                }
                if (cfg.count > 0)
                {
                    w.groups.push_back(std::move(cfg));
                }
            }
        }
        if (!w.groups.empty())
        {
            _waves.push_back(std::move(w));
        }
    }
    AXLOGD("waves loaded: {}", _waves.size());
    return true;
}

void MainScene::beginWave(size_t index)
{
    _waveIndex    = index;
    const auto& w = _waves[index];
    _groupRt.assign(w.groups.size(), SpawnGroupRuntime{});
    // 计时器从 -delay 起步：负数期间只计时不出生，自然实现"本组延迟 N 秒"
    for (size_t i = 0; i < w.groups.size(); ++i)
    {
        _groupRt[i].timer = -w.groups[i].delay;
    }
    _waveState = WaveState::Spawning;
    if (_waveLabel)
    {
        _waveLabel->setString(w.name);
    }
    AXLOGD("wave {}/{} begin: {}", index + 1, _waves.size(), w.name);
}

void MainScene::updateWaves(float delta)
{
    if (_wallHP <= 0 || _waves.empty())
    {
        return;  // 城破或没配置：不再出怪
    }
    switch (_waveState)
    {
    case WaveState::Idle:
        beginWave(0);
        break;

    case WaveState::Spawning:
    {
        const auto& w  = _waves[_waveIndex];
        bool allSpawned = true;
        for (size_t i = 0; i < w.groups.size(); ++i)
        {
            const auto& cfg = w.groups[i];
            auto& rt        = _groupRt[i];
            if (rt.spawned >= cfg.count)
            {
                continue;  // 本组出完
            }
            rt.timer += delta;
            if (rt.timer >= 0.0f)
            {
                rt.timer -= cfg.interval;  // 到 0 出第一只，之后每 interval 一只
                spawnMonster(cfg.type, cfg.lane, cfg.from);
                ++rt.spawned;
            }
            if (rt.spawned < cfg.count)
            {
                allSpawned = false;
            }
        }
        // 全部出完且场上无活怪 → 进休整（不是出完就休整，要给玩家清场的过程）
        if (allSpawned)
        {
            bool anyActive = false;
            for (const auto& m : _monsterPool)
            {
                if (m.active)
                {
                    anyActive = true;
                    break;
                }
            }
            if (!anyActive)
            {
                _waveState = WaveState::Resting;
                _restTimer = w.restAfter;
            }
        }
        break;
    }

    case WaveState::Resting:
        _restTimer -= delta;
        if (_waveLabel)
        {
            _waveLabel->setString(StringUtils::format("下一波来袭 %d", static_cast<int>(_restTimer) + 1));
        }
        if (_restTimer <= 0.0f)
        {
            if (_waveIndex + 1 < _waves.size())
            {
                beginWave(_waveIndex + 1);
            }
            else
            {
                _waveState = WaveState::AllClear;
                if (_waveLabel)
                {
                    _waveLabel->setString("已守住全部波次！");
                }
                AXLOGD("all waves cleared, kills={}", _killCount);
            }
        }
        break;

    case WaveState::AllClear:
        break;  // 结算画面留给后续里程碑
    }
}

// ---------------------------------------------------------------------------
// 怪物状态机驱动：Walk → 沿车道向城墙移动；Attack → 每秒啃一口扣墙血；
// Dead → 回收归池。状态机是游戏 AI 的入门结构：任何时刻怪物只在一个状态里，
// 每个状态只做自己的事，跳转条件写在状态内部，清晰不易乱。
// ---------------------------------------------------------------------------
void MainScene::updateMonsters(float delta)
{
    for (auto& m : _monsterPool)
    {
        if (!m.active)
        {
            continue;
        }
        switch (m.state)
        {
        case MonsterState::Walk:
        {
            Vec2 pos    = m.node->getPosition();
            Vec2 target = m.targetPos;  // 出生时已含车道偏移，直线推进不纠偏
            Vec2 dir    = target - pos;
            // 进入开火距离就停下：近战 6px（贴墙），远程 320px（停火线）
            if (dir.length() <= m.attackRange)
            {
                m.state       = MonsterState::Attack;
                m.attackTimer = m.attackInterval;  // 到站立刻来一发，不白等一拍
                break;
            }
            dir.normalize();
            m.node->setPosition(pos + dir * m.speed * delta);
            break;
        }
        case MonsterState::Attack:
        {
            m.attackTimer += delta;
            if (m.attackTimer >= m.attackInterval)
            {
                m.attackTimer -= m.attackInterval;
                if (m.type == MonsterType::Ranged)
                {
                    // 远程：发射弹丸，伤害在弹丸命中城墙时结算
                    fireEnemyBolt(m.node->getPosition() + Vec2(0, m.bodyR), m.targetPos,
                                  RANGED_ATTACK_DAMAGE);
                }
                else
                {
                    damageWall(MONSTER_ATTACK_DAMAGE);  // 近战：直接啃
                }
            }
            break;
        }
        case MonsterState::Dead:
            m.active = false;
            m.node->setVisible(false);
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 调试用重开：清场（三池全收）+ 城墙满血 + 回到第 1 波。
// 注意全程没有 new/delete——对象本来就是池化的，"重置"只是改状态位，
// 这也是对象池的红利之一：重开一局是 O(池大小) 的零分配操作
// ---------------------------------------------------------------------------
void MainScene::clearField()
{
    for (auto& a : _arrowPool)
    {
        a.active = false;
        a.node->setVisible(false);
    }
    for (auto& m : _monsterPool)
    {
        m.active = false;
        m.node->setVisible(false);
    }
    for (auto& b : _boltPool)
    {
        b.active = false;
        b.node->setVisible(false);
    }
}

void MainScene::resetGame()
{
    clearField();
    _wallHP    = WALL_MAX_HP;
    _killCount = 0;
    _waveState = WaveState::Idle;  // 下一帧 updateWaves 自动 beginWave(0)
    if (_waveLabel)
    {
        _waveLabel->setString("");
    }
    AXLOGD("game reset by debug");
}

// 调试跳波：跳到指定波（清场，但不动城墙血量和击杀数——那是 resetGame 的事）
void MainScene::gotoWave(int offset)
{
    if (_waves.empty())
    {
        return;
    }
    int next = static_cast<int>(_waveIndex) + offset;
    next     = std::clamp(next, 0, static_cast<int>(_waves.size()) - 1);
    clearField();
    beginWave(static_cast<size_t>(next));
    AXLOGD("jump to wave {}", next + 1);
}

// ---------------------------------------------------------------------------
// 城墙扣血统一入口：近战啃咬和远程弹丸都走这里，城破判定只有一处
// ---------------------------------------------------------------------------
void MainScene::damageWall(int dmg)
{
    _wallHP = std::max(0, _wallHP - dmg);
    AXLOGD("wall HP: {}, kills: {}", _wallHP, _killCount);
    if (_wallHP == 0)
    {
        AXLOGD("城墙告破！（后续做结算画面）");
    }
}

// ---------------------------------------------------------------------------
// 敌方弹丸池：与箭矢池同一思想——开局建好，发射=激活，命中=归池
// ---------------------------------------------------------------------------
void MainScene::initEnemyBoltPool()
{
    _boltPool.reserve(ENEMY_BOLT_POOL_SIZE);
    for (int i = 0; i < ENEMY_BOLT_POOL_SIZE; ++i)
    {
        EnemyBolt b;
        b.node = DrawNode::create();
        // 紫色小弹丸：内核 + 外圈光晕，和玩家箭矢（白色）一眼区分开
        b.node->drawSolidCircle(Vec2::ZERO, 6.0f, 0.0f, 12, Color4F(0.75f, 0.35f, 0.90f, 1.0f));
        b.node->drawSolidCircle(Vec2::ZERO, 3.0f, 0.0f, 10, Color4F(1.0f, 1.0f, 1.0f, 0.9f));
        b.node->setVisible(false);
        addChild(b.node, 2);  // 与怪物同层
        _boltPool.push_back(b);
    }
}

void MainScene::fireEnemyBolt(const Vec2& from, const Vec2& target, int damage)
{
    for (auto& b : _boltPool)
    {
        if (b.active)
        {
            continue;
        }
        Vec2 dir = target - from;
        if (dir.lengthSquared() < 1.0f)
        {
            return;  // 零向量不能归一化（见知识点手册：normalize）
        }
        b.active    = true;
        b.velocity  = dir.getNormalized() * ENEMY_BOLT_SPEED;
        b.targetPos = target;
        b.damage    = damage;
        b.node->setPosition(from);
        b.node->setVisible(true);
        return;
    }
    AXLOGD("enemy bolt pool full, dropped");
}

void MainScene::updateEnemyBolts(float delta)
{
    for (auto& b : _boltPool)
    {
        if (!b.active)
        {
            continue;
        }
        Vec2 pos = b.node->getPosition() + b.velocity * delta;
        // 到达瞄准点 = 命中城墙：结算伤害并回收
        if ((pos - b.targetPos).lengthSquared() < 12.0f * 12.0f)
        {
            damageWall(b.damage);
            b.active = false;
            b.node->setVisible(false);
            continue;
        }
        b.node->setPosition(pos);
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
        if (_debugRestartBtnRect.containsPoint(t->getLocation()))
        {
            resetGame();  // ↻ 重开：清场满血回第 1 波
            return;
        }
        if (_debugPrevBtnRect.containsPoint(t->getLocation()))
        {
            gotoWave(-1);  // ◀ 上一波
            return;
        }
        if (_debugNextBtnRect.containsPoint(t->getLocation()))
        {
            gotoWave(1);  // ▶ 下一波
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
    else if (code == EventKeyboard::KeyCode::KEY_R)
    {
        resetGame();  // 桌面调试：R = 重开（清场满血回第 1 波）
    }
    else if (code == EventKeyboard::KeyCode::KEY_Q)
    {
        gotoWave(-1);  // Q = 上一波
    }
    else if (code == EventKeyboard::KeyCode::KEY_E)
    {
        gotoWave(1);  // E = 下一波
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

        // 波次管理：JSON 数据驱动出怪（取代写死的固定间隔）
        updateWaves(delta);

        // 游戏主循环：怪物移动、箭矢飞行、敌方弹丸、碰撞判定都在这里驱动
        updateMonsters(delta);
        updateArrows(delta);
        updateEnemyBolts(delta);
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
