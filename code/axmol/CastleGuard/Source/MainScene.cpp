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

#if CG_DEBUG_TOOLS
#    include "ImGui/ImGuiPresenter.h"  // 引擎扩展：手感调优面板（正式包剔除）
#endif
#include "rapidjson/document.h"        // 引擎自带 JSON 解析库（波次配置用）

#include <algorithm>  // std::max
#include <cfloat>     // FLT_MAX（闪电找最近主目标）
#include <cstdlib>    // rand / srand
#include <cstring>    // strcmp
#include <ctime>      // time

using namespace ax;
#if CG_DEBUG_TOOLS
USING_NS_AX_EXT;
#endif

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
// 手感调优清单 4 项全在这（ImGui 面板实时拖滑杆，别编译了）：
float ARROW_SPEED     = 1000.0f;  // ① 箭矢初速（像素/秒）
// 重力加速度（像素/秒²）。0 = 直线弹道；调成 600~900 就是抛物线
float ARROW_GRAVITY   = 0.0f;     // ② 抛物线 vs 直线
float FIRE_INTERVAL   = 0.18f;    // ④ 连点射速上限（秒/发）
constexpr float RAD2DEG         = 57.2957795f;  // 弧度→角度（180/π）

// ---- 调试层 ----
#if CG_DEBUG_TOOLS
constexpr float DEBUG_BTN_SIZE  = 88.0f;    // 左上角"切换视角"按钮边长（设计像素）
#endif

// ---- 怪物系统 ----
constexpr int   MONSTER_POOL_SIZE     = 48;     // 对象池容量：同时在屏怪物上限（怪群）
float MONSTER_SPEED                   = 70.0f;  // ③ 推进速度基准（ImGui 可调），个体 ±15% 差异
constexpr int   MONSTER_ATTACK_DAMAGE = 5;      // 啃墙伤害（每秒一口）
constexpr float MONSTER_BODY_R        = 11.0f;  // 身体半径（占位圆，脚底为节点原点）
// 怪物类型数值表：加新怪先加这里（数值），再去 applyMonsterSkin 加外观
constexpr float FAST_SPEED_MUL        = 1.8f;   // 快速怪速度倍率
constexpr float FAST_BODY_SCALE       = 0.8f;   // 快速怪体型倍率
constexpr float RANGED_SPEED_MUL      = 0.9f;   // 远程怪走得稍慢
float RANGED_STOP_RATIO               = 0.62f;  // ⑤ 停火位：战场纵深比例（ImGui 可调）
constexpr float RANGED_ATTACK_INTERVAL= 1.8f;   // 远程攻击间隔（秒）
constexpr int   RANGED_ATTACK_DAMAGE  = 4;      // 每发弹丸伤害
// 护盾怪：箭矢免疫只怕技能（克制设计：逼玩家用技能），血厚走得慢
constexpr float SHIELD_SPEED_MUL      = 0.75f;  // 速度倍率（慢，给玩家留技能冷却时间）
constexpr float SHIELD_BODY_SCALE     = 1.25f;  // 体型倍率（大，一眼能认出是"硬目标"）
constexpr int   SHIELD_HP             = 3;      // 血量（当前技能伤害都是秒杀级，血量为以后伤害分级预留）
// 自爆怪：冲墙自爆一次性大伤害，被提前点死不爆——"必须优先点杀"的压迫感来源
constexpr float BOMBER_SPEED_MUL      = 1.4f;   // 速度倍率（快，反应窗口短）
constexpr float BOMBER_BODY_SCALE     = 0.9f;   // 体型倍率
constexpr int   BOMBER_DAMAGE         = 15;     // 自爆对城墙伤害（≈近战怪啃 3 秒）
constexpr float BOMBER_FX_RADIUS      = 60.0f;  // 自爆特效半径（纯表现，伤害只打城墙）
// 飞行怪：游击手——进场后在远区空域持续游荡（航点随机+正弦摆动出曲线），
// 攻击严格按间隔计时，移动不打断计时——频率稳定，走位飘忽
constexpr float FLYER_SPEED_MUL        = 1.2f;   // 速度倍率（飞得快）
constexpr float FLYER_BODY_SCALE       = 0.85f;  // 体型倍率
constexpr float FLYER_STOP_MIN         = 0.25f;  // 进场深度下限（战场纵深比例，走到这就开始游荡）
constexpr float FLYER_STOP_MAX         = 0.62f;  // 进场深度上限
constexpr float FLYER_ATTACK_INTERVAL  = 2.2f;   // 攻击间隔（秒，钉死不变，保证频率稳定）
constexpr int   FLYER_ATTACK_DAMAGE    = 3;      // 每发弹丸伤害
constexpr float FLYER_SWAY_AMP         = 55.0f;  // 游荡正弦摆动幅度（像素/秒，越大轨迹越飘）
constexpr float FLYER_SWAY_FREQ        = 4.0f;   // 摆动频率（弧度/秒）
constexpr float FLYER_WINDUP           = 0.5f;   // 攻击前摇（秒）：悬停蓄力，看得见的前摇提示
// 治疗怪：跟在怪群后方，周期性给周围友军回血——逼玩家越过前排点杀后排
constexpr float HEALER_SPEED_MUL       = 0.7f;
constexpr float HEALER_STOP_RATIO      = 0.45f;  // 停在场中（比远程怪更靠后）
constexpr float HEALER_INTERVAL        = 2.5f;   // 治疗脉冲间隔（秒）
constexpr float HEALER_RADIUS          = 110.0f; // 治疗半径
constexpr int   HEALER_AMOUNT          = 1;      // 每次回血量
constexpr int   HEALER_HP              = 2;
// 分裂怪：死后分裂成两只小快怪——克单点箭，怕 AOE（火球一锅端）
constexpr float SPLITTER_SPEED_MUL     = 1.0f;
constexpr float SPLITTER_BODY_SCALE    = 1.15f;
constexpr int   SPLITTER_HP            = 2;
constexpr float SPLIT_CHILD_OFFSET     = 28.0f;  // 子体出生横向偏移
// 狂暴怪：血越少跑得越快——要么集火秒掉，要么留到最后处理
constexpr float RAGER_SPEED_MUL        = 1.1f;
constexpr int   RAGER_HP               = 3;
constexpr float RAGER_SPEED_BOOST      = 1.2f;   // 空血时额外加速比例（+120%）
// 幽灵怪：周期性隐身，隐身时箭打不中（AOE 技能照吃）——逼玩家掐时机或留蓝
constexpr float GHOST_SHOW_TIME        = 2.5f;   // 显形时长（秒）
constexpr float GHOST_HIDE_TIME        = 1.8f;   // 隐身时长（秒）
// 召唤怪：站远区周期性召唤小怪——不集火就越打越多
constexpr float SUMMONER_SPEED_MUL     = 0.6f;
constexpr float SUMMONER_STOP_RATIO    = 0.55f;
constexpr float SUMMONER_INTERVAL      = 4.0f;   // 召唤间隔（秒）
constexpr int   SUMMONER_HP            = 3;
// 护符怪：一次性全能护盾（挡任何攻击一次）+ 高速——必须优先处理的冲锋手
constexpr float AEGIS_SPEED_MUL        = 1.5f;
constexpr float AEGIS_BODY_SCALE       = 0.9f;
// Boss：自由飞（游荡复用飞行怪机制）、厚血、前摇后扇形三连发
constexpr float BOSS_SPEED_MUL         = 0.55f;
constexpr float BOSS_BODY_SCALE        = 2.4f;
constexpr int   BOSS_HP                = 30;
constexpr float BOSS_STOP_RATIO        = 0.40f;  // 进场深度（战场纵深比例）
constexpr float BOSS_ATTACK_INTERVAL   = 3.0f;
constexpr float BOSS_WINDUP            = 1.2f;   // 大招前摇（秒，长前摇=玩家的输出窗口）
constexpr int   BOSS_BOLT_DAMAGE       = 4;      // 每发弹丸伤害
constexpr int   BOSS_BOLT_COUNT        = 3;      // 扇形弹丸数
constexpr float BOSS_BOLT_SPREAD       = 90.0f;  // 扇形横向间距（像素）
// ---- 敌方弹丸 ----
constexpr int   ENEMY_BOLT_POOL_SIZE  = 16;     // 同时在屏弹丸上限
constexpr float ENEMY_BOLT_SPEED      = 350.0f; // 弹丸速度（像素/秒，比箭慢，看得见飞）

// ---- 技能系统（M1）----
// 数值表驱动：调平衡只动这里。①②③ 对应手感调优面板滑杆
constexpr float MANA_MAX         = 100.0f;
float MANA_REGEN                 = 6.0f;    // ① 回蓝速度（点/秒，ImGui 可调）
// 各技能 [冷却秒, 耗蓝]——顺序与 SkillType 枚举一致（火球/冰冻/箭雨/闪电）
constexpr float SKILL_CD[4]      = {8.0f, 10.0f, 15.0f, 12.0f};
constexpr float SKILL_MANA[4]    = {30.0f, 25.0f, 40.0f, 35.0f};
// 火球
float FIREBALL_RADIUS            = 90.0f;   // ② 爆炸半径（ImGui 可调）
constexpr float FIREBALL_FLIGHT  = 0.6f;    // 飞行时间（秒）
constexpr float FIREBALL_ARC_H   = 120.0f;  // 抛物线拱高（像素）
// 冰冻
constexpr float FREEZE_RADIUS    = 110.0f;  // 冰冻半径
constexpr float FREEZE_DURATION  = 3.0f;    // 减速持续（秒）
float FREEZE_SLOW                = 0.35f;   // ③ 减速后移速比例（ImGui 可调）
// 箭雨
constexpr float RAIN_RADIUS      = 100.0f;  // 箭雨覆盖半径
constexpr int   RAIN_STRIKES     = 10;      // 落箭数量
constexpr float RAIN_INTERVAL    = 0.15f;   // 落箭间隔（秒）
constexpr float RAIN_HIT_R       = 26.0f;   // 单支箭点杀半径
// 闪电（原作电系 1 阶）：主目标点杀 + 范围概率麻痹
constexpr float LIGHTNING_RADIUS   = 80.0f;   // 麻痹生效半径
constexpr float LIGHTNING_DIRECT_R = 30.0f;   // 直击半径：雷只劈中心点，劈中才有主目标击杀
float LIGHTNING_STUN_CHANCE        = 0.8f;    // ④ 麻痹概率（ImGui 可调）
constexpr float LIGHTNING_STUN_TIME= 2.0f;    // 麻痹时长（秒）
constexpr float STUN_SLOW          = 0.15f;   // 麻痹时移速比例：大幅下降但仍能蠕动
// 池容量与层级
constexpr int   FIREBALL_POOL_SIZE = 4;
constexpr int   FX_POOL_SIZE       = 12;
constexpr float SKILL_BTN_SIZE     = 80.0f;
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
#if CG_DEBUG_TOOLS
    initDebugUI();
#endif
    // 技能栏同理：必须先于 buildBattlefield 创建（布局重排在重建战场时统一做）
    initSkillBar();

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
    initFireballPool();  // M1 技能：火球弹体池
    initFxPool();        // M1 技能：特效池（爆炸环/冰冻区/箭雨痕）
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
    // 5b. （技能栏已在第 2 步创建——buildBattlefield 会统一重排它的位置）

    /////////////////////////////
    // 5. 输入监听：触摸射箭，键盘桌面调试用
    _touchListener                 = EventListenerTouchAllAtOnce::create();
    _touchListener->onTouchesBegan = AX_CALLBACK_2(MainScene::onTouchesBegan, this);
    _touchListener->onTouchesMoved = AX_CALLBACK_2(MainScene::onTouchesMoved, this);
    _touchListener->onTouchesEnded = AX_CALLBACK_2(MainScene::onTouchesEnded, this);
    _eventDispatcher->addEventListenerWithSceneGraphPriority(_touchListener, this);

#if CG_DEBUG_TOOLS
    // 键盘监听只在开发版注册：桌面调试键（1/2 切视角、R 重开、Q/E 跳波、T 调参）
    _keyboardListener                = EventListenerKeyboard::create();
    _keyboardListener->onKeyPressed  = AX_CALLBACK_2(MainScene::onKeyPressed, this);
    _keyboardListener->onKeyReleased = AX_CALLBACK_2(MainScene::onKeyReleased, this);
    _eventDispatcher->addEventListenerWithFixedPriority(_keyboardListener, 11);
#endif

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
    // 技能侧：飞行中火球/特效/待落箭雨一并回收（蓝量、冷却保留）
    for (auto& f : _fireballPool)
    {
        f.active = false;
        f.node->setVisible(false);
    }
    for (auto& fx : _fxPool)
    {
        fx.active = false;
        fx.node->setVisible(false);
    }
    _rainQueue.clear();
    _aimingSkill = -1;
    _aimDragging = false;
    if (_aimIndicator)
    {
        _aimIndicator->setVisible(false);
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

#if CG_DEBUG_TOOLS
    layoutDebugUI(origin, top);  // 调试按钮重排（正式包无此函数）
#endif
    layoutSkillBar(origin, right);  // 技能栏重排（正式玩法 UI）
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

    AXLOGD("战场已重建：{}，可见尺寸=({:.0f}, {:.0f})",
           layout == BattleLayout::Oblique25D ? "Oblique25D" : "SideView",
           visibleSize.width, visibleSize.height);
}

#if CG_DEBUG_TOOLS
// ---------------------------------------------------------------------------
// 调试层：左上角常驻"切换视角"按钮。
// 微信模拟器里鼠标无法模拟双指、也没有键盘，调试入口必须是触屏可点的。
// 按钮不挂在战场容器里（战场重建时不会被一起销毁），位置由 layoutDebugUI 更新。
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

    // ⚙ 手感调优面板（ImGui）：第三排最右，图标是三条滑杆
    _debugTuneBtn = Node::create();
    auto bg5 = DrawNode::create();
    bg5->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.0f, 0.0f, 0.0f, 0.45f));
    bg5->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.7f));
    for (int i = 0; i < 3; ++i)
    {
        const float y = s * (0.30f + i * 0.20f);
        bg5->drawSolidRect(Vec2(s * 0.22f, y - 2), Vec2(s * 0.78f, y + 2), w);  // 横轨
        const float knobX = s * (i % 2 == 0 ? 0.36f : 0.58f);                   // 滑块错开
        bg5->drawSolidCircle(Vec2(knobX, y), 5.5f, 0.0f, 10, w);
    }
    _debugTuneBtn->addChild(bg5);
    addChild(_debugTuneBtn, 10);
}

// 调试按钮跟随布局定位：左上角留边区（避开右上角的微信胶囊菜单）。
// 三排：⇄ 切视角 / ↻ 重开 / ◀ ▶ ⚙（跳波 + 调参）
void MainScene::layoutDebugUI(const Vec2& origin, float top)
{
    const float s = DEBUG_BTN_SIZE;
    if (_debugBtn)
    {
        Vec2 p(origin.x + 16.0f, top - 16.0f - s);
        _debugBtn->setPosition(p);
        _debugBtnRect = Rect(p.x, p.y, s, s);
    }
    if (_debugRestartBtn)
    {
        Vec2 p(origin.x + 16.0f, top - 16.0f - s - 12.0f - s);
        _debugRestartBtn->setPosition(p);
        _debugRestartBtnRect = Rect(p.x, p.y, s, s);
    }
    const float y3 = top - 16.0f - (s + 12.0f) * 2 - s;  // 第三排
    if (_debugPrevBtn && _debugNextBtn)
    {
        _debugPrevBtn->setPosition(Vec2(origin.x + 16.0f, y3));
        _debugPrevBtnRect = Rect(origin.x + 16.0f, y3, s, s);
        _debugNextBtn->setPosition(Vec2(origin.x + 16.0f + s + 12.0f, y3));
        _debugNextBtnRect = Rect(origin.x + 16.0f + s + 12.0f, y3, s, s);
    }
    if (_debugTuneBtn)
    {
        const float x = origin.x + 16.0f + (s + 12.0f) * 2;
        _debugTuneBtn->setPosition(Vec2(x, y3));
        _debugTuneBtnRect = Rect(x, y3, s, s);
    }
}

// ---------------------------------------------------------------------------
// 手感调优面板（ImGui 扩展）：滑杆实时改参数，不用改代码重编译。
// 原理：ImGui 是"立即模式"GUI——每帧重新描述一遍界面（没有持久的控件对象），
// 通过 addRenderLoop 注册一个每帧回调，我们在回调里调用 ImGui:: 系列函数画窗口。
// 面板显隐 = 添加/移除这个渲染循环，隐藏时零开销。
// ---------------------------------------------------------------------------
void MainScene::onEnter()
{
    Scene::onEnter();
    // 中文字体在 toggleTuningPanel 首次打开时加载（fonts/NotoSansSC-Regular.ttf）
}

void MainScene::onExit()
{
    if (_tuningVisible)  // 场景退出时归还渲染循环，防止悬垂回调
    {
        ImGuiPresenter::getInstance()->removeRenderLoop("#tune");
        _tuningVisible = false;
    }
    Scene::onExit();
}

void MainScene::toggleTuningPanel()
{
    _tuningVisible = !_tuningVisible;
    if (_tuningVisible)
    {
        // 首次打开时加载中文字体（Noto Sans SC，OFL 协议可随意打包；
        // ImGui 1.92 动态图集按需加载字形，不用操心字形范围）
        static bool s_fontLoaded = false;
        if (!s_fontLoaded)
        {
            ImGuiPresenter::getInstance()->addFont("fonts/NotoSansSC-Regular.ttf", 20.0f);
            s_fontLoaded = true;
        }
        ImGuiPresenter::getInstance()->addRenderLoop("#tune", AX_CALLBACK_0(MainScene::onImGuiDraw, this),
                                                     this);
    }
    else
    {
        ImGuiPresenter::getInstance()->removeRenderLoop("#tune");
    }
}

void MainScene::onImGuiDraw()
{
    ImGui::SetNextWindowPos(ImVec2(130, 60), ImGuiCond_FirstUseEver);
    // 不加 AlwaysAutoResize：ImGui 窗口默认可拖边缘/右下角调大小，给个初始尺寸即可
    ImGui::SetNextWindowSize(ImVec2(420, 380), ImGuiCond_FirstUseEver);
    ImGui::Begin("手感调优");
    ImGui::Text("帧率: %.1f", 1.0f / ImGui::GetIO().DeltaTime);
    ImGui::SeparatorText("射箭");
    ImGui::SliderFloat("箭矢速度", &ARROW_SPEED, 300.0f, 2000.0f, "%.0f 像素/秒");
    ImGui::SliderFloat("射速上限", &FIRE_INTERVAL, 0.05f, 0.50f, "%.2f 秒/发");
    ImGui::SeparatorText("怪物");
    ImGui::SliderFloat("怪物速度", &MONSTER_SPEED, 20.0f, 200.0f, "%.0f 像素/秒");
    ImGui::SliderFloat("远程停火位", &RANGED_STOP_RATIO, 0.40f, 0.85f, "%.2f（纵深比）");
    // 进阶参数：玩法实验性质，默认收起——抛物线弹道会显著改变瞄准手感，
    // 属于"丰富玩法"的候选（比如后续做成可解锁的箭塔改造）
    if (ImGui::CollapsingHeader("进阶参数（玩法实验）"))
    {
        ImGui::SliderFloat("抛物线重力", &ARROW_GRAVITY, 0.0f, 900.0f, "%.0f（0=直线弹道）");
    }
    ImGui::SeparatorText("技能（M1）");
    ImGui::SliderFloat("回蓝速度", &MANA_REGEN, 0.0f, 20.0f, "%.1f 点/秒");
    ImGui::SliderFloat("火球半径", &FIREBALL_RADIUS, 40.0f, 200.0f, "%.0f 像素");
    ImGui::SliderFloat("冰冻强度", &FREEZE_SLOW, 0.05f, 1.0f, "%.2f（移速比例）");
    ImGui::SliderFloat("闪电麻痹概率", &LIGHTNING_STUN_CHANCE, 0.0f, 1.0f, "%.2f");
    ImGui::SeparatorText("战场状态");
    ImGui::Text("城墙血量: %d  击杀: %d  蓝量: %.0f", _wallHP, _killCount, _mana);
    ImGui::End();
}
#endif  // CG_DEBUG_TOOLS（调试层整块结束）

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
    // 射速上限：冷却没转好就忽略这次点击（手感调优清单第 4 项）
    if (_fireCooldown > 0.0f)
    {
        return;
    }
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
        _fireCooldown = FIRE_INTERVAL;  // 射出才进冷却
        return;
    }
    AXLOGD("箭矢池已满（{} 支在飞），本次射击丢弃", ARROW_POOL_SIZE);
}

// ---------------------------------------------------------------------------
// 击杀统一入口：箭矢命中和技能 AOE 都走这里。进 Dead 状态而非直接回收——
// 回池动作由 updateMonsters 统一做（遍历中不改池状态，更安全）
// ---------------------------------------------------------------------------
void MainScene::killMonster(Monster& m)
{
    if (m.state == MonsterState::Dead)
    {
        return;
    }
    m.state = MonsterState::Dead;
    _killCount++;
    // 分裂怪：死亡时在原地裂出两只小快怪。注意这对不同伤害的克制关系——
    // 单点箭：杀一只变两只（分裂怪的血条优势）；AOE 技能：子体出生在
    // 爆炸半径内，同一帧的后续判定直接带走（AOE 一锅端）——一石二鸟的设计
    if (m.type == MonsterType::Splitter)
    {
        const Vec2 c = m.node->getPosition();
        spawnMinion(MonsterType::Fast, c + Vec2(-SPLIT_CHILD_OFFSET, 0));
        spawnMinion(MonsterType::Fast, c + Vec2(SPLIT_CHILD_OFFSET, 0));
    }
}

// 扣血统一入口：箭、火球、闪电都走这里。扣血与击杀分离，加新怪时不用再动伤害逻辑
void MainScene::damageMonster(Monster& m, int dmg)
{
    if (m.state == MonsterState::Dead)
    {
        return;
    }
    // 护符怪的一次性全能护盾：挡【任何】伤害（箭和技能都挡）。挡下后重画皮，
    // 金环消失——"盾破了"必须有即时视觉反馈，玩家才知道下一击能杀
    if (m.shieldCharges > 0)
    {
        m.shieldCharges--;
        spawnFx(0, m.node->getPosition() + Vec2(0, m.bodyR), m.bodyR + 10.0f, 0.2f);  // 碎盾闪光
        applyMonsterSkin(m);
        return;
    }
    m.hp -= dmg;
    if (m.hp <= 0)
    {
        killMonster(m);
    }
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
            // 幽灵怪隐身时箭直接穿过（打不中），但技能 AOE 照吃——
            // 和护盾怪正好反过来：一个逼你用技能，一个逼你掐时机
            if (m.type == MonsterType::Ghost && m.ghostHidden)
            {
                continue;
            }
            // 命中判定：箭尖点 vs 怪身圆。体型随类型缩放，所以用个体半径
            const float hitR    = m.bodyR + 6.0f;  // 身体半径 + 箭尖余量
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            if ((pos - bodyCenter).lengthSquared() < hitR * hitR)
            {
                // 护盾怪：箭矢免疫（抗性系统的萌芽——现在是最简 if，
                // 将来伤害带类型字段、怪带抗性表，就不用在这里写死类型判断了）。
                // 箭被弹开：给个小闪光反馈，让玩家知道"命中了但没掉血，得换技能"
                if (m.type == MonsterType::Shield)
                {
                    spawnFx(0, bodyCenter, m.bodyR + 8.0f, 0.18f);  // 小号爆炸环当弹开闪光
                }
                else
                {
                    damageMonster(m, 1);       // 箭矢 1 点伤害
                }
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

// 飞行怪游荡航点：在"远区空域"里随机取点。两种布局各自的空域定义不同——
// 2.5D 是城墙与出怪线之间的整块矩形，侧视版是地面上方的一条空气带
ax::Vec2 MainScene::pickFlyerWaypoint() const
{
    auto vs = _director->getVisibleSize();
    auto o  = _director->getVisibleOrigin();
    const float r1 = static_cast<float>(rand()) / RAND_MAX;
    const float r2 = static_cast<float>(rand()) / RAND_MAX;
    if (_battleLayout == BattleLayout::Oblique25D)
    {
        const float y0 = o.y + vs.height * (WALL_RATIO + 0.10f);    // 不贴城墙
        const float y1 = o.y + vs.height * (SPAWN_RATIO - 0.06f);   // 不出出怪线
        return Vec2(o.x + 50.0f + r1 * (vs.width - 100.0f), y0 + r2 * (y1 - y0));
    }
    // SideView：房子右侧到出怪口之间，地面上方 40~200px 的空气带
    const float x0 = o.x + vs.width * SIDE_HOUSE_X_RATIO + 90.0f;
    const float x1 = o.x + vs.width * SPAWN_RATIO - 40.0f;
    const float yGround = o.y + vs.height * SIDE_GROUND_RATIO;
    return Vec2(x0 + r1 * (x1 - x0), yGround + 40.0f + r2 * 160.0f);
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
        // 冰冻标记：蓝色圆环，画在怪身体上方，slowTimer>0 时才可见
        // （DrawNode 画完颜色就固定了，没法整体变色，减速外观用叠加节点实现）
        m.slowMark = DrawNode::create();
        m.slowMark->drawCircle(Vec2(0, MONSTER_BODY_R), MONSTER_BODY_R + 5.0f, 0.0f, 20, false,
                               Color4F(0.35f, 0.75f, 1.0f, 0.9f), 3.0f);
        m.slowMark->setVisible(false);
        m.node->addChild(m.slowMark, 1);
        // 麻痹标记：黄色锯齿电纹（两道折线），闪电技能的 stun 视觉
        m.stunMark = DrawNode::create();
        m.stunMark->drawLine(Vec2(-6, MONSTER_BODY_R * 2.4f), Vec2(0, MONSTER_BODY_R * 2.0f),
                             Color4F(1.0f, 0.9f, 0.2f, 1.0f), 3.0f);
        m.stunMark->drawLine(Vec2(0, MONSTER_BODY_R * 2.0f), Vec2(-4, MONSTER_BODY_R * 1.6f),
                             Color4F(1.0f, 0.9f, 0.2f, 1.0f), 3.0f);
        m.stunMark->drawLine(Vec2(6, MONSTER_BODY_R * 2.4f), Vec2(2, MONSTER_BODY_R * 2.0f),
                             Color4F(1.0f, 0.9f, 0.2f, 1.0f), 3.0f);
        m.stunMark->setVisible(false);
        m.node->addChild(m.stunMark, 1);
        // 蓄力标记：橙色光环（飞行怪攻击前摇的视觉提示——"它要出手了"）
        m.windupMark = DrawNode::create();
        m.windupMark->drawCircle(Vec2(0, MONSTER_BODY_R), MONSTER_BODY_R + 7.0f, 0.0f, 24, false,
                                 Color4F(1.0f, 0.65f, 0.15f, 0.95f), 3.5f);
        m.windupMark->setVisible(false);
        m.node->addChild(m.windupMark, 1);
        _monsterPool.push_back(m);
    }
}

// 按类型画皮。读图识怪原则：每种怪都有专属的"零件"（盾环/引信/翅膀/十字…），
// 玩家不用记颜色，看零件就知道威胁类型和应对方式
void MainScene::applyMonsterSkin(Monster& m)
{
    m.node->clear();  // DrawNode 可以清空重画，池化复用的关键（注意：只清自身，子节点标记不受影响）
    const float r = m.bodyR;
    const float k = r / MONSTER_BODY_R;  // 五官随体型等比缩放
    Color4F body;
    switch (m.type)
    {
    case MonsterType::Fast:     body = Color4F(0.90f, 0.50f, 0.15f, 1.0f); break;  // 橙
    case MonsterType::Ranged:   body = Color4F(0.62f, 0.30f, 0.72f, 1.0f); break;  // 紫
    case MonsterType::Shield:   body = Color4F(0.28f, 0.52f, 0.62f, 1.0f); break;  // 青钢
    case MonsterType::Bomber:   body = Color4F(0.22f, 0.20f, 0.20f, 1.0f); break;  // 黑
    case MonsterType::Flyer:    body = Color4F(0.88f, 0.88f, 0.82f, 1.0f); break;  // 浅白
    case MonsterType::Healer:   body = Color4F(0.42f, 0.78f, 0.45f, 1.0f); break;  // 浅绿
    case MonsterType::Splitter: body = Color4F(0.85f, 0.35f, 0.60f, 1.0f); break;  // 品红
    case MonsterType::Rager:    body = Color4F(0.55f, 0.10f, 0.08f, 1.0f); break;  // 深红
    case MonsterType::Ghost:    body = Color4F(0.80f, 0.88f, 0.95f, 1.0f); break;  // 幽白
    case MonsterType::Summoner: body = Color4F(0.35f, 0.18f, 0.50f, 1.0f); break;  // 深紫
    case MonsterType::Aegis:    body = Color4F(0.95f, 0.82f, 0.28f, 1.0f); break;  // 亮黄
    case MonsterType::Boss:     body = Color4F(0.45f, 0.15f, 0.35f, 1.0f); break;  // 暗紫红
    default:                    body = Color4F(0.72f, 0.22f, 0.18f, 1.0f); break;  // 红
    }
    const Color4F horn(0.50f, 0.12f, 0.10f, 1.0f);
    m.node->drawSolidCircle(Vec2(0, r), r, 0.0f, 20, body);  // 身体
    if (m.type != MonsterType::Ghost)  // 幽灵无角：轮廓圆润=非实体
    {
        m.node->drawSolidTriangle(Vec2(-r * 0.7f, r * 1.7f), Vec2(-r * 0.2f, r * 1.8f),
                                  Vec2(-r * 0.5f, r * 2.3f), horn, horn);  // 左角
        m.node->drawSolidTriangle(Vec2(r * 0.7f, r * 1.7f), Vec2(r * 0.2f, r * 1.8f),
                                  Vec2(r * 0.5f, r * 2.3f), horn, horn);   // 右角
    }
    m.node->drawSolidCircle(Vec2(-5 * k, r + 3 * k), 2.5f * k, 0.0f, 8, Color4F::WHITE);  // 左眼
    m.node->drawSolidCircle(Vec2(5 * k, r + 3 * k), 2.5f * k, 0.0f, 8, Color4F::WHITE);   // 右眼
    // ---- 类型特征件 ----
    if (m.type == MonsterType::Shield)
    {
        // 盾环：头顶一道亮青色弧线护盾（免箭的视觉承诺）
        m.node->drawCircle(Vec2(0, r), r + 4.0f, 0.0f, 24, false, Color4F(0.55f, 0.90f, 1.0f, 0.95f), 3.0f);
    }
    else if (m.type == MonsterType::Bomber)
    {
        // 引信：头顶一撮橙色火花 + 腹部橙心（"我会爆"的危险信号）
        m.node->drawSolidCircle(Vec2(0, r), r * 0.45f, 0.0f, 12, Color4F(1.0f, 0.55f, 0.10f, 1.0f));
        m.node->drawLine(Vec2(0, r * 1.8f), Vec2(3 * k, r * 2.5f), Color4F(0.6f, 0.5f, 0.4f, 1.0f), 2.0f);
        m.node->drawSolidCircle(Vec2(3 * k, r * 2.6f), 2.2f * k, 0.0f, 8, Color4F(1.0f, 0.75f, 0.15f, 1.0f));
    }
    else if (m.type == MonsterType::Flyer)
    {
        // 翅膀：身体两侧各一片三角形羽翼（会飞的游击手，看一眼就知道走位不老实）
        const Color4F wing(0.70f, 0.78f, 0.85f, 1.0f);
        m.node->drawSolidTriangle(Vec2(-r * 0.8f, r * 1.2f), Vec2(-r * 2.0f, r * 1.9f),
                                  Vec2(-r * 0.8f, r * 0.6f), wing, wing);  // 左翼
        m.node->drawSolidTriangle(Vec2(r * 0.8f, r * 1.2f), Vec2(r * 2.0f, r * 1.9f),
                                  Vec2(r * 0.8f, r * 0.6f), wing, wing);   // 右翼
    }
    else if (m.type == MonsterType::Healer)
    {
        // 十字：头顶白色治疗十字（"我在给队友加血"）
        const float cy = r * 2.4f;
        m.node->drawSolidRect(Vec2(-1.8f * k, cy - 6 * k), Vec2(1.8f * k, cy + 6 * k), Color4F::WHITE);
        m.node->drawSolidRect(Vec2(-6 * k, cy - 1.8f * k), Vec2(6 * k, cy + 1.8f * k), Color4F::WHITE);
    }
    else if (m.type == MonsterType::Splitter)
    {
        // 裂缝：身体中间一道白色竖缝（"我会一分为二"）
        m.node->drawLine(Vec2(0, r * 0.3f), Vec2(0, r * 1.8f), Color4F(1, 1, 1, 0.85f), 2.5f);
    }
    else if (m.type == MonsterType::Rager)
    {
        // 怒眉：两道倒八斜线（"别把我打残"）
        const Color4F brow(0.15f, 0.05f, 0.05f, 1.0f);
        m.node->drawLine(Vec2(-8 * k, r + 8 * k), Vec2(-2 * k, r + 5 * k), brow, 2.5f);
        m.node->drawLine(Vec2(8 * k, r + 8 * k), Vec2(2 * k, r + 5 * k), brow, 2.5f);
    }
    else if (m.type == MonsterType::Summoner)
    {
        // 法杖：身侧竖杖 + 顶端悬浮亮点（"我在造兵"）
        m.node->drawLine(Vec2(r * 1.2f, 0), Vec2(r * 1.2f, r * 2.6f), Color4F(0.45f, 0.30f, 0.20f, 1.0f), 3.0f);
        m.node->drawSolidCircle(Vec2(r * 1.2f, r * 2.8f), 2.5f * k, 0.0f, 8, Color4F(0.85f, 0.55f, 1.0f, 1.0f));
    }
    else if (m.type == MonsterType::Aegis && m.shieldCharges > 0)
    {
        // 护符泡：金色厚环（破盾后 applyMonsterSkin 重画，环就没了）
        m.node->drawCircle(Vec2(0, r), r + 5.0f, 0.0f, 26, false, Color4F(1.0f, 0.85f, 0.25f, 0.95f), 4.0f);
    }
    else if (m.type == MonsterType::Boss)
    {
        // 王冠：头顶三个尖三角（全场最大的怪 + 唯一的冠）
        const Color4F crown(1.0f, 0.80f, 0.20f, 1.0f);
        for (int i = -1; i <= 1; ++i)
        {
            const float cx = i * r * 0.55f;
            m.node->drawSolidTriangle(Vec2(cx - r * 0.22f, r * 1.9f), Vec2(cx + r * 0.22f, r * 1.9f),
                                      Vec2(cx, r * 2.5f), crown, crown);
        }
    }
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

// 从池里找一个空槽位。对象池的核心操作：分配 = 翻标志位，不做内存分配
// （返回类型要写全 MainScene::Monster——类外定义成员函数时，
//   返回类型的名字查找发生在类的作用域之外，这是 C++ 的经典坑）
MainScene::Monster* MainScene::allocMonster()
{
    for (auto& m : _monsterPool)
    {
        if (!m.active)
        {
            return &m;
        }
    }
    return nullptr;
}

// 战中生怪：分裂怪的子体、召唤怪的召唤物。和波次出怪的唯一区别是
// 出生点由调用方指定（父怪死亡位置/召唤者身边），目标点直指城墙
void MainScene::spawnMinion(MonsterType type, const Vec2& pos)
{
    Monster* m = allocMonster();
    if (!m)
    {
        return;  // 池满就生不出来——池容量天然就是"召唤通胀"的刹车
    }
    m->lane = laneCount() > 1 ? rand() % laneCount() : 0;
    setupMonster(*m, type, pos, laneWallPos(m->lane));
}

// 槽位初始化：状态复位 + 类型数值表 + 画皮。spawnMonster（波次出怪）和
// spawnMinion（战中生怪）共用这一套，差异只在"出生点/目标点怎么算"
void MainScene::setupMonster(Monster& m, MonsterType type, const Vec2& spawnPos, const Vec2& targetPos)
{
    m.active        = true;
    m.state         = MonsterState::Walk;
    m.attackTimer   = 0.0f;
    m.slowTimer     = 0.0f;   // 技能状态复位（池化复用，上一局的减速/麻痹不能带过来）
    m.stunTimer     = 0.0f;
    m.windupTimer   = 0.0f;   // 攻击前摇复位（飞行怪/Boss）
    m.hp            = 1;      // 基准 1 血；下方类型表里特殊怪会覆盖
    m.shieldCharges = 0;      // 一次性护盾复位（护符怪在类型表里给 1 层）
    m.ghostHidden   = false;  // 幽灵状态复位
    m.ghostTimer    = GHOST_SHOW_TIME;
    m.type          = type;
    // 类型数值表：速度/体型/血量/攻击方式都由类型决定。
    // 这就是"数值表驱动"的裸实现——加新怪只动这张表和画皮，驱动逻辑零改动
    const float r2 = static_cast<float>(rand()) / RAND_MAX;  // 个体速度随机源
    float speedMul  = 1.0f;
    float bodyScale = 1.0f;
    switch (type)
    {
    case MonsterType::Fast:     speedMul = FAST_SPEED_MUL;     bodyScale = FAST_BODY_SCALE;     break;
    case MonsterType::Ranged:   speedMul = RANGED_SPEED_MUL;                                    break;
    case MonsterType::Shield:   speedMul = SHIELD_SPEED_MUL;   bodyScale = SHIELD_BODY_SCALE;   m.hp = SHIELD_HP;   break;
    case MonsterType::Bomber:   speedMul = BOMBER_SPEED_MUL;   bodyScale = BOMBER_BODY_SCALE;   break;
    case MonsterType::Flyer:    speedMul = FLYER_SPEED_MUL;    bodyScale = FLYER_BODY_SCALE;    break;
    case MonsterType::Healer:   speedMul = HEALER_SPEED_MUL;                                    m.hp = HEALER_HP;   break;
    case MonsterType::Splitter: speedMul = SPLITTER_SPEED_MUL; bodyScale = SPLITTER_BODY_SCALE; m.hp = SPLITTER_HP; break;
    case MonsterType::Rager:    speedMul = RAGER_SPEED_MUL;                                     m.hp = RAGER_HP;    break;
    case MonsterType::Ghost:                                                                                      break;
    case MonsterType::Summoner: speedMul = SUMMONER_SPEED_MUL;                                  m.hp = SUMMONER_HP; break;
    case MonsterType::Aegis:    speedMul = AEGIS_SPEED_MUL;    bodyScale = AEGIS_BODY_SCALE;    m.shieldCharges = 1; break;
    case MonsterType::Boss:     speedMul = BOSS_SPEED_MUL;     bodyScale = BOSS_BODY_SCALE;     m.hp = BOSS_HP;     break;
    default:                                                                                    break;
    }
    m.maxHp = m.hp;  // 血量上限跟着初始值走（狂暴怪按 hp/maxHp 算狂暴加速）
    m.speed = MONSTER_SPEED * speedMul * (0.85f + r2 * 0.3f);  // 0.85~1.15 倍
    m.bodyR = MONSTER_BODY_R * bodyScale;
    // 开火距离与攻击间隔：近战贴墙啃；远程/治疗/召唤按战场纵深比例停在各自的
    // 功能位（比例而非写死像素，横竖屏都成立）；飞行怪/Boss 进场后开始游荡
    const float fieldDepth = _director->getVisibleSize().height * (SPAWN_RATIO - WALL_RATIO);
    if (type == MonsterType::Flyer)
    {
        const float t = static_cast<float>(rand()) / RAND_MAX;
        m.attackRange    = fieldDepth * (FLYER_STOP_MIN + t * (FLYER_STOP_MAX - FLYER_STOP_MIN));
        m.attackInterval = FLYER_ATTACK_INTERVAL;
    }
    else if (type == MonsterType::Ranged)
    {
        m.attackRange    = fieldDepth * RANGED_STOP_RATIO;
        m.attackInterval = RANGED_ATTACK_INTERVAL;
    }
    else if (type == MonsterType::Healer)
    {
        m.attackRange    = fieldDepth * HEALER_STOP_RATIO;
        m.attackInterval = HEALER_INTERVAL;   // attackTimer 复用为治疗脉冲计时
    }
    else if (type == MonsterType::Summoner)
    {
        m.attackRange    = fieldDepth * SUMMONER_STOP_RATIO;
        m.attackInterval = SUMMONER_INTERVAL; // attackTimer 复用为召唤计时
    }
    else if (type == MonsterType::Boss)
    {
        m.attackRange    = fieldDepth * BOSS_STOP_RATIO;
        m.attackInterval = BOSS_ATTACK_INTERVAL;
    }
    else
    {
        m.attackRange    = 6.0f;
        m.attackInterval = 1.0f;
    }
    m.targetPos = targetPos;
    if (type == MonsterType::Flyer || type == MonsterType::Boss)
    {
        m.wanderTarget = pickFlyerWaypoint();  // 进场后的第一个游荡航点
        // 相位随机错开，避免所有飞行怪整齐划一地摆（集体同频摆动看着很假）
        m.wanderPhase  = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
    }
    m.node->setOpacity(255);  // 幽灵怪隐身降过透明度，槽位复用时要复位
    m.node->setPosition(spawnPos);
    applyMonsterSkin(m);
    m.node->setVisible(true);
}

void MainScene::spawnMonster(MonsterType type, int lane, const std::string& from)
{
    Monster* m = allocMonster();
    if (!m)
    {
        AXLOGD("怪物池已满（{} 只在场），本次出怪丢弃", MONSTER_POOL_SIZE);
        return;
    }
    m->lane = lane >= 0 ? lane % laneCount() : (laneCount() > 1 ? rand() % laneCount() : 0);
    // 怪群感关键：同车道内横向抖动 + 个体速度差异。
    // 抖动对出生点和终点施加同一份偏移，怪物沿"平移后的车道"走直线，
    // 不会在半途被拉回车道中心线；侧翼滑入的不抖，保持轨迹干净
    const float r1 = static_cast<float>(rand()) / RAND_MAX;  // 0~1
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
    setupMonster(*m, type, groupSpawnPos(from, m->lane) + jitter, laneWallPos(m->lane) + jitter);
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
        AXLOGD("waves.json 缺失或格式错误，启用内置兜底波次");
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
                    // type 字符串 → 枚举的登记处：加新怪在这里补一行就能被波次配置驱动
                    const char* t = g["type"].GetString();
                    if      (!strcmp(t, "fast"))     cfg.type = MonsterType::Fast;
                    else if (!strcmp(t, "ranged"))   cfg.type = MonsterType::Ranged;
                    else if (!strcmp(t, "shield"))   cfg.type = MonsterType::Shield;
                    else if (!strcmp(t, "bomber"))   cfg.type = MonsterType::Bomber;
                    else if (!strcmp(t, "flyer"))    cfg.type = MonsterType::Flyer;
                    else if (!strcmp(t, "healer"))   cfg.type = MonsterType::Healer;
                    else if (!strcmp(t, "splitter")) cfg.type = MonsterType::Splitter;
                    else if (!strcmp(t, "rager"))    cfg.type = MonsterType::Rager;
                    else if (!strcmp(t, "ghost"))    cfg.type = MonsterType::Ghost;
                    else if (!strcmp(t, "summoner")) cfg.type = MonsterType::Summoner;
                    else if (!strcmp(t, "aegis"))    cfg.type = MonsterType::Aegis;
                    else if (!strcmp(t, "boss"))     cfg.type = MonsterType::Boss;
                    else                             cfg.type = MonsterType::Normal;
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
    AXLOGD("波次配置已加载：{} 波", _waves.size());
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
    AXLOGD("波次 {}/{} 开始：{}", index + 1, _waves.size(), w.name);
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
                AXLOGD("全部波次已守住！击杀={}", _killCount);
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
        // 冰冻减速（技能系统）：计时衰减 + 蓝环标记显隐
        if (m.slowTimer > 0.0f)
        {
            m.slowTimer -= delta;
        }
        if (m.slowMark)
        {
            m.slowMark->setVisible(m.slowTimer > 0.0f);
        }
        // 闪电麻痹：计时衰减 + 黄电纹显隐；麻痹=移速大降（15%）+ 不能攻击
        if (m.stunTimer > 0.0f)
        {
            m.stunTimer -= delta;
        }
        if (m.stunMark)
        {
            m.stunMark->setVisible(m.stunTimer > 0.0f);
        }
        // 攻击前摇标记（飞行怪）：橙环亮起 = "它要出手了"
        if (m.windupMark)
        {
            m.windupMark->setVisible(m.windupTimer > 0.0f);
        }
        // 幽灵怪：显形/隐身周期轮转。隐身时半透明（看得见打不中，给玩家预判的线索），
        // 箭矢判定在 updateArrows 里跳过隐身个体
        if (m.type == MonsterType::Ghost && m.state != MonsterState::Dead)
        {
            m.ghostTimer -= delta;
            if (m.ghostTimer <= 0.0f)
            {
                m.ghostHidden = !m.ghostHidden;
                m.ghostTimer  = m.ghostHidden ? GHOST_HIDE_TIME : GHOST_SHOW_TIME;
                m.node->setOpacity(m.ghostHidden ? 70 : 255);  // 半透明而非全隐：保留威胁可读性
            }
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
                if (m.type == MonsterType::Bomber)
                {
                    // 自爆怪：不啃墙，到站直接爆——一次性大伤害 + 爆炸特效。
                    // 被箭/技能提前点死走不到这里（killMonster 直接进 Dead），
                    // 所以"提前点杀不爆"的策略空间天然成立
                    spawnFx(0, pos + Vec2(0, m.bodyR), BOMBER_FX_RADIUS, 0.35f);
                    damageWall(BOMBER_DAMAGE);
                    killMonster(m);
                    break;
                }
                m.state = MonsterState::Attack;
                // 近战/远程到站立刻来一发，不白等一拍；飞行怪不加速——
                // 它的攻击频率严格按间隔走（规格：移动不影响攻击节奏）
                m.attackTimer = m.type == MonsterType::Flyer ? 0.0f : m.attackInterval;
                break;
            }
            dir.normalize();
            // 狂暴怪：血越少跑得越快（线性加速，空血时 +120%）——
            // "要么集火秒掉，要么别碰它"的决策压力来源
            float spd = m.speed;
            if (m.type == MonsterType::Rager && m.maxHp > 0)
            {
                spd *= 1.0f + (1.0f - static_cast<float>(m.hp) / m.maxHp) * RAGER_SPEED_BOOST;
            }
            // 减速不打断状态机，只在移动这一步打折——麻痹（15%）优先于冰冻（35%）
            const float effSpeed = m.stunTimer > 0.0f ? spd * STUN_SLOW
                                 : m.slowTimer > 0.0f ? spd * FREEZE_SLOW : spd;
            m.node->setPosition(pos + dir * effSpeed * delta);
            break;
        }
        case MonsterState::Attack:
        {
            if (m.stunTimer > 0.0f)
            {
                break;  // 麻痹中：不能攻击也不能动（计时器也不走，解除后重新起手）
            }
            // 飞行怪：游荡 ↔ 蓄力停滞 ↔ 发射 的循环。移动与攻击互斥——
            // 要出手就得悬停（前摇看得见，玩家才有"快打断它"的反应窗口）
            if (m.type == MonsterType::Flyer)
            {
                if (m.windupTimer > 0.0f)
                {
                    // 前摇中：悬停不动，计时走完才发射
                    m.windupTimer -= delta;
                    if (m.windupTimer <= 0.0f)
                    {
                        fireEnemyBolt(m.node->getPosition() + Vec2(0, m.bodyR), laneWallPos(m.lane),
                                      FLYER_ATTACK_DAMAGE);
                        m.attackTimer = 0.0f;  // 从发射这一刻重新计下一发，频率恒定
                    }
                    break;
                }
                // 游荡：朝航点飞，叠加垂直正弦摆动让轨迹带弧度；到航点换下一个
                Vec2 pos = m.node->getPosition();
                Vec2 dir = m.wanderTarget - pos;
                if (dir.length() < 14.0f)
                {
                    m.wanderTarget = pickFlyerWaypoint();  // 到航点，换下一个
                    dir            = m.wanderTarget - pos;
                }
                if (dir.lengthSquared() > 1.0f)
                {
                    dir.normalize();
                    m.wanderPhase += FLYER_SWAY_FREQ * delta;
                    // 曲线轨迹：直线速度 + 垂直方向正弦摆动（steering 的雏形）
                    Vec2 sway(-dir.y * sinf(m.wanderPhase) * FLYER_SWAY_AMP,
                              dir.x * sinf(m.wanderPhase) * FLYER_SWAY_AMP);
                    const float effSpeed = m.slowTimer > 0.0f ? m.speed * FREEZE_SLOW : m.speed;
                    m.node->setPosition(pos + (dir * effSpeed + sway) * delta);
                }
                m.attackTimer += delta;
                if (m.attackTimer >= m.attackInterval)
                {
                    m.attackTimer = m.attackInterval;  // 钳住不溢出，前摇结束才清零
                    m.windupTimer = FLYER_WINDUP;      // 进入前摇：悬停蓄力
                }
                break;
            }
            // Boss：游荡（复用飞行怪的航点机制）+ 长前摇 + 扇形三连发。
            // 长前摇是玩家的集火窗口——看到橙环亮 1.2 秒，所有技能都该招呼上去
            if (m.type == MonsterType::Boss)
            {
                if (m.windupTimer > 0.0f)
                {
                    m.windupTimer -= delta;
                    if (m.windupTimer <= 0.0f)
                    {
                        // 扇形三连发：以正对城墙点为轴横向散开
                        const Vec2 from = m.node->getPosition() + Vec2(0, m.bodyR);
                        for (int i = 0; i < BOSS_BOLT_COUNT; ++i)
                        {
                            const float off = (i - (BOSS_BOLT_COUNT - 1) / 2.0f) * BOSS_BOLT_SPREAD;
                            fireEnemyBolt(from, laneWallPos(m.lane) + Vec2(off, 0), BOSS_BOLT_DAMAGE);
                        }
                        m.attackTimer = 0.0f;
                    }
                    break;
                }
                Vec2 pos = m.node->getPosition();
                Vec2 dir = m.wanderTarget - pos;
                if (dir.length() < 20.0f)
                {
                    m.wanderTarget = pickFlyerWaypoint();  // 到航点换下一个（全场自由飞）
                    dir            = m.wanderTarget - pos;
                }
                if (dir.lengthSquared() > 1.0f)
                {
                    dir.normalize();
                    m.wanderPhase += FLYER_SWAY_FREQ * 0.6f * delta;  // Boss 体重大，摆动更沉稳
                    Vec2 sway(-dir.y * sinf(m.wanderPhase) * FLYER_SWAY_AMP * 0.7f,
                              dir.x * sinf(m.wanderPhase) * FLYER_SWAY_AMP * 0.7f);
                    const float effSpeed = m.slowTimer > 0.0f ? m.speed * FREEZE_SLOW : m.speed;
                    m.node->setPosition(pos + (dir * effSpeed + sway) * delta);
                }
                m.attackTimer += delta;
                if (m.attackTimer >= m.attackInterval)
                {
                    m.attackTimer = m.attackInterval;
                    m.windupTimer = BOSS_WINDUP;  // 进入大招前摇
                }
                break;
            }
            // 治疗怪：不打城墙，攻击计时复用为治疗脉冲——给半径内友军回血。
            // 阵型克制的核心：它站在怪群后方，逼玩家越过前排点杀后排
            if (m.type == MonsterType::Healer)
            {
                m.attackTimer += delta;
                if (m.attackTimer >= m.attackInterval)
                {
                    m.attackTimer -= m.attackInterval;
                    const Vec2 c  = m.node->getPosition();
                    const float r2 = HEALER_RADIUS * HEALER_RADIUS;
                    for (auto& o : _monsterPool)
                    {
                        if (!o.active || o.state == MonsterState::Dead || &o == &m)
                        {
                            continue;
                        }
                        if ((o.node->getPosition() - c).lengthSquared() < r2 && o.hp < o.maxHp)
                        {
                            o.hp = std::min(o.maxHp, o.hp + HEALER_AMOUNT);
                        }
                    }
                    spawnFx(4, c + Vec2(0, m.bodyR), HEALER_RADIUS, 0.5f);  // 绿环治疗波
                }
                break;
            }
            // 召唤怪：不打城墙，攻击计时复用为召唤计时——在身边造普通怪。
            // 不集火就越打越多；池容量是召唤通胀的天然刹车
            if (m.type == MonsterType::Summoner)
            {
                m.attackTimer += delta;
                if (m.attackTimer >= m.attackInterval)
                {
                    m.attackTimer -= m.attackInterval;
                    const Vec2 at = m.node->getPosition() + Vec2(20.0f, 10.0f);
                    spawnMinion(MonsterType::Normal, at);
                    spawnFx(0, at, 30.0f, 0.25f);  // 召唤出现的小爆光
                }
                break;
            }
            m.attackTimer += delta;
            if (m.attackTimer >= m.attackInterval)
            {
                m.attackTimer -= m.attackInterval;
                if (m.type == MonsterType::Ranged)
                {
                    // 远程：发射弹丸，伤害在弹丸命中城墙时结算
                    fireEnemyBolt(m.node->getPosition() + Vec2(0, m.bodyR), laneWallPos(m.lane),
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

#if CG_DEBUG_TOOLS
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
    // 技能侧同步清场：火球/特效/待落箭雨
    for (auto& f : _fireballPool)
    {
        f.active = false;
        f.node->setVisible(false);
    }
    for (auto& fx : _fxPool)
    {
        fx.active = false;
        fx.node->setVisible(false);
    }
    _rainQueue.clear();
}

void MainScene::resetGame()
{
    clearField();
    _wallHP    = WALL_MAX_HP;
    _killCount = 0;
    _mana      = MANA_MAX;  // 技能系统：蓝量冷却一并复位
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        _cdRemaining[i] = 0.0f;
    }
    _aimingSkill = -1;
    _rainQueue.clear();
    _waveState = WaveState::Idle;  // 下一帧 updateWaves 自动 beginWave(0)
    if (_waveLabel)
    {
        _waveLabel->setString("");
    }
    AXLOGD("已通过调试面板重开（清场满血满蓝回第 1 波）");
}

// 调试跳波：跳到指定波（清场 + 城墙满血——跳波是为了能立刻开测，
// 城墙 0 血时波次状态机是冻结的，不回血跳了也不出怪；击杀数保留便于对比）
void MainScene::gotoWave(int offset)
{
    if (_waves.empty())
    {
        return;
    }
    int next = static_cast<int>(_waveIndex) + offset;
    next     = std::clamp(next, 0, static_cast<int>(_waves.size()) - 1);
    clearField();
    _wallHP = WALL_MAX_HP;
    beginWave(static_cast<size_t>(next));
    AXLOGD("调试跳波：第 {} 波", next + 1);
}
#endif  // CG_DEBUG_TOOLS

// ---------------------------------------------------------------------------
// 城墙扣血统一入口：近战啃咬和远程弹丸都走这里，城破判定只有一处
// ---------------------------------------------------------------------------
void MainScene::damageWall(int dmg)
{
    _wallHP = std::max(0, _wallHP - dmg);
    AXLOGD("城墙血量：{}，击杀：{}", _wallHP, _killCount);
    if (_wallHP == 0)
    {
        // 城破后波次状态机冻结（updateWaves 守卫），给个肉眼可见的提示
        if (_waveLabel)
        {
            _waveLabel->setString("城墙告破！点 ↻ 重开");
        }
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
    AXLOGD("敌方弹丸池已满，本次发射丢弃");
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

// =========================== 技能系统（M1） ===========================
// 三段式：点按钮（tryAimSkill 进瞄准）→ 点战场（castSkill 释放）→ 结算。
// 双资源模型：冷却（每技能独立计时）+ 蓝量（全局共享、自动回复）——
// 冷却管"节奏"，蓝量管"取舍"，两个旋钮策划可以分别调。
// ---------------------------------------------------------------------------

// 技能按钮 + 蓝条 + 提示文本。技能 UI 是正式玩法，不进 CG_DEBUG_TOOLS。
void MainScene::initSkillBar()
{
    const float s                   = SKILL_BTN_SIZE;
    const char* names[SKILL_COUNT]  = {"火球", "冰冻", "箭雨", "闪电"};
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        auto btn = Node::create();
        auto bg  = DrawNode::create();
        bg->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.10f, 0.10f, 0.14f, 0.75f));  // 底
        bg->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(1.0f, 1.0f, 1.0f, 0.8f), 2.0f);     // 框
        // 图标（画在按钮上部，下部留给名称）
        const Vec2 c(s * 0.5f, s * 0.62f);
        switch (i)
        {
        case 0:  // 火球：橙红实心圆 + 三簇火苗
        {
            const Color4F fire(0.95f, 0.42f, 0.10f, 1.0f);
            bg->drawSolidCircle(c, 15.0f, 0.0f, 16, fire);
            bg->drawSolidCircle(c, 7.0f, 0.0f, 12, Color4F(1.0f, 0.85f, 0.35f, 1.0f));
            for (int k = -1; k <= 1; ++k)
            {
                bg->drawSolidTriangle(c + Vec2(k * 10.0f - 4, 14), c + Vec2(k * 10.0f + 4, 14),
                                      c + Vec2(k * 12.0f, 26), fire, fire);
            }
            break;
        }
        case 1:  // 冰冻：冰蓝圆环 + 雪花（三条交叉线）
        {
            const Color4F ice(0.40f, 0.78f, 1.0f, 1.0f);
            bg->drawCircle(c, 15.0f, 0.0f, 20, false, ice, 3.0f);
            for (int k = 0; k < 3; ++k)
            {
                const float a = k * 1.0472f;  // 60° 间隔
                Vec2 d(cosf(a) * 13.0f, sinf(a) * 13.0f);
                bg->drawLine(c - d, c + d, ice, 3.0f);
            }
            break;
        }
        case 2:  // 箭雨：三支并排向下的小箭
        {
            const Color4F gold(0.92f, 0.80f, 0.35f, 1.0f);
            for (int k = -1; k <= 1; ++k)
            {
                const float x = c.x + k * 12.0f;
                bg->drawSolidRect(Vec2(x - 1.5f, c.y - 6), Vec2(x + 1.5f, c.y + 12), gold);
                bg->drawSolidTriangle(Vec2(x - 5, c.y - 6), Vec2(x + 5, c.y - 6),
                                      Vec2(x, c.y - 16), gold, gold);
            }
            break;
        }
        default:  // 闪电：黄色锯齿闪电形
        {
            const Color4F bolt(1.0f, 0.88f, 0.20f, 1.0f);
            bg->drawSolidTriangle(c + Vec2(-4, 18), c + Vec2(10, 18), c + Vec2(2, 2), bolt, bolt);
            bg->drawSolidTriangle(c + Vec2(-8, 4), c + Vec2(4, 4), c + Vec2(-2, -16), bolt, bolt);
            break;
        }
        }
        btn->addChild(bg, 0);

        // 冷却遮罩：黑色半透明从底部向上盖，高度 = 剩余冷却比例（每帧重画）
        auto shade = DrawNode::create();
        btn->addChild(shade, 1);
        _skillCdShade[i] = shade;

        // 冷却剩余秒数（居中，冷却时才显示）
        auto cdLabel = Label::createWithSystemFont("", "Arial", 32);
        cdLabel->setPosition(Vec2(s * 0.5f, s * 0.5f));
        btn->addChild(cdLabel, 2);
        _skillCdLabel[i] = cdLabel;

        // 技能名（按钮内底部）
        auto name = Label::createWithSystemFont(names[i], "Arial", 20);
        name->setPosition(Vec2(s * 0.5f, 14.0f));
        btn->addChild(name, 2);
        _skillNameLabel[i] = name;

        addChild(btn, 5);  // zOrder 5：HUD 层
        _skillBtns[i] = btn;
    }

    // 蓝条：DrawNode 每帧重画（两条矩形，开销可忽略）
    _manaBar = DrawNode::create();
    addChild(_manaBar, 5);

    // 瞄准/报错提示：屏幕中部，平时隐藏
    _skillHint = Label::createWithSystemFont("", "Arial", 26);
    _skillHint->setTextColor(Color4B(255, 230, 140, 255));
    _skillHint->setVisible(false);
    addChild(_skillHint, 5);

    // 瞄准范围指示圈：瞄准时跟随手指（拖动）/鼠标（悬停），抬起或点击战场时释放
    _aimIndicator = DrawNode::create();
    _aimIndicator->setVisible(false);
    addChild(_aimIndicator, 4);  // 特效层级
}

// 技能栏跟随布局：底部居中横排，蓝条在按钮正上方
void MainScene::layoutSkillBar(const Vec2& origin, float right)
{
    const float s        = SKILL_BTN_SIZE;
    const float gap      = 16.0f;
    const float totalW   = SKILL_COUNT * s + (SKILL_COUNT - 1) * gap;
    const float centerX  = (origin.x + right) * 0.5f;
    const float btnY     = origin.y + 20.0f;
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        if (!_skillBtns[i])
        {
            continue;
        }
        const float x = centerX - totalW * 0.5f + i * (s + gap);
        _skillBtns[i]->setPosition(Vec2(x, btnY));
        _skillBtnRects[i] = Rect(x, btnY, s, s);
    }
    if (_manaBar)
    {
        _manaBar->setPosition(Vec2(centerX - totalW * 0.5f, btnY + s + 8.0f));
    }
    if (_skillHint)
    {
        auto vs = _director->getVisibleSize();
        _skillHint->setPosition(Vec2(centerX, origin.y + vs.height * 0.62f));
    }
}

// 点技能按钮：再点同一个 = 取消；冷却中/蓝不足 → 提示
void MainScene::tryAimSkill(int index)
{
    if (_aimingSkill == index)
    {
        _aimingSkill = -1;  // 取消瞄准
        _aimDragging = false;
        _aimIndicator->setVisible(false);
        _skillHint->setVisible(false);
        return;
    }
    if (_cdRemaining[index] > 0.0f)
    {
        _skillHint->setString("技能冷却中…");
        _skillHint->setVisible(true);
        _skillHintTimer = 1.2f;
        return;
    }
    if (_mana < SKILL_MANA[index])
    {
        _skillHint->setString("蓝量不足！");
        _skillHint->setVisible(true);
        _skillHintTimer = 1.2f;
        return;
    }
    _aimingSkill = index;
    const char* names[SKILL_COUNT] = {"火球", "冰冻", "箭雨", "闪电"};
    _skillHint->setString(StringUtils::format("「%s」在战场按下选择落点，抬起释放", names[index]));
    _skillHint->setVisible(true);
    _skillHintTimer = 0.0f;  // 瞄准提示常驻（松手也不消失，直到释放/取消）
    // 注意：预瞄阶段不显示范围圈——在战场上按下才出现（Stark 定的交互规格）
    AXLOGD("进入瞄准：{}", names[index]);
}

// 按技能类型重画范围指示圈并移动到目标位置（每技能半径/配色不同）
void MainScene::updateAimIndicator(const Vec2& pos)
{
    if (_aimingSkill < 0 || !_aimIndicator)
    {
        return;
    }
    float radius;
    Color4F color;
    switch (static_cast<SkillType>(_aimingSkill))
    {
    case SkillType::Fireball:  radius = FIREBALL_RADIUS;  color = Color4F(0.95f, 0.45f, 0.10f, 1.0f); break;
    case SkillType::Freeze:    radius = FREEZE_RADIUS;    color = Color4F(0.40f, 0.78f, 1.00f, 1.0f); break;
    case SkillType::Lightning: radius = LIGHTNING_RADIUS; color = Color4F(1.00f, 0.90f, 0.20f, 1.0f); break;
    default:                   radius = RAIN_RADIUS;      color = Color4F(0.92f, 0.80f, 0.35f, 1.0f); break;
    }
    _aimIndicator->clear();
    _aimIndicator->drawSolidCircle(Vec2::ZERO, radius, 0.0f, 32, Color4F(color.r, color.g, color.b, 0.15f));
    _aimIndicator->drawCircle(Vec2::ZERO, radius, 0.0f, 40, false, Color4F(color.r, color.g, color.b, 0.9f), 3.0f);
    if (static_cast<SkillType>(_aimingSkill) == SkillType::Lightning)
    {
        // 闪电多画一个直击内圈：只有内圈劈中才有主目标击杀，外圈只是麻痹范围
        _aimIndicator->drawCircle(Vec2::ZERO, LIGHTNING_DIRECT_R, 0.0f, 24, false, Color4F(color.r, color.g, color.b, 0.95f), 2.0f);
    }
    _aimIndicator->drawSolidCircle(Vec2::ZERO, 4.0f, 0.0f, 10, Color4F(color.r, color.g, color.b, 0.9f));  // 中心点
    _aimIndicator->setPosition(pos);
    _aimIndicator->setVisible(true);
}

// 释放技能：扣蓝、进冷却、按类型结算。落点=手指抬起/鼠标点击处（世界坐标）
void MainScene::castSkill(const Vec2& pos)
{
    const int i = _aimingSkill;
    if (i < 0)
    {
        return;
    }
    // 二次校验：瞄准期间蓝量可能被其他释放扣掉，释放前必须再看一眼
    if (_cdRemaining[i] > 0.0f || _mana < SKILL_MANA[i])
    {
        _aimingSkill = -1;
        _aimDragging = false;
        _aimIndicator->setVisible(false);
        _skillHint->setVisible(false);
        return;
    }
    _mana -= SKILL_MANA[i];
    _cdRemaining[i] = SKILL_CD[i];
    _aimingSkill    = -1;
    _aimDragging    = false;
    _aimIndicator->setVisible(false);
    _skillHint->setVisible(false);

    switch (static_cast<SkillType>(i))
    {
    case SkillType::Fireball:
    {
        // 火球不是立即结算：先有 0.6s 抛物线弹体，落点才爆炸（打击感的来源）
        for (auto& f : _fireballPool)
        {
            if (f.active)
            {
                continue;
            }
            f.active = true;
            f.from   = _archerPos;
            f.target = pos;
            f.t      = 0.0f;
            f.node->setPosition(_archerPos);
            f.node->setVisible(true);
            break;
        }
        break;
    }
    case SkillType::Freeze:
    {
        // AOE 判定：落点圆心 vs 每个怪的身体中心，距离平方比较（不开根号）
        const float r2 = FREEZE_RADIUS * FREEZE_RADIUS;
        for (auto& m : _monsterPool)
        {
            if (!m.active || m.state == MonsterState::Dead)
            {
                continue;
            }
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            if ((bodyCenter - pos).lengthSquared() < r2)
            {
                m.slowTimer = FREEZE_DURATION;  // 减速挂计时器，移动时打折
            }
        }
        spawnFx(1, pos, FREEZE_RADIUS, FREEZE_DURATION);  // 冰冻区域视觉
        break;
    }
    case SkillType::ArrowRain:
    {
        // 排程 10 支箭：每支配"何时落 + 落在哪"，由 updateSkills 到点结算
        for (int k = 0; k < RAIN_STRIKES; ++k)
        {
            const float a  = static_cast<float>(rand()) / RAND_MAX * 6.2832f;
            const float rr = static_cast<float>(rand()) / RAND_MAX * RAIN_RADIUS;
            _rainQueue.push_back({k * RAIN_INTERVAL, pos + Vec2(cosf(a) * rr, sinf(a) * rr)});
        }
        break;
    }
    case SkillType::Lightning:
    {
        // 闪电（原作电系）：雷只劈【中心点】——直击半径内的最近怪吃致命一击，
        // 劈空则没有主目标击杀；外圈到作用半径之间的怪各自掷一次麻痹判定。
        // 伤害与控制分离，和火球的"全灭"拉开定位。
        // 注意：麻痹用的是和冰冻一样的计时器裸实现，通用 buff/debuff 框架留给 M2
        const float directR2 = LIGHTNING_DIRECT_R * LIGHTNING_DIRECT_R;  // 直击圈
        const float auraR2   = LIGHTNING_RADIUS * LIGHTNING_RADIUS;      // 麻痹圈
        Monster* nearest     = nullptr;
        float nearestDistSq  = FLT_MAX;
        for (auto& m : _monsterPool)  // 第一遍：直击圈内找最近主目标
        {
            if (!m.active || m.state == MonsterState::Dead)
            {
                continue;
            }
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            const float d2  = (bodyCenter - pos).lengthSquared();  // 不开根号比远近
            if (d2 < directR2 && d2 < nearestDistSq)
            {
                nearestDistSq = d2;
                nearest       = &m;
            }
        }
        for (auto& m : _monsterPool)  // 第二遍：麻痹圈内非主目标概率麻痹
        {
            if (!m.active || m.state == MonsterState::Dead || &m == nearest)
            {
                continue;
            }
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            if ((bodyCenter - pos).lengthSquared() < auraR2 &&
                static_cast<float>(rand()) / RAND_MAX < LIGHTNING_STUN_CHANCE)
            {
                m.stunTimer = LIGHTNING_STUN_TIME;  // 范围怪：概率麻痹
            }
        }
        if (nearest)
        {
            damageMonster(*nearest, 99);  // 主目标：直接电穿（高伤害，为以后多血怪预留）
        }
        spawnFx(3, pos, LIGHTNING_RADIUS, 0.3f);  // 雷电劈落特效
        break;
    }
    default:
        break;
    }
}

void MainScene::initFireballPool()
{
    _fireballPool.reserve(FIREBALL_POOL_SIZE);
    for (int i = 0; i < FIREBALL_POOL_SIZE; ++i)
    {
        Fireball f;
        f.node = DrawNode::create();
        f.node->drawSolidCircle(Vec2::ZERO, 11.0f, 0.0f, 14, Color4F(0.95f, 0.40f, 0.08f, 1.0f));
        f.node->drawSolidCircle(Vec2::ZERO, 5.0f, 0.0f, 10, Color4F(1.0f, 0.88f, 0.40f, 1.0f));
        f.node->setVisible(false);
        addChild(f.node, 4);  // 特效层级
        _fireballPool.push_back(f);
    }
}

void MainScene::initFxPool()
{
    // 特效专用层：集中管理"活几秒就消失"的图形，层级在箭矢（3）之上、HUD（5）之下
    _effectLayer = Node::create();
    addChild(_effectLayer, 4);

    _fxPool.reserve(FX_POOL_SIZE);
    for (int i = 0; i < FX_POOL_SIZE; ++i)
    {
        Fx fx;
        fx.node = DrawNode::create();
        fx.node->setVisible(false);
        _effectLayer->addChild(fx.node);
        _fxPool.push_back(fx);
    }
}

// 取一个特效槽位：图形内容在 updateSkills 里按 age 每帧重画（动画 = 重画参数）
void MainScene::spawnFx(int kind, const Vec2& pos, float radius, float duration)
{
    for (auto& fx : _fxPool)
    {
        if (fx.active)
        {
            continue;
        }
        fx.active   = true;
        fx.kind     = kind;
        fx.pos      = pos;
        fx.radius   = radius;
        fx.duration = duration;
        fx.age      = 0.0f;
        fx.node->setPosition(pos);
        fx.node->setVisible(true);
        return;
    }
}

// 每帧：回蓝、冷却衰减、火球飞行、箭雨落箭、特效动画、HUD 刷新
void MainScene::updateSkills(float delta)
{
    // ① 双资源回复：蓝量自动回，冷却各自减
    _mana = std::min(MANA_MAX, _mana + MANA_REGEN * delta);
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        _cdRemaining[i] = std::max(0.0f, _cdRemaining[i] - delta);
    }

    // 提示文本的自动隐藏（瞄准提示常驻，不走路径）
    if (_skillHintTimer > 0.0f)
    {
        _skillHintTimer -= delta;
        if (_skillHintTimer <= 0.0f && _aimingSkill < 0)
        {
            _skillHint->setVisible(false);
        }
    }

    // ② 火球弹体：抛物线 = 两点线性插值（lerp）+ 一个先升后降的拱高项。
    //    4t(1-t) 是经典的"归一化抛物线"：t=0/1 时为 0，t=0.5 时最高
    for (auto& f : _fireballPool)
    {
        if (!f.active)
        {
            continue;
        }
        f.t += delta / FIREBALL_FLIGHT;
        if (f.t >= 1.0f)
        {
            // 落点爆炸：AOE 直伤 = 半径内全部击杀
            const float r2 = FIREBALL_RADIUS * FIREBALL_RADIUS;
            for (auto& m : _monsterPool)
            {
                if (!m.active || m.state == MonsterState::Dead)
                {
                    continue;
                }
                Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
                if ((bodyCenter - f.target).lengthSquared() < r2)
                {
                    killMonster(m);
                }
            }
            spawnFx(0, f.target, FIREBALL_RADIUS, 0.4f);  // 爆炸扩散环
            f.active = false;
            f.node->setVisible(false);
            continue;
        }
        const float arc = FIREBALL_ARC_H * 4.0f * f.t * (1.0f - f.t);
        f.node->setPosition(f.from + (f.target - f.from) * f.t + Vec2(0, arc));
    }

    // ③ 箭雨落箭：到点结算小半径点杀 + 落痕特效
    for (size_t i = 0; i < _rainQueue.size();)
    {
        auto& tick = _rainQueue[i];
        tick.delay -= delta;
        if (tick.delay > 0.0f)
        {
            ++i;
            continue;
        }
        const float r2 = RAIN_HIT_R * RAIN_HIT_R;
        for (auto& m : _monsterPool)
        {
            if (!m.active || m.state == MonsterState::Dead)
            {
                continue;
            }
            Vec2 bodyCenter = m.node->getPosition() + Vec2(0, m.bodyR);
            if ((bodyCenter - tick.pos).lengthSquared() < r2)
            {
                killMonster(m);
            }
        }
        spawnFx(2, tick.pos, 0.0f, 0.25f);
        _rainQueue.erase(_rainQueue.begin() + i);  // 结算即删（每帧最多 10 个，erase 开销无感）
    }

    // ④ 特效动画：按 age/duration 重画，播完归池
    for (auto& fx : _fxPool)
    {
        if (!fx.active)
        {
            continue;
        }
        fx.age += delta;
        const float t = fx.age / fx.duration;  // 播放进度 0→1
        if (t >= 1.0f)
        {
            fx.active = false;
            fx.node->setVisible(false);
            continue;
        }
        fx.node->clear();
        if (fx.kind == 0)
        {
            // 爆炸：半径扩散 + 透明度衰减（DrawNode 动画的本质 = 每帧重画）
            fx.node->drawSolidCircle(Vec2::ZERO, fx.radius * (0.3f + 0.7f * t), 0.0f, 24,
                                     Color4F(1.0f, 0.55f, 0.15f, 0.55f * (1.0f - t)));
            fx.node->drawCircle(Vec2::ZERO, fx.radius * t, 0.0f, 28, false,
                                Color4F(1.0f, 0.85f, 0.40f, 1.0f - t), 4.0f);
        }
        else if (fx.kind == 1)
        {
            // 冰冻区：半透明蓝圆常驻，边缘描亮（持续整个减速时间）
            fx.node->drawSolidCircle(Vec2::ZERO, fx.radius, 0.0f, 28,
                                     Color4F(0.35f, 0.75f, 1.0f, 0.18f));
            fx.node->drawCircle(Vec2::ZERO, fx.radius, 0.0f, 32, false,
                                Color4F(0.50f, 0.85f, 1.0f, 0.7f * (1.0f - t * 0.5f)), 3.0f);
        }
        else if (fx.kind == 3)
        {
            // 雷电：从高空劈落的锯齿折线（每次重画都随机抖一下，模拟电弧闪烁）
            //       + 落点光圈扩散。白亮芯 + 黄边双层线
            const float alpha = 1.0f - t;
            const float h     = fx.radius * 2.6f;  // 从多高处劈下
            Vec2 p0(0, h), p1(-fx.radius * 0.22f, h * 0.66f), p2(fx.radius * 0.18f, h * 0.33f), p3(0, 0);
            const Color4F bolt(1.0f, 0.95f, 0.55f, alpha);
            fx.node->drawLine(p0, p1, bolt, 7.0f);
            fx.node->drawLine(p1, p2, bolt, 6.0f);
            fx.node->drawLine(p2, p3, bolt, 5.0f);
            fx.node->drawLine(p0, p1, Color4F(1, 1, 1, alpha), 3.0f);
            fx.node->drawLine(p1, p2, Color4F(1, 1, 1, alpha), 2.5f);
            fx.node->drawLine(p2, p3, Color4F(1, 1, 1, alpha), 2.0f);
            fx.node->drawCircle(Vec2::ZERO, fx.radius * (0.4f + 0.6f * t), 0.0f, 24, false,
                                Color4F(1.0f, 0.95f, 0.5f, alpha), 3.0f);
        }
        else if (fx.kind == 4)
        {
            // 治疗波：绿色圆环向外扩散 + 淡淡的填充（生命感 = 绿）
            fx.node->drawSolidCircle(Vec2::ZERO, fx.radius * t, 0.0f, 26,
                                     Color4F(0.35f, 0.90f, 0.45f, 0.20f * (1.0f - t)));
            fx.node->drawCircle(Vec2::ZERO, fx.radius * t, 0.0f, 30, false,
                                Color4F(0.45f, 1.0f, 0.55f, 0.9f * (1.0f - t)), 3.5f);
        }
        else
        {
            // 箭雨落痕：一支从上方扎下来的白亮短线，快速淡出
            const float len = 120.0f * (1.0f - t);
            fx.node->drawSolidRect(Vec2(-2.0f, 0), Vec2(2.0f, len),
                                   Color4F(0.95f, 0.95f, 1.0f, 0.9f * (1.0f - t)));
        }
    }

    refreshSkillHUD();
}

// 蓝条与冷却遮罩重画。HUD 每帧重画几条矩形是 DrawNode 的常规用法，开销可忽略
void MainScene::refreshSkillHUD()
{
    if (_manaBar)
    {
        const float totalW = SKILL_COUNT * SKILL_BTN_SIZE + (SKILL_COUNT - 1) * 16.0f;
        _manaBar->clear();
        _manaBar->drawSolidRect(Vec2::ZERO, Vec2(totalW, 14.0f), Color4F(0.08f, 0.08f, 0.12f, 0.8f));
        _manaBar->drawSolidRect(Vec2(2, 2), Vec2(2.0f + (totalW - 4.0f) * (_mana / MANA_MAX), 12.0f),
                                Color4F(0.25f, 0.55f, 1.0f, 0.95f));
        _manaBar->drawRect(Vec2::ZERO, Vec2(totalW, 14.0f), Color4F(1, 1, 1, 0.6f));
    }
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        if (!_skillCdShade[i])
        {
            continue;
        }
        _skillCdShade[i]->clear();
        if (_cdRemaining[i] > 0.0f)
        {
            const float h = SKILL_BTN_SIZE * (_cdRemaining[i] / SKILL_CD[i]);
            _skillCdShade[i]->drawSolidRect(Vec2::ZERO, Vec2(SKILL_BTN_SIZE, h),
                                            Color4F(0.0f, 0.0f, 0.0f, 0.65f));
            _skillCdLabel[i]->setString(StringUtils::format("%d", static_cast<int>(_cdRemaining[i]) + 1));
        }
        else
        {
            _skillCdLabel[i]->setString("");
        }
        // 瞄准中的按钮放大一点 = 选中态（零贴图的按钮高亮方案）
        _skillBtns[i]->setScale(i == _aimingSkill ? 1.15f : 1.0f);
    }
}

void MainScene::onTouchesBegan(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    // 输入优先级：调试按钮 > 双指手势 > 射箭。
    // 命中测试：Rect::containsPoint 判断触点是否在按钮热区内——
    // 这就是最朴素的"UI 按钮"实现（一个矩形 + 点包含判定）
    for (auto&& t : touches)
    {
#if CG_DEBUG_TOOLS
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
        if (_debugTuneBtnRect.containsPoint(t->getLocation()))
        {
            toggleTuningPanel();  // ⚙ 手感调优面板
            return;
        }
#endif
        // 技能按钮（正式玩法 UI）：按下只进/退瞄准模式，不显示范围圈
        for (int i = 0; i < SKILL_COUNT; ++i)
        {
            if (_skillBtnRects[i].containsPoint(t->getLocation()))
            {
                tryAimSkill(i);
                return;
            }
        }
        // 瞄准模式下在战场【按下】：范围圈出现并开始跟随（拖动调整，抬起释放）
        if (_aimingSkill >= 0)
        {
            _aimDragging = true;
            updateAimIndicator(t->getLocation());
            return;
        }
    }

#if CG_DEBUG_TOOLS
    // 双指点击 = 切换视角版本（真机手势；模拟器请用左上角调试按钮）
    // EventListenerTouchAllAtOnce 的 touches 数组长度 = 当前按下的手指数
    if (touches.size() >= 2)
    {
        buildBattlefield(_battleLayout == BattleLayout::Oblique25D ? BattleLayout::SideView
                                                                   : BattleLayout::Oblique25D);
        return;
    }
#endif
    // 瞄准模式下的战场按下已在上面处理（显示范围圈），释放由 onTouchesEnded 结算
    for (auto&& t : touches)
    {
        // 单指点击屏幕 → 从箭手位向点击方向射箭
        // t->getLocation() 是世界坐标（已含分辨率适配的换算）
        fireArrow(t->getLocation());
    }
}

void MainScene::onTouchesMoved(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    // 技能瞄准拖动：范围指示圈跟随手指（只有"战场上按下后"才进入这个状态）
    if (_aimingSkill >= 0 && _aimDragging && !touches.empty())
    {
        updateAimIndicator(touches[0]->getLocation());
    }
}

void MainScene::onTouchesEnded(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    // 技能释放：战场上抬起手指 = 在抬起处释放
    if (_aimingSkill >= 0 && _aimDragging && !touches.empty())
    {
        _aimDragging = false;
        castSkill(touches[0]->getLocation());
    }
}

void MainScene::onKeyPressed(ax::EventKeyboard::KeyCode code, ax::Event* event)
{
#if CG_DEBUG_TOOLS
    // 桌面调试键：1/2 切视角、R 重开、Q/E 跳波、T 调参（正式包无键盘监听）
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
        resetGame();
    }
    else if (code == EventKeyboard::KeyCode::KEY_Q)
    {
        gotoWave(-1);
    }
    else if (code == EventKeyboard::KeyCode::KEY_E)
    {
        gotoWave(1);
    }
    else if (code == EventKeyboard::KeyCode::KEY_T)
    {
        toggleTuningPanel();
    }
    else if (code >= EventKeyboard::KeyCode::KEY_3 && code <= EventKeyboard::KeyCode::KEY_6)
    {
        tryAimSkill(static_cast<int>(code) - static_cast<int>(EventKeyboard::KeyCode::KEY_3));
    }
#endif
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

        // 射箭冷却每帧衰减（连点射速上限）
        _fireCooldown = std::max(0.0f, _fireCooldown - delta);

        // 游戏主循环：怪物移动、箭矢飞行、敌方弹丸、技能系统都在这里驱动
        updateMonsters(delta);
        updateArrows(delta);
        updateEnemyBolts(delta);
        updateSkills(delta);  // M1：回蓝/冷却/火球弹道/箭雨/特效
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
