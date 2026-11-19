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
#    include "DevelopmentMenuPanel.h"
#endif
#include "GameData.h"
#include "ModeHomePanel.h"
#include "ModeSelectPanel.h"
#include "ThreeChoicePanel.h"
#include "UpgradePanel.h"

#if CG_DEBUG_TOOLS
#    include "ImGui/ImGuiPresenter.h"  // 引擎扩展：手感调优面板（正式包剔除）
#endif

#include <algorithm>  // std::max
#include <array>
#include <cfloat>     // FLT_MAX（闪电找最近主目标）
#include <cmath>      // std::ceil
#include <cstdlib>    // rand / srand
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
constexpr int MAX_MULTI_SHOT_COUNT = 8;     // RunBuffModel 的多重箭硬上限
constexpr int ARROW_POOL_SIZE = 256;         // 低箭速 + 多重箭连续射击仍能维持完整齐射
// 手感调优清单 4 项全在这（ImGui 面板实时拖滑杆，别编译了）：
float ARROW_SPEED     = 1000.0f;  // ① 箭矢初速（像素/秒）
// 重力加速度（像素/秒²）。0 = 直线弹道；调成 600~900 就是抛物线
float ARROW_GRAVITY   = 0.0f;     // ② 抛物线 vs 直线
float FAST_MODE_FIRE_INTERVAL = 0.18f;  // 玩法核心/肉鸽保留快速射击基准
float CAMPAIGN_FIRE_INTERVAL  = 1.0f;   // 闯关模式基础攻速：1 秒一发
constexpr float RAD2DEG         = 57.2957795f;  // 弧度→角度（180/π）
constexpr float MULTI_SHOT_SPREAD = 0.10f;      // 多重箭相邻箭的夹角（弧度，约 5.7°）
constexpr float ARROW_SPAWN_FORWARD_OFFSET = 24.0f;  // 箭节点原点在杆中部，前移后让箭尾贴住弓弦
constexpr float BOW_MULTI_ARROW_SPACING = 6.0f;      // 搭载箭横向间距，避免多支箭完全重叠

// ---- 调试层 ----
#if CG_DEBUG_TOOLS
constexpr float DEBUG_BTN_SIZE  = 88.0f;    // 左上角"切换视角"按钮边长（设计像素）
#endif

// ---- 怪物系统 ----
constexpr int   MONSTER_POOL_SIZE     = 48;     // 对象池容量：同时在屏怪物上限（怪群）
constexpr float MONSTER_BODY_R        = 11.0f;  // 身体半径（占位圆，脚底为节点原点）
// 类型血量/速度/体型/行为数值统一归 MonsterBalance，场景只保留行为规则与表现常量。
constexpr float MONSTER_BREATH_SCALE_X = 0.04f;   // 横向 ±4%，与纵向反相形成挤压/伸展
constexpr float MONSTER_BREATH_SCALE_Y = 0.08f;   // 纵向 ±8%，约 4px 峰谷差，缩放后仍清晰可见
constexpr float MONSTER_BREATH_SPEED_MIN = 2.0f;  // 呼吸相位速度下限（弧度/秒）
constexpr float MONSTER_BREATH_SPEED_MAX = 3.0f;  // 呼吸相位速度上限
constexpr float KNOCKBACK_TRAVEL_SPEED = 480.0f;  // 击退平滑位移速度，避免命中瞬移
constexpr float MAX_QUEUED_KNOCKBACK   = 120.0f;  // 高频连射时限制待结算位移，防止推出战场
constexpr float BOMBER_FX_RADIUS      = 60.0f;  // 自爆特效半径（纯表现，伤害只打城墙）
// 飞行怪：游击手——进场后在远区空域持续游荡（航点随机+正弦摆动出曲线），
// 攻击严格按间隔计时，移动不打断计时——频率稳定，走位飘忽
constexpr float FLYER_SWAY_AMP         = 55.0f;  // 游荡正弦摆动幅度（像素/秒，越大轨迹越飘）
constexpr float FLYER_SWAY_FREQ        = 4.0f;   // 摆动频率（弧度/秒）
constexpr float FLYER_WINDUP           = 0.5f;   // 攻击前摇（秒）：悬停蓄力，看得见的前摇提示
// 治疗怪：跟在怪群后方，周期性给周围友军回血——逼玩家越过前排点杀后排
constexpr float HEALER_RADIUS          = 110.0f; // 治疗半径
// 分裂怪：死后分裂成两只小快怪——克单点箭，怕 AOE（火球一锅端）
constexpr float SPLIT_CHILD_OFFSET     = 28.0f;  // 子体出生横向偏移
// 狂暴怪：血越少跑得越快——要么集火秒掉，要么留到最后处理
constexpr float RAGER_SPEED_BOOST      = 1.2f;   // 空血时额外加速比例（+120%）
// 幽灵怪：周期性隐身，隐身时箭打不中（AOE 技能照吃）——逼玩家掐时机或留蓝
constexpr float GHOST_SHOW_TIME        = 2.5f;   // 显形时长（秒）
constexpr float GHOST_HIDE_TIME        = 1.8f;   // 隐身时长（秒）
// 召唤怪：站远区周期性召唤小怪——不集火就越打越多
// 护符怪：一次性全能护盾（挡任何攻击一次）+ 高速——必须优先处理的冲锋手
// Boss：自由飞（游荡复用飞行怪机制）、厚血、前摇后扇形三连发
constexpr float BOSS_WINDUP            = 1.2f;   // 大招前摇（秒，长前摇=玩家的输出窗口）
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
// ---- 经济 HUD / 结算层（纯表现尺寸，奖励数值归 GameData）----
constexpr float SETTLEMENT_BUTTON_W = 230.0f;
constexpr float SETTLEMENT_BUTTON_H = 72.0f;
constexpr float SETTLEMENT_BUTTON_GAP = 24.0f;
constexpr float WALL_HEALTH_BAR_W = 360.0f;
constexpr float BOSS_HEALTH_BAR_W = 480.0f;
constexpr float COMBAT_HEALTH_BAR_H = 28.0f;

// 墙头主武器占位：弓身与箭挂点分层；挂点承载的就是之后飞行的真实对象池箭。
// 局部 +Y 是发射方向；侧视战场旋转 90° 后即朝右。
Node* createWallBow(float rotation, Node*& bowVisual, Node*& arrowMount)
{
    auto* weapon = Node::create();
    bowVisual = Node::create();
    arrowMount = Node::create();
    auto* bow = DrawNode::create();
    const Color4F outline(0.16f, 0.09f, 0.04f, 1.0f);
    const Color4F wood(0.72f, 0.40f, 0.14f, 1.0f);
    const Color4F highlight(0.94f, 0.68f, 0.26f, 1.0f);
    const Color4F stringColor(0.88f, 0.90f, 0.86f, 0.95f);
    const Vec2 leftTip(-36.0f, 13.0f);
    const Vec2 leftMid(-23.0f, 23.0f);
    const Vec2 center(0.0f, 29.0f);
    const Vec2 rightMid(23.0f, 23.0f);
    const Vec2 rightTip(36.0f, 13.0f);

    // 深色粗描边 + 木色弓臂，让小尺寸下仍有清楚轮廓。
    bow->drawLine(leftTip, leftMid, outline, 9.0f);
    bow->drawLine(leftMid, center, outline, 9.0f);
    bow->drawLine(center, rightMid, outline, 9.0f);
    bow->drawLine(rightMid, rightTip, outline, 9.0f);
    bow->drawLine(leftTip, leftMid, wood, 5.0f);
    bow->drawLine(leftMid, center, highlight, 5.0f);
    bow->drawLine(center, rightMid, highlight, 5.0f);
    bow->drawLine(rightMid, rightTip, wood, 5.0f);

    // 攻击间隔内保持空弓：弦回到两端连线，箭只在真实弹体发射时出现。
    bow->drawLine(leftTip, rightTip, stringColor, 2.0f);
    bow->drawSolidRect(Vec2(-5.0f, 23.0f), Vec2(5.0f, 35.0f),
                       Color4F(0.24f, 0.14f, 0.07f, 1.0f));

    bowVisual->addChild(bow);
    weapon->addChild(bowVisual);
    weapon->addChild(arrowMount, 1);
    weapon->setRotation(rotation);
    return weapon;
}
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
    // 玩家钱包和成长存档在 GameData 构造时加载；新一局从当前城墙上限开始。
    _wallMaxHP = GameData::getInstance().wallMaxHealth();
    _wallHP    = _wallMaxHP;
    initEconomyUI();
    initCombatHUD();
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
    loadCampaign();
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
    _touchListener->onTouchesCancelled = AX_CALLBACK_2(MainScene::onTouchesCancelled, this);
    _eventDispatcher->addEventListenerWithSceneGraphPriority(_touchListener, this);

#if CG_DEBUG_TOOLS
    // 键盘监听只在开发版注册：桌面调试键（1/2 视角、R 重开、Q/E 波次、T 调参、U 升级页）
    _keyboardListener                = EventListenerKeyboard::create();
    _keyboardListener->onKeyPressed  = AX_CALLBACK_2(MainScene::onKeyPressed, this);
    _keyboardListener->onKeyReleased = AX_CALLBACK_2(MainScene::onKeyReleased, this);
    _eventDispatcher->addEventListenerWithFixedPriority(_keyboardListener, 11);
#endif

    // scheduleUpdate() is required to ensure update(float) is called on every loop
    scheduleUpdate();

    showEntryMenu();

    return true;
}

// ---------------------------------------------------------------------------
// 按布局重建战场：所有战场节点挂在一个容器 Node 下，切换/旋转时整体销毁重画。
// 这是一种很常用的"场景重组"手法——容器节点的孩子是相对容器定位的，
// 这里容器放原点不动，孩子直接用世界坐标，简单直观。
// ---------------------------------------------------------------------------
void MainScene::buildBattlefield(BattleLayout layout)
{
    // 已装填的真实箭属于旧弓的子节点；先取回场景池，再销毁旧战场。
    for (auto& a : _arrowPool)
    {
        parkArrow(a);
    }
    _wallBowWeapon = nullptr;  // 旧弓属于旧 battlefield，移除容器前先清掉非拥有指针
    _wallBowVisual = nullptr;
    _wallBowArrowMount = nullptr;
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

    // 布局重建后战场几何变了，在场怪物与其他弹体全部回收（回池，不销毁）
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
        house->setPosition(Vec2(origin.x + visibleSize.width * SIDE_HOUSE_X_RATIO, groundTop));
        _battlefield->addChild(house, 1);

        // 屋顶弓朝向右侧战场；箭矢从拉弦释放点发射。
        _archerPos = house->getPosition() + Vec2(0, 120);
        _wallBowWeapon = createWallBow(90.0f, _wallBowVisual, _wallBowArrowMount);
        _wallBowWeapon->setPosition(_archerPos);
        _wallBowWeapon->setScale(0.88f);
        _battlefield->addChild(_wallBowWeapon, 2);

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

        // 城墙（底）：横条墙体 + 顶缘墙垛 + 中央主弓（箭矢从这里射出）
        auto wall = DrawNode::create();
        wall->drawSolidRect(origin, Vec2(right, wallTop), Color4F(0.50f, 0.50f, 0.55f, 1.0f));
        const float merlonW = 22.0f, merlonH = 14.0f, merlonGap = 14.0f;
        for (float x = origin.x; x + merlonW <= right; x += merlonW + merlonGap)
        {
            wall->drawSolidRect(Vec2(x, wallTop), Vec2(x + merlonW, wallTop + merlonH),
                                Color4F(0.62f, 0.62f, 0.67f, 1.0f));
        }
        const float centerX = (origin.x + right) * 0.5f;
        _battlefield->addChild(wall, 1);

        // 斜俯视主弓朝向上方战场，拉弦点就是弹体出生点。
        _archerPos = Vec2(centerX, wallTop + 8.0f);
        _wallBowWeapon = createWallBow(0.0f, _wallBowVisual, _wallBowArrowMount);
        _wallBowWeapon->setPosition(_archerPos);
        _battlefield->addChild(_wallBowWeapon, 2);

        // 出怪线（顶）：横向红色虚线，贯穿全宽
        auto spawnLine = DrawNode::create();
        for (float x = origin.x; x + dash <= right; x += dash + gap)
        {
            spawnLine->drawSolidRect(Vec2(x, spawnY), Vec2(x + dash, spawnY + lineW),
                                     Color4F(0.9f, 0.15f, 0.15f, 1.0f));
        }
        _battlefield->addChild(spawnLine, 1);
    }

    // 新战场初始即处于装填完成状态；箭数跟随当前多重箭属性。
    refreshWallBowLoadedArrows();

#if CG_DEBUG_TOOLS
    layoutDebugUI(origin, top);  // 调试按钮重排（正式包无此函数）
#endif
    layoutSkillBar(origin, right);  // 技能栏重排（正式玩法 UI）
    layoutEconomyUI(origin, right, top);
    layoutCombatHUD(origin, visibleSize);
    // 波次 HUD：顶部居中
    if (_waveLabel)
    {
        _waveLabel->setPosition(Vec2((origin.x + right) * 0.5f, top - 30.0f));
    }

    // 战场重建回收了在场怪物（见函数开头）→ 重打当前波。
    // 配置是只读的，重置运行时状态即可；AllClear（全部打完）就不重开了
    if (!_settlementVisible && (!_choicePanel || !_choicePanel->isVisible()) &&
        (!_upgradePanel || !_upgradePanel->isVisible()) &&
        (!_modeSelectPanel || !_modeSelectPanel->isVisible())
#if CG_DEBUG_TOOLS
        && (!_developmentMenuPanel || !_developmentMenuPanel->isVisible())
#endif
        && (!_campaignMenuPanel || !_campaignMenuPanel->isVisible())
        && (!_roguelikeMenuPanel || !_roguelikeMenuPanel->isVisible())
        && !_waves.empty() &&
        (_waveState == WaveState::Spawning || _waveState == WaveState::Resting))
    {
        // 重建会清掉场上怪并重播当前波，先回滚到本波起点，避免重复击杀刷金币。
        GameData::getInstance().rewindToCheckpoint();
        refreshEconomyHUD();
        beginWave(_waveIndex);
    }

    AXLOGD("战场已重建：{}，可见尺寸=({:.0f}, {:.0f})",
           layout == BattleLayout::Oblique25D ? "Oblique25D" : "SideView",
           visibleSize.width, visibleSize.height);
}

// ---------------------------------------------------------------------------
// 共享战斗血条：只读取本局战斗状态，不持有生命规则。
// 城墙条固定在技能栏上方；Boss 条固定在顶部，避免跟随大体型节点遮挡瞄准。
// ---------------------------------------------------------------------------
void MainScene::initCombatHUD()
{
    _wallHealthBar = DrawNode::create();
    addChild(_wallHealthBar, 6);
    _wallHealthLabel = Label::createWithSystemFont("", "Arial", 19);
    _wallHealthLabel->setTextColor(Color4B::WHITE);
    _wallHealthLabel->enableOutline(Color4B(0, 0, 0, 220), 2);
    addChild(_wallHealthLabel, 6);

    _bossHealthBar = DrawNode::create();
    addChild(_bossHealthBar, 6);
    _bossHealthLabel = Label::createWithSystemFont("", "Arial", 25);
    _bossHealthLabel->setTextColor(Color4B(255, 222, 224, 255));
    addChild(_bossHealthLabel, 6);

    _wallHealthBar->setVisible(false);
    _wallHealthLabel->setVisible(false);
    _bossHealthBar->setVisible(false);
    _bossHealthLabel->setVisible(false);
    AXLOGI("[CombatCore] health HUD initialized");
}

void MainScene::layoutCombatHUD(const Vec2& origin, const Size& visibleSize)
{
    const float centerX = origin.x + visibleSize.width * 0.5f;
    const float wallW   = std::min(WALL_HEALTH_BAR_W, visibleSize.width * 0.50f);
    const float bossW   = std::min(BOSS_HEALTH_BAR_W, visibleSize.width * 0.68f);
    const float wallY   = origin.y + SKILL_BTN_SIZE + 55.0f;
    const float bossY   = origin.y + visibleSize.height - 135.0f;
    _wallHealthRect = Rect(centerX - wallW * 0.5f, wallY, wallW, COMBAT_HEALTH_BAR_H);
    _bossHealthRect = Rect(centerX - bossW * 0.5f, bossY, bossW, COMBAT_HEALTH_BAR_H);
    _wallHealthLabel->setPosition(Vec2(centerX, wallY + COMBAT_HEALTH_BAR_H * 0.5f));
    _bossHealthLabel->setPosition(Vec2(centerX, bossY + 38.0f));
}

void MainScene::refreshCombatHUD()
{
    if (!_wallHealthBar || !_bossHealthBar)
    {
        return;
    }
    const bool battleVisible = _gameState == GameState::update;
    _wallHealthBar->setVisible(battleVisible);
    _wallHealthLabel->setVisible(battleVisible);
    if (battleVisible)
    {
        const float ratio = std::clamp(static_cast<float>(_wallHP) /
                                           static_cast<float>(std::max(1, _wallMaxHP)),
                                       0.0f, 1.0f);
        const Color4F fill = ratio > 0.55f ? Color4F(0.24f, 0.78f, 0.42f, 0.96f)
                             : ratio > 0.25f ? Color4F(0.92f, 0.68f, 0.20f, 0.96f)
                                             : Color4F(0.90f, 0.25f, 0.20f, 0.96f);
        const Vec2 min = _wallHealthRect.origin;
        const Vec2 max(min.x + _wallHealthRect.size.width, min.y + _wallHealthRect.size.height);
        _wallHealthBar->clear();
        _wallHealthBar->drawSolidRect(min, max, Color4F(0.04f, 0.06f, 0.08f, 0.88f));
        _wallHealthBar->drawSolidRect(min, Vec2(min.x + _wallHealthRect.size.width * ratio, max.y), fill);
        _wallHealthBar->drawRect(min, max, Color4F(0.80f, 0.86f, 0.90f, 0.95f), 2.0f);
        _wallHealthLabel->setString(StringUtils::format("城墙  %d / %d", _wallHP, _wallMaxHP));
    }

    int bossCount = 0;
    int bossHP = 0;
    int bossMaxHP = 0;
    for (const auto& monster : _monsterPool)
    {
        if (monster.active && monster.state != MonsterState::Dead &&
            monster.type == MonsterType::Boss)
        {
            ++bossCount;
            bossHP += std::max(0, monster.hp);
            bossMaxHP += monster.maxHp;
        }
    }
    const bool bossVisible = battleVisible && bossCount > 0 && bossMaxHP > 0;
    _bossHealthBar->setVisible(bossVisible);
    _bossHealthLabel->setVisible(bossVisible);
    if (bossVisible)
    {
        const float ratio = std::clamp(static_cast<float>(bossHP) / bossMaxHP, 0.0f, 1.0f);
        const Vec2 min = _bossHealthRect.origin;
        const Vec2 max(min.x + _bossHealthRect.size.width, min.y + _bossHealthRect.size.height);
        _bossHealthBar->clear();
        _bossHealthBar->drawSolidRect(min, max, Color4F(0.05f, 0.03f, 0.06f, 0.92f));
        _bossHealthBar->drawSolidRect(
            min, Vec2(min.x + _bossHealthRect.size.width * ratio, max.y),
            Color4F(0.78f, 0.16f, 0.26f, 0.98f));
        _bossHealthBar->drawRect(min, max, Color4F(1.0f, 0.72f, 0.76f, 0.96f), 2.5f);
        _bossHealthLabel->setString(
            bossCount > 1 ? StringUtils::format("BOSS x%d  %d / %d", bossCount, bossHP, bossMaxHP)
                          : StringUtils::format("BOSS  %d / %d", bossHP, bossMaxHP));
    }
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

    // 四宫格 = 返回开发总入口。zOrder 高于所有模态页，任何玩法状态下都可退出。
    _debugModeMenuBtn = Node::create();
    auto menuBg = DrawNode::create();
    menuBg->drawSolidRect(Vec2::ZERO, Vec2(s, s), Color4F(0.07f, 0.14f, 0.18f, 0.88f));
    menuBg->drawRect(Vec2::ZERO, Vec2(s, s), Color4F(0.35f, 0.90f, 0.82f, 1.0f), 2.5f);
    const Color4F menuIcon(0.82f, 1.0f, 0.95f, 1.0f);
    const float tile = s * 0.20f;
    const float gap = s * 0.10f;
    const float gridMin = (s - tile * 2.0f - gap) * 0.5f;
    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 2; ++column)
        {
            const Vec2 min(gridMin + column * (tile + gap), gridMin + row * (tile + gap));
            menuBg->drawSolidRect(min, min + Vec2(tile, tile), menuIcon);
        }
    }
    _debugModeMenuBtn->addChild(menuBg);
    addChild(_debugModeMenuBtn, 60);

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
    // 与开发总入口一样高于模式主页/工坊，闯关调参不必先进入战斗。
    addChild(_debugTuneBtn, 60);
}

// 调试按钮跟随布局定位：左上角留边区（避开右上角的微信胶囊菜单）。
// 三排：▦ 开发总入口 + ⇄ 切视角 / ↻ 重开 / ◀ ▶ ⚙（跳波 + 调参）
void MainScene::layoutDebugUI(const Vec2& origin, float top)
{
    const float s = DEBUG_BTN_SIZE;
    if (_debugModeMenuBtn)
    {
        const Vec2 p(origin.x + 16.0f, top - 16.0f - s);
        _debugModeMenuBtn->setPosition(p);
        _debugModeMenuBtnRect = Rect(p.x, p.y, s, s);
    }
    if (_debugBtn)
    {
        Vec2 p(origin.x + 16.0f + s + 12.0f, top - 16.0f - s);
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
        syncSandboxDraft();
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
    ImGui::SetNextWindowSize(ImVec2(520, 720), ImGuiCond_FirstUseEver);
    ImGui::Begin("开发者工具");
    ImGui::Text("帧率: %.1f", 1.0f / ImGui::GetIO().DeltaTime);
    if (ImGui::BeginTabBar("开发者工具标签"))
    {
        if (ImGui::BeginTabItem("实时调参"))
        {
            drawTuningTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("怪物参数"))
        {
            drawMonsterTuningTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("沙盒"))
        {
            drawSandboxTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void MainScene::drawTuningTab()
{
    ImGui::SeparatorText("射箭");
    // 允许归零：箭从弓上脱离后停在发射点，便于逐帧检查单箭/多重箭表现。
    ImGui::SliderFloat("箭矢速度", &ARROW_SPEED, 0.0f, 2000.0f, "%.0f 像素/秒");
    if (_gameMode == GameMode::Campaign)
    {
        ImGui::SliderFloat("闯关基础射击间隔", &CAMPAIGN_FIRE_INTERVAL, 0.05f, 2.0f, "%.2f 秒/发");
    }
    else
    {
        ImGui::SliderFloat("快速模式基础射击间隔", &FAST_MODE_FIRE_INTERVAL, 0.05f, 2.0f,
                           "%.2f 秒/发");
    }
    ImGui::Text("疾射升级后实际间隔: %.3f 秒/发", currentCombatStats().fireInterval);
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
    ImGui::Text("城墙血量: %d  击杀: %d  蓝量: %.0f", _wallHP,
                GameData::getInstance().economy().runKills(), _mana);
}

void MainScene::drawMonsterTuningTab()
{
    static const char* monsterNames[] = {"普通怪", "快速怪", "远程怪", "护盾怪", "自爆怪",
                                         "飞行怪", "治疗怪", "分裂怪", "狂暴怪", "幽灵怪",
                                         "召唤怪", "护符怪", "Boss"};
    static const char* powerLabels[] = {"啃墙伤害", "啃墙伤害", "弹丸伤害", "啃墙伤害", "自爆伤害",
                                        "弹丸伤害", "治疗量", "啃墙伤害", "啃墙伤害", "啃墙伤害",
                                        "召唤数量", "啃墙伤害", "单发伤害"};

    ImGui::SeparatorText("全局");
    float baseSpeed = _monsterBalance.baseSpeed();
    if (ImGui::SliderFloat("基础速度", &baseSpeed, 10.0f, 300.0f, "%.0f 像素/秒"))
    {
        _monsterBalance.setBaseSpeed(baseSpeed);
        _monsterTuningDirtyType = _monsterTuningType;
    }
    float baseBodyRadius = _monsterBalance.baseBodyRadius();
    if (ImGui::SliderFloat("基础体型", &baseBodyRadius, 8.0f, 24.0f, "%.0f 像素半径"))
    {
        _monsterBalance.setBaseBodyRadius(baseBodyRadius);
        _monsterTuningDirtyType = _monsterTuningType;
    }

    ImGui::SeparatorText("类型参数");
    ImGui::Combo("怪物类型", &_monsterTuningType, monsterNames,
                 static_cast<int>(MonsterBalance::TYPE_COUNT));
    const auto type = static_cast<MonsterType>(_monsterTuningType);
    auto value      = _monsterBalance.parameters(type);
    bool changed    = false;
    changed |= ImGui::SliderInt("生命上限", &value.maxHp, 1, 100);
    changed |= ImGui::SliderFloat("速度倍率", &value.speedMultiplier, 0.1f, 4.0f, "%.2f 倍");
    changed |= ImGui::SliderFloat("体型倍率", &value.bodyScale, 0.4f, 3.0f, "%.2f 倍");
    changed |= ImGui::SliderFloat("击退承受系数", &value.knockbackFactor, 0.0f, 2.0f, "%.2f 倍");
    changed |= ImGui::SliderInt(powerLabels[_monsterTuningType], &value.actionPower, 0, 50);
    if (type != MonsterType::Bomber)
    {
        changed |= ImGui::SliderFloat("行为间隔", &value.actionInterval, 0.1f, 10.0f, "%.2f 秒");
    }

    const bool usesStopRatio = type == MonsterType::Ranged || type == MonsterType::Flyer ||
                               type == MonsterType::Healer || type == MonsterType::Summoner ||
                               type == MonsterType::Boss;
    if (usesStopRatio)
    {
        changed |= ImGui::SliderFloat("驻足位置下限", &value.stopRatioMin, 0.05f, 0.90f, "%.2f 纵深");
        changed |= ImGui::SliderFloat("驻足位置上限", &value.stopRatioMax, 0.05f, 0.90f, "%.2f 纵深");
    }

    if (changed)
    {
        _monsterBalance.setParameters(type, value);
        _monsterTuningDirtyType = _monsterTuningType;
    }
    if (_monsterTuningDirtyType >= 0 && !ImGui::IsAnyItemActive())
    {
        const auto loggedType  = static_cast<MonsterType>(_monsterTuningDirtyType);
        const auto& loggedData = _monsterBalance.parameters(loggedType);
        AXLOGI("[MonsterTuning] updated: type={}, baseSpeed={}, baseBodyRadius={}, hp={}, speedMul={}, "
               "bodyScale={}, knockbackFactor={}, power={}, interval={}, stop=[{},{}]",
               monsterNames[_monsterTuningDirtyType], _monsterBalance.baseSpeed(),
               _monsterBalance.baseBodyRadius(), loggedData.maxHp, loggedData.speedMultiplier,
               loggedData.bodyScale, loggedData.knockbackFactor, loggedData.actionPower,
               loggedData.actionInterval,
               loggedData.stopRatioMin, loggedData.stopRatioMax);
        _monsterTuningDirtyType = -1;
    }

    ImGui::TextDisabled("生效范围：之后生成的怪物");
    if (ImGui::Button("恢复当前怪物默认值"))
    {
        _monsterBalance.reset(type);
        _monsterTuningDirtyType = -1;
        AXLOGI("[MonsterTuning] reset type: {}", monsterNames[_monsterTuningType]);
    }
    ImGui::SameLine();
    if (ImGui::Button("全部恢复默认值"))
    {
        _monsterBalance.resetAll();
        _monsterTuningDirtyType = -1;
        AXLOGI("[MonsterTuning] reset all monster parameters");
    }
}

void MainScene::syncSandboxDraft()
{
    _sandboxDraft = GameData::getInstance().debugPlayerData();
}

void MainScene::sandboxKillAllMonsters()
{
    int killed = 0;
    // 分裂怪死亡会生成子体，多走一遍即可把同一批子体也纳入这次沙盒清场。
    for (int pass = 0; pass < 2; ++pass)
    {
        for (auto& monster : _monsterPool)
        {
            if (monster.active && monster.state != MonsterState::Dead)
            {
                killMonster(monster);
                ++killed;
            }
        }
    }
    for (auto& bolt : _boltPool)
    {
        bolt.active = false;
        bolt.node->setVisible(false);
    }
    AXLOGI("[Sandbox] battlefield cleared: killed={}, enemyBoltsRecycled=true", killed);
}

void MainScene::sandboxOpenUpgradePreview()
{
    if (_settlementVisible)
    {
        return;
    }
    stopContinuousFire();
    _settlementVisible = true;
    _gameState         = GameState::end;
    _settlementLayer->setVisible(false);
    _upgradePanel->open();
    AXLOGI("[Sandbox] upgrade preview opened without settlement");
}

void MainScene::drawSandboxTab()
{
    auto& data = GameData::getInstance();

    ImGui::SeparatorText("当前局作弊");
    if (ImGui::Checkbox("城墙无敌", &_sandboxInvincibleWall))
    {
        AXLOGI("[Sandbox] invincible wall={}", _sandboxInvincibleWall);
    }
    if (ImGui::Checkbox("无限蓝量", &_sandboxInfiniteMana))
    {
        AXLOGI("[Sandbox] infinite mana={}", _sandboxInfiniteMana);
    }
    if (ImGui::Checkbox("技能无冷却", &_sandboxNoSkillCooldown))
    {
        AXLOGI("[Sandbox] no skill cooldown={}", _sandboxNoSkillCooldown);
    }
    if (ImGui::Checkbox("射箭无冷却", &_sandboxNoFireCooldown))
    {
        AXLOGI("[Sandbox] no fire cooldown={}", _sandboxNoFireCooldown);
    }

    const bool runControllable = data.economy().runActive() && !_settlementVisible;
    const char* modeName = _gameMode == GameMode::Core ? "玩法核心"
                           : _gameMode == GameMode::Endless ? "肉鸽模式"
                                                            : "闯关模式";
    ImGui::Text("当前模式: %s", modeName);
    if (_gameMode == GameMode::Core)
    {
        ImGui::Text("核心波次: %d / %d", static_cast<int>(_waveIndex) + 1,
                    static_cast<int>(_waves.size()));
    }
    else if (_gameMode == GameMode::Endless)
    {
        ImGui::Text("当前波次: %d", _endlessWaveNumber);
    }
    else
    {
        ImGui::Text("当前关卡: %d / %d", static_cast<int>(_levelIndex) + 1,
                    static_cast<int>(_campaign.levels().size()));
    }
    ImGui::BeginDisabled(_gameMode != GameMode::Campaign);
    if (ImGui::Button("上一关"))
    {
        sandboxGotoLevel(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button("下一关"))
    {
        sandboxGotoLevel(1);
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(!runControllable);
    if (ImGui::Button("立即获胜并结算"))
    {
        AXLOGI("[Sandbox] forced clear: mode={}",
               _gameMode == GameMode::Core ? "core"
               : _gameMode == GameMode::Endless ? "endless"
                                                : "campaign");
        if (_gameMode == GameMode::Endless)
        {
            offerRunBuff();
        }
        else
        {
            showSettlement(true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("立即失败并结算"))
    {
        _wallHP = 0;
        AXLOGI("[Sandbox] forced defeat");
        showSettlement(false);
    }
    if (ImGui::Button("秒杀场上全部敌人"))
    {
        sandboxKillAllMonsters();
    }
    ImGui::SameLine();
    if (ImGui::Button("回满城墙"))
    {
        _wallMaxHP = data.wallMaxHealth();
        _wallHP    = _wallMaxHP;
        AXLOGI("[Sandbox] wall restored: hp={}", _wallHP);
    }
    ImGui::SameLine();
    if (ImGui::Button("补满蓝量"))
    {
        _mana = MANA_MAX;
        AXLOGI("[Sandbox] mana restored");
    }
    if (ImGui::Button("上一波"))
    {
        gotoWave(-1);
    }
    ImGui::SameLine();
    if (ImGui::Button("下一波"))
    {
        gotoWave(1);
    }
    ImGui::SameLine();
    if (ImGui::Button("跳到末波"))
    {
        gotoWave(static_cast<int>(_waves.size()));
    }
    ImGui::EndDisabled();

    if (ImGui::Button("重开本局"))
    {
        resetGame();
    }
    ImGui::SameLine();
    if (ImGui::Button("预览升级页"))
    {
        sandboxOpenUpgradePreview();
    }

    ImGui::SeparatorText("持久化玩家数据");
    ImGui::TextDisabled("修改草稿后点击“应用到存档”，桌面与微信存档都会立即写入。");
    ImGui::InputInt("金币", &_sandboxDraft.coins, 100, 1000);
    ImGui::InputInt("水晶", &_sandboxDraft.crystals, 10, 100);
    ImGui::InputInt("已通关关数", &_sandboxDraft.highestClearedStage, 1, 1);
    ImGui::InputInt("已领养成奖励关数", &_sandboxDraft.highestRewardedStage, 1, 1);
    ImGui::TextDisabled("关卡进度在重开或开始下一局时生效。正式战役共 5 关。");

    const char* upgradeNames[] = {"强弓等级", "疾射等级", "多重箭等级", "城墙等级"};
    for (size_t i = 0; i < _sandboxDraft.upgradeLevels.size(); ++i)
    {
        const auto id      = static_cast<UpgradeId>(i);
        const int maxLevel = data.upgradeDefinition(id).maxLevel();
        ImGui::SliderInt(upgradeNames[i], &_sandboxDraft.upgradeLevels[i], 0, maxLevel);
    }

    const char* skillNames[] = {"解锁火球", "解锁冰冻", "解锁箭雨", "解锁闪电"};
    for (size_t i = 0; i < _sandboxDraft.skillUnlocked.size(); ++i)
    {
        ImGui::Checkbox(skillNames[i], &_sandboxDraft.skillUnlocked[i]);
    }

    if (ImGui::Button("填充常用测试值"))
    {
        _sandboxDraft.coins    = 999999;
        _sandboxDraft.crystals = 999999;
        _sandboxDraft.highestClearedStage =
            std::max(0, static_cast<int>(_campaign.levels().size()) - 1);
        _sandboxDraft.highestRewardedStage = _sandboxDraft.highestClearedStage;
        for (size_t i = 0; i < _sandboxDraft.upgradeLevels.size(); ++i)
        {
            _sandboxDraft.upgradeLevels[i] = data.upgradeDefinition(static_cast<UpgradeId>(i)).maxLevel();
        }
        _sandboxDraft.skillUnlocked.fill(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("重新读取存档"))
    {
        syncSandboxDraft();
    }
    if (ImGui::Button("应用到存档"))
    {
        _sandboxDraft.highestClearedStage =
            std::clamp(_sandboxDraft.highestClearedStage, 0,
                       static_cast<int>(_campaign.levels().size()));
        _sandboxDraft.highestRewardedStage =
            std::clamp(_sandboxDraft.highestRewardedStage, 0,
                       _sandboxDraft.highestClearedStage);
        data.debugApplyPlayerData(_sandboxDraft);
        syncSandboxDraft();
        _wallMaxHP = data.wallMaxHealth();
        _wallHP = std::min(_wallHP, _wallMaxHP);
        refreshEconomyHUD();
        refreshSkillHUD();
    }
    ImGui::SameLine();
    if (ImGui::Button("恢复初始存档..."))
    {
        ImGui::OpenPopup("确认恢复初始存档");
    }

    if (ImGui::BeginPopupModal("确认恢复初始存档", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("金币、水晶、关卡进度和全部升级会恢复初始值。\n本局临时击杀收益不受影响。");
        if (ImGui::Button("确认恢复"))
        {
            data.debugResetPlayerData();
            syncSandboxDraft();
            _wallMaxHP = data.wallMaxHealth();
            _wallHP = std::min(_wallHP, _wallMaxHP);
            refreshEconomyHUD();
            refreshSkillHUD();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("取消"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
#endif  // CG_DEBUG_TOOLS（调试层整块结束）

// ---------------------------------------------------------------------------
// 对象池初始化：开局一次性创建 ARROW_POOL_SIZE 支真实箭，先放在场景池中。
// 为什么第一天就上对象池：游戏里箭矢每秒创建/销毁几十次，若每次都 new 节点
// 再 removeFromParent，内存分配抖动会造成帧率毛刺；池化后装填/发射只切换父节点，
// 箭对象本身从弓上一路复用到飞行与回收，不再创建第二套表现。
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
    // init 顺序中弓先于对象池创建；池建好后立即把真实箭挂到已有弓上。
    refreshWallBowLoadedArrows();
}

// ---------------------------------------------------------------------------
// 发射一支箭：向量数学是这里的主角——
//   方向向量 dir = (target - archer).normalize()  （指向目标、长度为 1）
//   速度向量 velocity = dir * ARROW_SPEED          （方向 × 速率）
//   每帧积分   position += velocity * delta         （欧拉积分，游戏物理的根基）
// ---------------------------------------------------------------------------
PlayerCombatStats MainScene::currentCombatStats() const
{
    const auto& data = GameData::getInstance();
    PlayerCombatStats base;
    base.arrowDamage    = data.arrowDamage();
    const float modeFireInterval = _gameMode == GameMode::Campaign ? CAMPAIGN_FIRE_INTERVAL
                                                                   : FAST_MODE_FIRE_INTERVAL;
    base.fireInterval   = modeFireInterval * data.fireIntervalMultiplier();
    base.multiShotCount = data.multiShotCount();
    return _gameMode == GameMode::Endless ? _runBuffs.resolve(base) : base;
}

void MainScene::reparentArrowNode(Arrow& arrow, Node* parent, int localZOrder)
{
    if (!arrow.node || !parent)
    {
        return;
    }
    if (arrow.node->getParent() == parent)
    {
        arrow.node->setLocalZOrder(localZOrder);
        return;
    }

    // removeChild 会 release；临时 retain 保证换父节点期间对象池裸指针仍然有效。
    arrow.node->retain();
    arrow.node->removeFromParentAndCleanup(false);
    parent->addChild(arrow.node, localZOrder);
    arrow.node->release();
}

void MainScene::parkArrow(Arrow& arrow)
{
    if (!arrow.node)
    {
        return;
    }
    reparentArrowNode(arrow, this, 3);
    arrow.active = false;
    arrow.loaded = false;
    arrow.loadedShotIndex = -1;
    arrow.node->setScale(1.0f);
    arrow.node->setVisible(false);
}

void MainScene::refreshWallBowLoadedArrows()
{
    if (!_wallBowArrowMount || _fireCooldown > 0.0f)
    {
        return;
    }

    const int count = std::clamp(currentCombatStats().multiShotCount, 1, MAX_MULTI_SHOT_COUNT);
    int loadedCount = 0;
    for (const auto& arrow : _arrowPool)
    {
        loadedCount += arrow.loaded ? 1 : 0;
    }
    if (loadedCount == count)
    {
        _wallBowArrowMount->setVisible(true);
        for (auto& arrow : _arrowPool)
        {
            if (arrow.loaded)
            {
                arrow.node->setVisible(true);
            }
        }
        return;
    }

    // 属性变化时整组重新装填，保证序号、间距和角度始终与真实弹道一一对应。
    _wallBowArrowMount->setVisible(false);
    for (auto& arrow : _arrowPool)
    {
        if (arrow.loaded)
        {
            parkArrow(arrow);
        }
    }

    // 先预留完整一组。槽位不足时保持空弓等待，绝不部分装填或拆散一次齐射。
    std::array<Arrow*, MAX_MULTI_SHOT_COUNT> reserved{};
    int reservedCount = 0;
    for (auto& arrow : _arrowPool)
    {
        if (!arrow.active && !arrow.loaded)
        {
            reserved[reservedCount++] = &arrow;
            if (reservedCount == count)
            {
                break;
            }
        }
    }
    if (reservedCount != count)
    {
        return;
    }

    const float bowScale = _wallBowWeapon ? _wallBowWeapon->getScaleX() : 1.0f;
    for (int shot = 0; shot < count; ++shot)
    {
        auto& arrow = *reserved[shot];
        const float centeredShot = shot - (count - 1) * 0.5f;
        const float offset = centeredShot * MULTI_SHOT_SPREAD;
        const Vec2 localDirection(-sinf(offset), cosf(offset));
        reparentArrowNode(arrow, _wallBowArrowMount, shot);
        arrow.active = false;
        arrow.loaded = true;
        arrow.loadedShotIndex = shot;
        arrow.node->setPosition(localDirection * ARROW_SPAWN_FORWARD_OFFSET +
                                Vec2(-centeredShot * BOW_MULTI_ARROW_SPACING, 0.0f));
        // 对象池箭几何朝 +X；旋转到弓的局部 +Y，再叠加多重箭扇形偏角。
        arrow.node->setRotation(-90.0f - offset * RAD2DEG);
        arrow.node->setScale(bowScale > 0.0f ? 1.0f / bowScale : 1.0f);
        arrow.node->setVisible(true);
    }
    // 所有真实箭都挂好以后才一次显示，渲染侧不会看到装填中的半成品。
    _wallBowArrowMount->setVisible(true);
}

void MainScene::updateWallBowAim(const Vec2& target)
{
    if (!_wallBowWeapon)
    {
        return;
    }
    const Vec2 direction = target - _archerPos;
    if (direction.lengthSquared() < 1.0f)
    {
        return;
    }
    const float targetAngle = atan2f(direction.y, direction.x) * RAD2DEG;
    // 弓图形的局部 +Y 为箭头方向；Axmol 节点正旋转为顺时针，因此换算为 90°-目标角。
    _wallBowWeapon->setRotation(90.0f - targetAngle);
}

void MainScene::playWallBowFireAnimation()
{
    if (!_wallBowVisual)
    {
        return;
    }

    // 每轮发射从同一释放姿态开始；停止上一轮余震，保证高射速下仍逐箭清晰回弹。
    _wallBowVisual->stopAllActions();
    _wallBowVisual->setScale(1.14f, 0.82f);
    _wallBowVisual->runAction(Sequence::create(
        ScaleTo::create(0.055f, 0.94f, 1.10f),
        ScaleTo::create(0.105f, 1.0f, 1.0f),
        nullptr));
}

void MainScene::fireArrow(const ax::Vec2& target)
{
    Vec2 dir = target - _archerPos;
    if (dir.lengthSquared() < 1.0f)
    {
        return;  // 点就点在箭手位上，无方向可射
    }
    // 先转弓、再检查射速；长按拖动时即使处于两箭之间，武器也能逐帧跟随瞄准。
    updateWallBowAim(target);
    // 射速上限：冷却没转好就忽略这次点击（手感调优清单第 4 项）
    if (_fireCooldown > 0.0f)
    {
        return;
    }
    dir.normalize();

    const auto stats = currentCombatStats();
    const int count  = std::clamp(stats.multiShotCount, 1, MAX_MULTI_SHOT_COUNT);
    const float baseAngle = atan2f(dir.y, dir.x);
    refreshWallBowLoadedArrows();

    // 发射同样采用两阶段提交：先确认整组齐全，再改变任何箭的父节点与状态。
    std::array<Arrow*, MAX_MULTI_SHOT_COUNT> volley{};
    for (auto& candidate : _arrowPool)
    {
        if (candidate.loaded && candidate.loadedShotIndex >= 0 && candidate.loadedShotIndex < count)
        {
            volley[candidate.loadedShotIndex] = &candidate;
        }
    }
    for (int shot = 0; shot < count; ++shot)
    {
        if (!volley[shot])
        {
            AXLOGD("[ContinuousFire] waiting for full volley: required={}, poolCapacity={}",
                   count, ARROW_POOL_SIZE);
            return;
        }
    }

    int fired = 0;
    for (int shot = 0; shot < count; ++shot)
    {
        Arrow* slot = volley[shot];

        const float centeredShot = shot - (count - 1) * 0.5f;
        const float offset = centeredShot * MULTI_SHOT_SPREAD;
        const float angle  = baseAngle + offset;
        const Vec2 shotDir(cosf(angle), sinf(angle));
        const Vec2 worldPosition = slot->node->convertToWorldSpace(Vec2::ZERO);
        reparentArrowNode(*slot, this, 3);
        slot->node->setPosition(convertToNodeSpace(worldPosition));
        const float criticalRoll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
        slot->critical = criticalRoll < stats.criticalChance;
        slot->active   = true;
        slot->loaded   = false;
        slot->loadedShotIndex = -1;
        slot->damage   = slot->critical
                             ? static_cast<int>(std::ceil(stats.arrowDamage * stats.criticalDamage))
                             : stats.arrowDamage;
        slot->knockbackDistance = stats.knockbackDistance;
        slot->velocity = shotDir * ARROW_SPEED;
        slot->node->setRotation(-angle * RAD2DEG);
        slot->node->setScale(slot->critical ? 1.35f : 1.0f);
        slot->node->setVisible(true);
        ++fired;
    }
    if (fired > 0)
    {
        playWallBowFireAnimation();
        _fireCooldown = stats.fireInterval;
    }
    else
    {
        AXLOGD("[ContinuousFire] volley was not fired: activeCapacity={}", ARROW_POOL_SIZE);
    }
}

void MainScene::beginContinuousFire(ax::Touch* touch)
{
    if (!touch || _continuousFireActive || _gameState != GameState::update)
    {
        return;
    }
    _continuousFireActive  = true;
    _continuousFireTouchId = touch->getID();
    _continuousFireTarget  = touch->getLocation();
    AXLOGD("[ContinuousFire] hold started: touchId={}, target=({:.1f}, {:.1f})",
           _continuousFireTouchId, _continuousFireTarget.x, _continuousFireTarget.y);
    fireArrow(_continuousFireTarget);  // 点按仍然立即射出第一轮，不增加操作延迟
}

void MainScene::stopContinuousFire(int touchId)
{
    if (!_continuousFireActive || (touchId >= 0 && touchId != _continuousFireTouchId))
    {
        return;
    }
    AXLOGD("[ContinuousFire] hold stopped: touchId={}", _continuousFireTouchId);
    _continuousFireActive  = false;
    _continuousFireTouchId = -1;
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
    GameData::getInstance().recordKill();
    refreshEconomyHUD();
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
bool MainScene::damageMonster(Monster& m, int dmg)
{
    if (m.state == MonsterState::Dead || dmg <= 0)
    {
        return false;
    }
    // 护符怪的一次性全能护盾：挡【任何】伤害（箭和技能都挡）。挡下后重画皮，
    // 金环消失——"盾破了"必须有即时视觉反馈，玩家才知道下一击能杀
    if (m.shieldCharges > 0)
    {
        m.shieldCharges--;
        spawnFx(0, m.node->getPosition() + Vec2(0, m.bodyR), m.bodyR + 10.0f, 0.2f);  // 碎盾闪光
        applyMonsterSkin(m);
        return false;
    }
    m.hp -= dmg;
    if (m.hp <= 0)
    {
        killMonster(m);
    }
    return true;
}

void MainScene::applyKnockback(Monster& m, float sourceDistance)
{
    if (m.state == MonsterState::Dead)
    {
        return;
    }
    const float distance = _monsterBalance.resolveKnockbackDistance(m.type, sourceDistance);
    Vec2 away = m.node->getPosition() - m.targetPos;
    if (distance <= 0.0f || away.lengthSquared() < 1.0f)
    {
        return;
    }
    away.normalize();
    m.knockbackDirection = away;
    m.knockbackRemaining = std::min(MAX_QUEUED_KNOCKBACK,
                                    m.knockbackRemaining + distance);
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
                    // 箭的伤害与击退都在发射时固化。护符挡伤时不触发击退；
                    // 击杀后也无需排位移，存活目标才进入平滑击退队列。
                    if (damageMonster(m, a.damage) && m.state != MonsterState::Dead)
                    {
                        applyKnockback(m, a.knockbackDistance);
                    }
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
    AXLOGI("[MonsterBreath] initialized: pool={}, scaleX={}, scaleY={}, speed=[{},{}]",
           MONSTER_POOL_SIZE, MONSTER_BREATH_SCALE_X, MONSTER_BREATH_SCALE_Y,
           MONSTER_BREATH_SPEED_MIN, MONSTER_BREATH_SPEED_MAX);
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
    m.knockbackDirection = Vec2::ZERO;
    m.knockbackRemaining = 0.0f;
    m.windupTimer   = 0.0f;   // 攻击前摇复位（飞行怪/Boss）
    m.hp            = 1;
    m.shieldCharges = 0;      // 一次性护盾复位（护符怪在类型表里给 1 层）
    m.ghostHidden   = false;  // 幽灵状态复位
    m.ghostTimer    = GHOST_SHOW_TIME;
    m.type          = type;
    // 出生时固化当前数值：开发者工具调参只影响之后生成的怪物，场上对象不会突变。
    const auto& parameters = _monsterBalance.parameters(type);
    const float r2 = static_cast<float>(rand()) / RAND_MAX;  // 个体速度随机源
    const float breathPhaseSeed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    const float breathSpeedSeed = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    m.hp          = parameters.maxHp;
    m.maxHp       = parameters.maxHp;
    m.actionPower = parameters.actionPower;
    m.speed       = _monsterBalance.baseSpeed() * parameters.speedMultiplier * (0.85f + r2 * 0.3f);
    m.bodyR       = _monsterBalance.baseBodyRadius() * parameters.bodyScale;
    m.breathPhase = breathPhaseSeed * 6.2831853f;
    m.breathSpeed = MONSTER_BREATH_SPEED_MIN +
                    breathSpeedSeed * (MONSTER_BREATH_SPEED_MAX - MONSTER_BREATH_SPEED_MIN);
    m.node->setScale(1.0f);  // 对象池复用时清掉上一只怪停留的呼吸缩放
    if (type == MonsterType::Aegis)
    {
        m.shieldCharges = 1;
    }
    const float markerScale = m.bodyR / MONSTER_BODY_R;
    m.slowMark->setScale(markerScale);
    m.stunMark->setScale(markerScale);
    m.windupMark->setScale(markerScale);
    // 开火距离与攻击间隔：近战贴墙啃；远程/治疗/召唤按战场纵深比例停在各自的
    // 功能位（比例而非写死像素，横竖屏都成立）；飞行怪/Boss 进场后开始游荡
    const float fieldDepth = _director->getVisibleSize().height * (SPAWN_RATIO - WALL_RATIO);
    if (parameters.stopRatioMax > 0.0f)
    {
        const float t = static_cast<float>(rand()) / RAND_MAX;
        const float stopRatio = parameters.stopRatioMin +
                                t * (parameters.stopRatioMax - parameters.stopRatioMin);
        m.attackRange = fieldDepth * stopRatio;
    }
    else
    {
        m.attackRange = 6.0f;
    }
    m.attackInterval = parameters.actionInterval;
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
    if (type == MonsterType::Boss)
    {
        AXLOGI("[CombatCore] boss spawned: hp={}, knockbackFactor={}", m.maxHp,
               parameters.knockbackFactor);
    }
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
// 五关战役：CampaignConfig 负责结构与校验，场景只持有当前关的波次运行时。
// 状态机：Idle → Spawning（按组出怪）→ Resting（清场休整）→ 下一波 → AllClear。
// ---------------------------------------------------------------------------
bool MainScene::loadCampaign()
{
    const std::string text = FileUtils::getInstance()->getStringFromFile("config/waves.json");
    std::string error;
    const bool loaded = _campaign.loadFromJson(text, &error);
    if (!loaded)
    {
        _campaign.useFallback();
        AXLOGW("[Campaign] config invalid, using fallback: {}", error);
    }

    for (size_t i = 0; i < _campaign.levels().size(); ++i)
    {
        const auto& level = _campaign.levels()[i];
        AXLOGI("[Campaign] level configured: stage={}, waves={}, enemies={}, crystals={}", i + 1,
               level.waves.size(), level.configuredEnemyCount(), level.clearCrystals);
    }

#if CG_DEBUG_TOOLS
    const std::string coreText = FileUtils::getInstance()->getStringFromFile("config/core-waves.json");
    std::string coreError;
    const bool coreLoaded = _coreBattle.loadFromJson(coreText, &coreError);
    if (!coreLoaded)
    {
        _coreBattle = _campaign;
        AXLOGW("[DevelopmentMenu][Core] baseline config invalid, using campaign fallback: {}",
               coreError);
    }
    else
    {
        const auto& core = _coreBattle.levels().front();
        AXLOGI("[DevelopmentMenu][Core] baseline configured: waves={}, enemies={}",
               core.waves.size(), core.configuredEnemyCount());
    }
    return loaded && coreLoaded;
#else
    return loaded;
#endif
}

const LevelConfig& MainScene::currentLevel() const
{
    return _campaign.levels()[_levelIndex];
}

void MainScene::activateLevel(size_t index, bool announceUnlocks)
{
    _levelIndex = std::min(index, _campaign.levels().size() - 1);
    _waves      = currentLevel().waves;
    _waveIndex = 0;
    _waveState = WaveState::Idle;
    _groupRt.clear();

    auto unlocked = GameData::getInstance().beginStage(static_cast<int>(_levelIndex) + 1);
    refreshSkillHUD();
    if (announceUnlocks)
    {
        showSkillUnlocks(unlocked);
    }
    AXLOGI("[Campaign] level activated: stage={}/{}, name={}, waves={}", _levelIndex + 1,
           _campaign.levels().size(), currentLevel().name, _waves.size());
}

void MainScene::prepareEndlessWave(int waveNumber)
{
    std::vector<const WaveConfig*> templates;
    for (const auto& level : _campaign.levels())
    {
        for (const auto& wave : level.waves)
        {
            templates.push_back(&wave);
        }
    }
    if (templates.empty())
    {
        _waves.clear();
        return;
    }

    const int safeWave = std::max(1, waveNumber);
    const size_t templateIndex = static_cast<size_t>(safeWave - 1) % templates.size();
    const int cycle = (safeWave - 1) / static_cast<int>(templates.size());
    WaveConfig wave = *templates[templateIndex];
    wave.name = StringUtils::format("无尽第 %d 波", safeWave);
    wave.restAfter = 2.0f;
    const float intervalScale = std::pow(0.93f, static_cast<float>(cycle));
    for (auto& group : wave.groups)
    {
        group.count += cycle * std::max(1, group.count / 3);
        group.interval = std::max(0.18f, group.interval * intervalScale);
    }
    _waves = {std::move(wave)};
    _waveIndex = 0;
    _waveState = WaveState::Idle;
    _groupRt.clear();
    _endlessWaveNumber = safeWave;
    AXLOGI("[GameModes][Endless] wave prepared: wave={}, template={}, cycle={}", safeWave,
           templateIndex + 1, cycle);
}

void MainScene::advanceEndlessWave()
{
    prepareEndlessWave(_endlessWaveNumber + 1);
    _gameState = GameState::update;
}

void MainScene::showSkillUnlocks(const std::vector<SkillId>& skills)
{
    if (skills.empty() || !_skillHint)
    {
        return;
    }
    std::string names;
    for (const auto id : skills)
    {
        if (!names.empty()) names += "、";
        names += GameData::getInstance().skillDefinition(id).name;
    }
    _skillHint->setString("新技能解锁：" + names + "！");
    _skillHint->setVisible(true);
    _skillHintTimer = 4.0f;
}

void MainScene::beginWave(size_t index)
{
    // 当前累计收益成为本波起点；旋转/切视角重播本波时可以安全回滚到这里。
    GameData::getInstance().checkpointRun();
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
        if (_gameMode == GameMode::Endless)
        {
            _waveLabel->setString(w.name);
        }
        else if (_gameMode == GameMode::Core)
        {
            _waveLabel->setString("核心验证 · " + w.name);
        }
        else
        {
            _waveLabel->setString(currentLevel().name + " · " + w.name);
        }
    }
    if (_gameMode == GameMode::Endless)
    {
        AXLOGI("[GameModes][Endless] wave started: wave={}, completed={}", _endlessWaveNumber,
               _endlessCompletedWaves);
    }
    else if (_gameMode == GameMode::Campaign)
    {
        AXLOGI("[Campaign] wave started: stage={}/{}, wave={}/{}, name={}", _levelIndex + 1,
               _campaign.levels().size(), index + 1, _waves.size(), w.name);
    }
    else
    {
        AXLOGI("[DevelopmentMenu][Core] wave started: wave={}/{}, name={}", index + 1,
               _waves.size(), w.name);
    }
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
            if (_gameMode == GameMode::Endless)
            {
                _waveLabel->setString(StringUtils::format("无尽第 %d 波完成", _endlessWaveNumber));
            }
            else if (_gameMode == GameMode::Core)
            {
                _waveLabel->setString(StringUtils::format("核心验证 · 下一波来袭 %d",
                                                           static_cast<int>(_restTimer) + 1));
            }
            else
            {
                _waveLabel->setString(StringUtils::format("第 %d/%d 关 · 下一波来袭 %d",
                                                           static_cast<int>(_levelIndex) + 1,
                                                           static_cast<int>(_campaign.levels().size()),
                                                           static_cast<int>(_restTimer) + 1));
            }
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
                if (_gameMode == GameMode::Endless)
                {
                    ++_endlessCompletedWaves;
                    AXLOGI("[GameModes][Endless] wave cleared: wave={}, completed={}, kills={}",
                           _endlessWaveNumber, _endlessCompletedWaves,
                           GameData::getInstance().economy().runKills());
                    if (_endlessCompletedWaves % 3 == 0)
                    {
                        offerRunBuff();
                    }
                    else
                    {
                        advanceEndlessWave();
                    }
                    break;
                }
                if (_gameMode == GameMode::Core)
                {
                    if (_waveLabel) _waveLabel->setString("核心玩法验证完成！");
                    AXLOGI("[DevelopmentMenu][Core] all waves cleared: waves={}, kills={}",
                           _waves.size(), GameData::getInstance().economy().runKills());
                    showSettlement(true);
                    break;
                }
                if (_waveLabel)
                {
                    _waveLabel->setString(StringUtils::format("第 %d 关守住了！",
                                                               static_cast<int>(_levelIndex) + 1));
                }
                AXLOGI("[Campaign] level cleared: stage={}/{}, kills={}", _levelIndex + 1,
                       _campaign.levels().size(), GameData::getInstance().economy().runKills());
                showSettlement(true);
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
    const auto visibleSize = _director->getVisibleSize();
    const auto origin = _director->getVisibleOrigin();
    for (auto& m : _monsterPool)
    {
        if (!m.active)
        {
            continue;
        }
        // 程序化呼吸：正弦曲线驱动轻微 squash-and-stretch。只改变绘制缩放，
        // m.bodyR 与圆形命中判定保持不变，因此这是纯表现动画。
        m.breathPhase += m.breathSpeed * delta;
        if (m.breathPhase >= 6.2831853f)
        {
            m.breathPhase -= 6.2831853f;
        }
        const float breath = sinf(m.breathPhase);
        m.node->setScale(1.0f - MONSTER_BREATH_SCALE_X * breath,
                         1.0f + MONSTER_BREATH_SCALE_Y * breath);
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
        // 击退是独立于 Walk/Attack 的短位移层：每帧先向远离城墙方向结算一小段，
        // 再让原状态机继续行动。这样不会瞬移，也不会额外制造一套 AI 状态。
        if (m.knockbackRemaining > 0.0f)
        {
            const float step = std::min(m.knockbackRemaining, KNOCKBACK_TRAVEL_SPEED * delta);
            Vec2 pushed = m.node->getPosition() + m.knockbackDirection * step;
            pushed.x = std::clamp(pushed.x, origin.x + m.bodyR,
                                  origin.x + visibleSize.width - m.bodyR);
            pushed.y = std::clamp(pushed.y, origin.y,
                                  origin.y + visibleSize.height - m.bodyR * 2.0f);
            m.node->setPosition(pushed);
            m.knockbackRemaining -= step;

            // 贴墙/驻足单位被推离自己的攻击距离后，需要重新走回阵位。
            // 飞行怪与 Boss 的 Attack 本身包含自由游荡，不切回 Walk。
            if (m.state == MonsterState::Attack && m.type != MonsterType::Flyer &&
                m.type != MonsterType::Boss &&
                (m.targetPos - pushed).length() > m.attackRange + 2.0f)
            {
                m.state = MonsterState::Walk;
                m.attackTimer = 0.0f;
            }
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
                    damageWall(m.actionPower);
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
                                      m.actionPower);
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
                            fireEnemyBolt(from, laneWallPos(m.lane) + Vec2(off, 0), m.actionPower);
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
                            o.hp = std::min(o.maxHp, o.hp + m.actionPower);
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
                    for (int i = 0; i < m.actionPower; ++i)
                    {
                        spawnMinion(MonsterType::Normal, at + Vec2(8.0f * i, 0.0f));
                    }
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
                                  m.actionPower);
                }
                else
                {
                    damageWall(m.actionPower);  // 近战：直接啃
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
// 开关卡：清场（三池全收）+ 城墙满血 + 回到该关第 1 波。
// 注意全程没有 new/delete——对象本来就是池化的，"重置"只是改状态位，
// 这也是对象池的红利之一：重开一局是 O(池大小) 的零分配操作
// ---------------------------------------------------------------------------
void MainScene::clearField()
{
    stopContinuousFire();
    for (auto& a : _arrowPool)
    {
        parkArrow(a);
    }
    for (auto& m : _monsterPool)
    {
        m.active = false;
        m.node->setScale(1.0f);
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
    if (_gameMode == GameMode::Endless)
    {
        startEndlessRun();
        return;
    }
    if (_gameMode == GameMode::Core)
    {
        startCoreRun();
        return;
    }
    startCampaignRun();
}

void MainScene::showEntryMenu()
{
#if CG_DEBUG_TOOLS
    showDevelopmentMenu();
#else
    showModeSelect();
#endif
}

void MainScene::showModeSelect()
{
    stopContinuousFire();
    clearField();
    _settlementVisible = false;
    _settlementLayer->setVisible(false);
    _upgradePanel->setVisible(false);
    _choicePanel->setVisible(false);
    _campaignMenuPanel->setVisible(false);
    _roguelikeMenuPanel->setVisible(false);
    _modeSelectPanel->setVisible(true);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->setVisible(false);
#endif
    _runBuffLabel->setVisible(false);
    _gameState = GameState::menu1;
    if (_waveLabel) _waveLabel->setString("");
    AXLOGI("[GameModes] selection opened");
}

void MainScene::showCampaignMenu()
{
    stopContinuousFire();
    clearField();
    _gameMode = GameMode::Campaign;
    _settlementVisible = false;
    _settlementLayer->setVisible(false);
    _upgradePanel->setVisible(false);
    _choicePanel->setVisible(false);
    _campaignMenuPanel->setVisible(false);
    _roguelikeMenuPanel->setVisible(false);
    _modeSelectPanel->setVisible(false);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->setVisible(false);
#endif
    _campaignMenuPanel->open(static_cast<int>(_campaign.levels().size()));
    _runBuffLabel->setVisible(false);
    _gameState = GameState::menu1;
    if (_waveLabel) _waveLabel->setString("");
    AXLOGI("[ModeNavigation] home opened: mode=campaign");
}

void MainScene::showRoguelikeMenu()
{
    stopContinuousFire();
    clearField();
    _gameMode = GameMode::Endless;
    _settlementVisible = false;
    _settlementLayer->setVisible(false);
    _upgradePanel->setVisible(false);
    _choicePanel->setVisible(false);
    _campaignMenuPanel->setVisible(false);
    _roguelikeMenuPanel->setVisible(false);
    _modeSelectPanel->setVisible(false);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->setVisible(false);
#endif
    _roguelikeMenuPanel->open(static_cast<int>(_campaign.levels().size()));
    _runBuffLabel->setVisible(false);
    _gameState = GameState::menu1;
    if (_waveLabel) _waveLabel->setString("");
    AXLOGI("[ModeNavigation] home opened: mode=roguelike");
}

void MainScene::showCurrentModeMenu()
{
    if (_gameMode == GameMode::Campaign)
    {
        showCampaignMenu();
    }
    else if (_gameMode == GameMode::Endless)
    {
        showRoguelikeMenu();
    }
    else
    {
        showEntryMenu();
    }
}

#if CG_DEBUG_TOOLS
void MainScene::showDevelopmentMenu()
{
    stopContinuousFire();
    clearField();
    _settlementVisible = false;
    _settlementLayer->setVisible(false);
    _upgradePanel->setVisible(false);
    _choicePanel->setVisible(false);
    _campaignMenuPanel->setVisible(false);
    _roguelikeMenuPanel->setVisible(false);
    _modeSelectPanel->setVisible(false);
    _developmentMenuPanel->setVisible(true);
    _runBuffLabel->setVisible(false);
    _gameState = GameState::menu1;
    if (_waveLabel) _waveLabel->setString("");
    AXLOGI("[DevelopmentMenu] opened");
}

void MainScene::selectDevelopmentMode(DevelopmentMenuEntry entry)
{
    _developmentMenuPanel->setVisible(false);
    switch (entry)
    {
    case DevelopmentMenuEntry::Core:
        AXLOGI("[DevelopmentMenu] selected: mode=core");
        selectMode(GameMode::Core);
        break;
    case DevelopmentMenuEntry::Campaign:
        AXLOGI("[DevelopmentMenu] selected: mode=campaign");
        selectMode(GameMode::Campaign);
        break;
    case DevelopmentMenuEntry::Roguelike:
        AXLOGI("[DevelopmentMenu] selected: mode=roguelike");
        selectMode(GameMode::Endless);
        break;
    }
}
#endif

void MainScene::selectMode(GameMode mode)
{
    _gameMode = mode;
    _modeSelectPanel->setVisible(false);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->setVisible(false);
#endif
    const char* modeName = mode == GameMode::Core ? "core"
                           : mode == GameMode::Endless ? "endless"
                                                        : "campaign";
    AXLOGI("[GameModes] selected: mode={}", modeName);
    if (mode == GameMode::Endless)
    {
        showRoguelikeMenu();
        return;
    }
    _runBuffs.reset();
    refreshRunBuffHUD();
    if (mode == GameMode::Core)
    {
        startCoreRun();
        return;
    }
    showCampaignMenu();
}

void MainScene::beginCampaignBattle()
{
    AXLOGI("[ModeNavigation] battle requested: mode=campaign");
    _runBuffs.reset();
    refreshRunBuffHUD();
    const int pendingStage = GameData::getInstance().pendingCampaignRewardStage();
    if (pendingStage > 0)
    {
        offerCampaignReward(pendingStage, true);
        return;
    }
    startCampaignRun();
}

void MainScene::startCoreRun()
{
    _runBuffs.reset();
    resetBattleForRun();
    _waves.clear();
    const auto& sourceLevels = _coreBattle.empty() ? _campaign.levels() : _coreBattle.levels();
    for (const auto& level : sourceLevels)
    {
        _waves.insert(_waves.end(), level.waves.begin(), level.waves.end());
    }
    _levelIndex = 0;
    _waveIndex  = 0;
    _waveState  = WaveState::Idle;
    _groupRt.clear();
    refreshRunBuffHUD();
    AXLOGI("[DevelopmentMenu][Core] run started: waves={}, wallHP={}, arrowDamage={}, multiShot={}, "
           "fireInterval={:.3f}", _waves.size(), _wallHP, GameData::getInstance().arrowDamage(),
           GameData::getInstance().multiShotCount(), currentCombatStats().fireInterval);
}

void MainScene::startCampaignRun()
{
    const int stage = GameData::getInstance().nextStage(static_cast<int>(_campaign.levels().size()));
    startLevel(static_cast<size_t>(stage - 1));
}

void MainScene::resetBattleForRun()
{
    stopContinuousFire();
    clearField();
    _wallMaxHP = GameData::getInstance().wallMaxHealth();
    _wallHP    = _wallMaxHP;
    _mana      = MANA_MAX;  // 技能系统：蓝量冷却一并复位
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
        _cdRemaining[i] = 0.0f;
    }
    _aimingSkill = -1;
    _aimDragging = false;
    _fireCooldown = 0.0f;
    _aimIndicator->setVisible(false);
    _rainQueue.clear();
    _settlementVisible = false;
    _settlementLayer->setVisible(false);
    _upgradePanel->setVisible(false);
    _choicePanel->setVisible(false);
    _campaignMenuPanel->setVisible(false);
    _roguelikeMenuPanel->setVisible(false);
    _modeSelectPanel->setVisible(false);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->setVisible(false);
#endif
    _skillHint->setVisible(false);
    _lastCampaignRewardName.clear();
    _gameState = GameState::update;
    GameData::getInstance().startRun();
    refreshEconomyHUD();
    if (_waveLabel)
    {
        _waveLabel->setString("");
    }
}

void MainScene::startLevel(size_t index)
{
    resetBattleForRun();
    activateLevel(index, true);
    _waveState = WaveState::Idle;  // 下一帧 updateWaves 自动 beginWave(0)
    refreshRunBuffHUD();
    AXLOGI("[Campaign] run started: stage={}/{}, wallHP={}, arrowDamage={}, multiShot={}, "
           "fireInterval={:.3f}", _levelIndex + 1, _campaign.levels().size(), _wallHP,
           GameData::getInstance().arrowDamage(), GameData::getInstance().multiShotCount(),
           currentCombatStats().fireInterval);
}

void MainScene::startEndlessRun()
{
    _runBuffs.reset();
    _endlessWaveNumber     = 1;
    _endlessCompletedWaves = 0;
    resetBattleForRun();
    prepareEndlessWave(1);
    refreshRunBuffHUD();
    AXLOGI("[GameModes][Endless] run started: wallHP={}, arrowDamage={}, multiShot={}, fireInterval={:.3f}",
           _wallHP, GameData::getInstance().arrowDamage(), GameData::getInstance().multiShotCount(),
           currentCombatStats().fireInterval);
}

#if CG_DEBUG_TOOLS
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
    _wallMaxHP = GameData::getInstance().wallMaxHealth();
    _wallHP    = _wallMaxHP;
    beginWave(static_cast<size_t>(next));
    AXLOGI("[Campaign] sandbox wave jump: stage={}, wave={}", _levelIndex + 1, next + 1);
}

void MainScene::sandboxGotoLevel(int offset)
{
    _gameMode = GameMode::Campaign;
    _runBuffs.reset();
    const int next = std::clamp(static_cast<int>(_levelIndex) + offset, 0,
                                static_cast<int>(_campaign.levels().size()) - 1);
    startLevel(static_cast<size_t>(next));
    AXLOGI("[Campaign] sandbox level jump: stage={}/{}", next + 1, _campaign.levels().size());
}
#endif  // CG_DEBUG_TOOLS

// ---------------------------------------------------------------------------
// 城墙扣血统一入口：近战啃咬和远程弹丸都走这里，城破判定只有一处
// ---------------------------------------------------------------------------
void MainScene::damageWall(int dmg)
{
#if CG_DEBUG_TOOLS
    if (_sandboxInvincibleWall)
    {
        return;
    }
#endif
    _wallHP = std::max(0, _wallHP - dmg);
    refreshCombatHUD();
    AXLOGD("城墙血量：{}，击杀：{}", _wallHP, GameData::getInstance().economy().runKills());
    if (_wallHP == 0)
    {
        // 城破后波次状态机冻结（updateWaves 守卫），给个肉眼可见的提示
        if (_waveLabel)
        {
            _waveLabel->setString("城墙告破！点 ↻ 重开");
        }
        AXLOGI("[Campaign] wall destroyed: entering defeat settlement");
        showSettlement(false);
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

// ---------------------------------------------------------------------------
// 经济 HUD 与结算层。这里只展示 GameData 的结果，奖励规则不在 UI 中重复计算。
// ---------------------------------------------------------------------------
void MainScene::initEconomyUI()
{
    _coinLabel = Label::createWithSystemFont("", "Arial", 26);
    _coinLabel->setAnchorPoint(Vec2(1.0f, 1.0f));
    _coinLabel->setTextColor(Color4B(255, 220, 95, 255));
    addChild(_coinLabel, 5);

    _crystalLabel = Label::createWithSystemFont("", "Arial", 26);
    _crystalLabel->setAnchorPoint(Vec2(1.0f, 1.0f));
    _crystalLabel->setTextColor(Color4B(110, 225, 255, 255));
    addChild(_crystalLabel, 5);

    _settlementLayer = Node::create();
    _settlementLayer->setVisible(false);
    addChild(_settlementLayer, 20);

    _settlementBackdrop = DrawNode::create();
    _settlementLayer->addChild(_settlementBackdrop, 0);
    _settlementPanel = DrawNode::create();
    _settlementLayer->addChild(_settlementPanel, 1);

    _settlementTitle = Label::createWithSystemFont("", "Arial", 48);
    _settlementLayer->addChild(_settlementTitle, 2);
    _settlementRewards = Label::createWithSystemFont("", "Arial", 30);
    _settlementRewards->setAlignment(TextHAlignment::CENTER);
    _settlementLayer->addChild(_settlementRewards, 2);
    _settlementTotals = Label::createWithSystemFont("", "Arial", 25);
    _settlementTotals->setTextColor(Color4B(205, 215, 230, 255));
    _settlementLayer->addChild(_settlementTotals, 2);

    _settlementRestartBtn = Node::create();
    auto restartBg = DrawNode::create();
    restartBg->drawSolidRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                             Color4F(0.18f, 0.58f, 0.36f, 1.0f));
    restartBg->drawRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                        Color4F(0.75f, 1.0f, 0.84f, 1.0f), 3.0f);
    _settlementRestartBtn->addChild(restartBg);
    _settlementRestartLabel = Label::createWithSystemFont("再来一局", "Arial", 30);
    _settlementRestartLabel->setPosition(Vec2(SETTLEMENT_BUTTON_W * 0.5f, SETTLEMENT_BUTTON_H * 0.5f));
    _settlementRestartBtn->addChild(_settlementRestartLabel, 1);
    _settlementLayer->addChild(_settlementRestartBtn, 2);

    _settlementUpgradeBtn = Node::create();
    auto upgradeBg = DrawNode::create();
    upgradeBg->drawSolidRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                             Color4F(0.20f, 0.42f, 0.68f, 1.0f));
    upgradeBg->drawRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                        Color4F(0.76f, 0.90f, 1.0f, 1.0f), 3.0f);
    _settlementUpgradeBtn->addChild(upgradeBg);
    auto upgradeLabel = Label::createWithSystemFont("升级", "Arial", 30);
    upgradeLabel->setPosition(Vec2(SETTLEMENT_BUTTON_W * 0.5f, SETTLEMENT_BUTTON_H * 0.5f));
    _settlementUpgradeBtn->addChild(upgradeLabel, 1);
    _settlementLayer->addChild(_settlementUpgradeBtn, 2);

    _settlementModeBtn = Node::create();
    auto modeBg = DrawNode::create();
    modeBg->drawSolidRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                          Color4F(0.40f, 0.30f, 0.58f, 1.0f));
    modeBg->drawRect(Vec2::ZERO, Vec2(SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H),
                     Color4F(0.88f, 0.82f, 1.0f, 1.0f), 3.0f);
    _settlementModeBtn->addChild(modeBg);
    _settlementHomeLabel = Label::createWithSystemFont("返回主页", "Arial", 28);
    _settlementHomeLabel->setPosition(Vec2(SETTLEMENT_BUTTON_W * 0.5f, SETTLEMENT_BUTTON_H * 0.5f));
    _settlementModeBtn->addChild(_settlementHomeLabel, 1);
    _settlementLayer->addChild(_settlementModeBtn, 2);

    _upgradePanel = UpgradePanel::create();
    _upgradePanel->setStartRunCallback([this]() { resetGame(); });
    addChild(_upgradePanel, 30);

    _choicePanel = ThreeChoicePanel::create();
    _choicePanel->setChoiceCallback([this](size_t index) {
        if (_campaignRewards.hasPendingOffer())
        {
            chooseCampaignReward(_campaignRewards.choices()[index]);
        }
        else if (_runBuffs.hasPendingOffer())
        {
            chooseRunBuff(_runBuffs.choices()[index]);
        }
    });
    addChild(_choicePanel, 40);

    _modeSelectPanel = ModeSelectPanel::create();
    _modeSelectPanel->setChoiceCallback(
        [this](bool endless) { selectMode(endless ? GameMode::Endless : GameMode::Campaign); });
    addChild(_modeSelectPanel, 45);

    _campaignMenuPanel = ModeHomePanel::create(ModeHomeKind::Campaign);
    _campaignMenuPanel->setStartCallback([this]() { beginCampaignBattle(); });
    _campaignMenuPanel->setWorkshopCallback([this]() {
        _campaignMenuPanel->setVisible(false);
        _upgradePanel->setStartButtonText("返回闯关主页");
        _upgradePanel->setStartRunCallback([this]() { showCampaignMenu(); });
        _upgradePanel->open();
        AXLOGI("[ModeNavigation] campaign workshop opened");
    });
    addChild(_campaignMenuPanel, 43);

    _roguelikeMenuPanel = ModeHomePanel::create(ModeHomeKind::Roguelike);
    _roguelikeMenuPanel->setStartCallback([this]() {
        AXLOGI("[ModeNavigation] battle requested: mode=roguelike");
        startEndlessRun();
    });
    addChild(_roguelikeMenuPanel, 43);

#if CG_DEBUG_TOOLS
    _developmentMenuPanel = DevelopmentMenuPanel::create();
    _developmentMenuPanel->setChoiceCallback(
        [this](DevelopmentMenuEntry entry) { selectDevelopmentMode(entry); });
    addChild(_developmentMenuPanel, 46);
#endif

    _runBuffLabel = Label::createWithSystemFont("", "Arial", 21);
    _runBuffLabel->setTextColor(Color4B(250, 220, 125, 255));
    addChild(_runBuffLabel, 5);

    refreshEconomyHUD();
    refreshRunBuffHUD();
}

void MainScene::layoutEconomyUI(const Vec2& origin, float right, float top)
{
    if (_coinLabel)
    {
        _coinLabel->setPosition(Vec2(right - 18.0f, top - 18.0f));
        _crystalLabel->setPosition(Vec2(right - 18.0f, top - 54.0f));
    }
    if (!_settlementLayer)
    {
        return;
    }

    const auto visibleSize = _director->getVisibleSize();
    _upgradePanel->layout(origin, visibleSize);
    _choicePanel->layout(origin, visibleSize);
    _modeSelectPanel->layout(origin, visibleSize);
    _campaignMenuPanel->layout(origin, visibleSize);
    _roguelikeMenuPanel->layout(origin, visibleSize);
#if CG_DEBUG_TOOLS
    _developmentMenuPanel->layout(origin, visibleSize);
#endif
    _runBuffLabel->setPosition(Vec2(origin.x + visibleSize.width * 0.5f, top - 64.0f));
    const float panelW     = std::min(560.0f, visibleSize.width * 0.86f);
    const float panelH     = std::min(520.0f, visibleSize.height * 0.76f);
    const Vec2 center((origin.x + right) * 0.5f, origin.y + visibleSize.height * 0.5f);
    const Vec2 panelMin(center.x - panelW * 0.5f, center.y - panelH * 0.5f);
    const Vec2 panelMax(center.x + panelW * 0.5f, center.y + panelH * 0.5f);

    _settlementBackdrop->clear();
    _settlementBackdrop->drawSolidRect(origin, Vec2(right, top), Color4F(0.02f, 0.03f, 0.05f, 0.78f));
    _settlementPanel->clear();
    _settlementPanel->drawSolidRect(panelMin, panelMax, Color4F(0.10f, 0.12f, 0.17f, 0.98f));
    _settlementPanel->drawRect(panelMin, panelMax, Color4F(0.68f, 0.74f, 0.84f, 1.0f), 3.0f);

    _settlementTitle->setPosition(Vec2(center.x, center.y + panelH * 0.31f));
    _settlementRewards->setPosition(Vec2(center.x, center.y + panelH * 0.10f));
    _settlementTotals->setPosition(Vec2(center.x, center.y - panelH * 0.10f));

    const float buttonsW = SETTLEMENT_BUTTON_W * 2.0f + SETTLEMENT_BUTTON_GAP;
    Vec2 upgradePos(center.x - buttonsW * 0.5f, panelMin.y + 28.0f);
    Vec2 restartPos(upgradePos.x + SETTLEMENT_BUTTON_W + SETTLEMENT_BUTTON_GAP, upgradePos.y);
    Vec2 modePos(center.x - SETTLEMENT_BUTTON_W * 0.5f,
                 upgradePos.y + SETTLEMENT_BUTTON_H + 20.0f);
    if (_gameMode == GameMode::Endless)
    {
        modePos = Vec2(center.x - buttonsW * 0.5f, panelMin.y + 28.0f);
        restartPos = Vec2(modePos.x + SETTLEMENT_BUTTON_W + SETTLEMENT_BUTTON_GAP, modePos.y);
    }
    else if (_gameMode == GameMode::Core)
    {
        restartPos = Vec2(center.x - SETTLEMENT_BUTTON_W * 0.5f, panelMin.y + 28.0f);
    }
    _settlementUpgradeBtn->setPosition(upgradePos);
    _settlementRestartBtn->setPosition(restartPos);
    _settlementModeBtn->setPosition(modePos);
    _settlementUpgradeRect = Rect(upgradePos.x, upgradePos.y, SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H);
    _settlementRestartRect = Rect(restartPos.x, restartPos.y, SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H);
    _settlementModeRect = Rect(modePos.x, modePos.y, SETTLEMENT_BUTTON_W, SETTLEMENT_BUTTON_H);
}

void MainScene::refreshEconomyHUD()
{
    if (!_coinLabel)
    {
        return;
    }
    const auto& economy = GameData::getInstance().economy();
    if (economy.runActive() && economy.runCoins() > 0)
    {
        _coinLabel->setString(StringUtils::format("金币  %d  (+%d)", economy.totalCoins(), economy.runCoins()));
    }
    else
    {
        _coinLabel->setString(StringUtils::format("金币  %d", economy.totalCoins()));
    }
    _crystalLabel->setString(StringUtils::format("水晶  %d", economy.totalCrystals()));
}

void MainScene::refreshRunBuffHUD()
{
    if (!_runBuffLabel)
    {
        return;
    }
    std::string text;
    for (size_t i = 0; i < RunBuffModel::BUFF_COUNT; ++i)
    {
        const auto id = static_cast<RunBuffId>(i);
        const int stack = _runBuffs.stacks(id);
        if (stack <= 0)
        {
            continue;
        }
        if (!text.empty()) text += "   ";
        text += StringUtils::format("%s Lv.%d", RunBuffModel::definition(id).name, stack);
    }
    _runBuffLabel->setString(text);
    _runBuffLabel->setVisible(_gameMode == GameMode::Endless && !text.empty());
}

void MainScene::offerRunBuff()
{
    const uint32_t seed = static_cast<uint32_t>(rand()) ^
                          static_cast<uint32_t>(_endlessWaveNumber) * 0x9E3779B9u;
    if (!_runBuffs.beginOffer(seed))
    {
        AXLOGW("[GameModes][Roguelike] offer rejected because another offer is pending");
        return;
    }
    stopContinuousFire();
    _gameState   = GameState::pause;
    _aimingSkill = -1;
    _aimDragging = false;
    _aimIndicator->setVisible(false);
    std::array<ThreeChoiceCard, ThreeChoicePanel::CHOICE_COUNT> cards;
    const Color4B accents[] = {
        Color4B(235, 178, 61, 255), Color4B(77, 199, 163, 255),
        Color4B(235, 87, 82, 255), Color4B(87, 158, 245, 255),
    };
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto id          = _runBuffs.choices()[i];
        const auto& definition = RunBuffModel::definition(id);
        const int current      = _runBuffs.stacks(id);
        cards[i] = ThreeChoiceCard{
            definition.name,
            definition.description,
            StringUtils::format("当前层数 %d  ->  %d", current,
                                std::min(current + 1, definition.maxStacks)),
            accents[static_cast<size_t>(id)],
        };
    }
    _choicePanel->open("无尽增益 · 三选一", "仅本次无尽守城生效，失败后全部清空", cards);
    AXLOGI("[GameModes][Roguelike] offer opened: endlessWave={}, offer={}", _endlessWaveNumber,
           _runBuffs.offerCount());
}

void MainScene::chooseRunBuff(RunBuffId id)
{
    if (!_runBuffs.select(id))
    {
        AXLOGW("[GameModes][Roguelike] invalid selection ignored: id={}", static_cast<int>(id));
        return;
    }
    _choicePanel->setVisible(false);
    refreshRunBuffHUD();
    AXLOGI("[GameModes][Roguelike] selected: key={}, stacks={}", RunBuffModel::definition(id).key,
           _runBuffs.stacks(id));
    advanceEndlessWave();
}

void MainScene::offerCampaignReward(int stage, bool resumeRunAfterChoice)
{
    _campaignRewards.reset();
    const uint32_t seed = 0xC0FFEEu ^ static_cast<uint32_t>(stage) * 0x85EBCA6Bu;
    if (!_campaignRewards.beginOffer(seed, GameData::getInstance().campaignRewardContext(stage)))
    {
        AXLOGW("[GameModes][CampaignReward] offer rejected: stage={}", stage);
        return;
    }
    _resumeCampaignAfterReward = resumeRunAfterChoice;
    stopContinuousFire();
    _gameState   = GameState::pause;
    _aimingSkill = -1;
    _aimDragging = false;
    _aimIndicator->setVisible(false);

    std::array<ThreeChoiceCard, ThreeChoicePanel::CHOICE_COUNT> cards;
    for (size_t i = 0; i < cards.size(); ++i)
    {
        const auto id          = _campaignRewards.choices()[i];
        const auto& definition = CampaignRewardModel::definition(id);
        const bool resource    = definition.upgradeIndex < 0;
        cards[i] = ThreeChoiceCard{
            definition.name,
            CampaignRewardModel::description(id, stage),
            resource ? "立即入账并保存" : "永久养成 · 无需消耗货币",
            resource ? Color4B(91, 192, 225, 255) : Color4B(235, 178, 61, 255),
        };
    }
    _choicePanel->open(StringUtils::format("第 %d 关养成奖励", stage),
                       "首次通关奖励 · 选择后永久保存", cards);
    AXLOGI("[GameModes][CampaignReward] offer opened: stage={}, resume={}", stage,
           resumeRunAfterChoice);
}

void MainScene::chooseCampaignReward(CampaignRewardId id)
{
    const int stage = _campaignRewards.stage();
    if (!_campaignRewards.select(id))
    {
        AXLOGW("[GameModes][CampaignReward] invalid selection ignored: stage={}, id={}", stage,
               static_cast<int>(id));
        return;
    }
    if (!GameData::getInstance().claimCampaignReward(stage, id))
    {
        AXLOGW("[GameModes][CampaignReward] selection became unavailable; refreshing offer: stage={}",
               stage);
        offerCampaignReward(stage, _resumeCampaignAfterReward);
        return;
    }
    _choicePanel->setVisible(false);
    refreshEconomyHUD();
    _lastCampaignRewardName = CampaignRewardModel::definition(id).name;
    AXLOGI("[GameModes][CampaignReward] selection completed: stage={}, reward={}", stage,
           CampaignRewardModel::definition(id).key);
    if (_hasPendingSettlement)
    {
        const auto result = _pendingSettlement;
        _hasPendingSettlement = false;
        presentSettlement(result, true);
    }
    else if (_resumeCampaignAfterReward)
    {
        _resumeCampaignAfterReward = false;
        startCampaignRun();
    }
}

void MainScene::showSettlement(bool victory)
{
    if (_settlementVisible)
    {
        return;
    }
    const int stage      = static_cast<int>(_levelIndex) + 1;
    const int stageCount = static_cast<int>(_campaign.levels().size());
    SettlementSummary result;
    if (_gameMode == GameMode::Endless)
    {
        result = GameData::getInstance().settleEndlessRun();
    }
    else if (_gameMode == GameMode::Core)
    {
        result = GameData::getInstance().settlePracticeRun(victory);
    }
    else
    {
        result = GameData::getInstance().settleRun(victory, stage, stageCount,
                                                    currentLevel().clearCrystals);
    }
    if (!result.committed)
    {
        return;
    }
    if (_gameMode == GameMode::Campaign && result.stageAdvanced)
    {
        _pendingSettlement    = result;
        _hasPendingSettlement = true;
        offerCampaignReward(stage, false);
        return;
    }
    presentSettlement(result, victory);
}

void MainScene::presentSettlement(const SettlementSummary& result, bool victory)
{
    stopContinuousFire();
    _settlementVisible = true;
    _gameState         = GameState::end;
    _aimingSkill       = -1;
    _aimDragging       = false;
    _aimIndicator->setVisible(false);
    _skillHint->setVisible(false);

    const auto& data = GameData::getInstance();
    const int stage      = static_cast<int>(_levelIndex) + 1;
    const int stageCount = static_cast<int>(_campaign.levels().size());
    const int clearedStages = std::min(data.highestClearedStage(), stageCount);
    const bool campaignClear = _gameMode == GameMode::Campaign && victory && clearedStages >= stageCount;
    _settlementTitle->setString(
        _gameMode == GameMode::Core ? (victory ? "核心玩法验证完成" : "核心玩法验证失败")
        : _gameMode == GameMode::Endless ? "无尽守城结束"
        : campaignClear ? "五关全通！"
                        : StringUtils::format("第 %d 关%s", stage, victory ? "通过" : "失败"));
    _settlementTitle->setTextColor(victory ? Color4B(120, 245, 160, 255) : Color4B(255, 125, 105, 255));
    const std::string rewardSuffix = _lastCampaignRewardName.empty()
                                         ? ""
                                         : "    养成：" + _lastCampaignRewardName;
    if (_gameMode == GameMode::Core)
    {
        _settlementRewards->setString(
            StringUtils::format("本局击杀  %d\n练习局：不结算金币、水晶或关卡进度", result.kills));
        _settlementTotals->setString(
            StringUtils::format("永久数据未改变：金币 %d    水晶 %d", data.economy().totalCoins(),
                                data.economy().totalCrystals()));
    }
    else if (_gameMode == GameMode::Endless)
    {
        _settlementRewards->setString(
            StringUtils::format("守住 %d 波    击杀 %d\n金币 +%d", _endlessCompletedWaves,
                                result.kills, result.coinsEarned));
        _settlementTotals->setString(
            StringUtils::format("持有：金币 %d    水晶 %d", data.economy().totalCoins(),
                                data.economy().totalCrystals()));
    }
    else
    {
        _settlementRewards->setString(
            StringUtils::format("本局击杀  %d\n金币 +%d    水晶 +%d%s", result.kills,
                                result.coinsEarned, result.crystalsEarned, rewardSuffix.c_str()));
        _settlementTotals->setString(
            StringUtils::format("持有：金币 %d    水晶 %d    进度 %d/%d",
                                data.economy().totalCoins(), data.economy().totalCrystals(),
                                clearedStages, stageCount));
    }
    const std::string nextText = _gameMode == GameMode::Core ? "再次验证"
                                     : _gameMode == GameMode::Endless ? "再守一次"
                                     : victory ? (campaignClear ? "重玩终章" : "下一关")
                                               : "重试本关";
    _settlementRestartLabel->setString(nextText);
    _settlementUpgradeBtn->setVisible(_gameMode == GameMode::Campaign);
    _settlementModeBtn->setVisible(_gameMode != GameMode::Core);
    _settlementHomeLabel->setString(_gameMode == GameMode::Endless ? "返回肉鸽主页"
                                                                    : "返回闯关主页");
    _upgradePanel->setStartRunCallback([this]() { resetGame(); });
    _upgradePanel->setStartButtonText(nextText);
    _settlementLayer->setVisible(true);
    const auto visibleSize = _director->getVisibleSize();
    const auto origin = _director->getVisibleOrigin();
    layoutEconomyUI(origin, origin.x + visibleSize.width, origin.y + visibleSize.height);
    refreshEconomyHUD();
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

bool MainScene::isSkillAvailable(SkillId id) const
{
    // 五关拆分前的稳定核心基线默认开放三项基础技能。这里只改变本局可用性，
    // 不调用解锁接口，因此不会绕过闯关进度写入玩家存档。
    if (_gameMode == GameMode::Core && id != SkillId::Lightning)
    {
        return true;
    }
    return GameData::getInstance().isSkillUnlocked(id);
}

// 点技能按钮：再点同一个 = 取消；冷却中/蓝不足 → 提示
void MainScene::tryAimSkill(int index)
{
    if (index < 0 || index >= SKILL_COUNT)
    {
        return;
    }
    if (!isSkillAvailable(static_cast<SkillId>(index)))
    {
        const auto& definition = GameData::getInstance().skillDefinition(static_cast<SkillId>(index));
        _skillHint->setString(definition.unlockStage > 0
                                  ? StringUtils::format("技能尚未解锁：第 %d 关自动解锁", definition.unlockStage)
                                  : "技能尚未解锁，可在结算后的城堡工坊购买");
        _skillHint->setVisible(true);
        _skillHintTimer = 1.8f;
        return;
    }
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
#if CG_DEBUG_TOOLS
    if (_sandboxInfiniteMana)
    {
        _mana = MANA_MAX;
    }
    else
#endif
    {
        _mana = std::min(MANA_MAX, _mana + MANA_REGEN * delta);
    }
    for (int i = 0; i < SKILL_COUNT; ++i)
    {
#if CG_DEBUG_TOOLS
        if (_sandboxNoSkillCooldown)
        {
            _cdRemaining[i] = 0.0f;
            continue;
        }
#endif
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
        const bool unlocked = isSkillAvailable(static_cast<SkillId>(i));
        if (!unlocked)
        {
            _skillCdShade[i]->drawSolidRect(Vec2::ZERO, Vec2(SKILL_BTN_SIZE, SKILL_BTN_SIZE),
                                            Color4F(0.0f, 0.0f, 0.0f, 0.72f));
            _skillCdLabel[i]->setString("锁");
            _skillNameLabel[i]->setTextColor(Color4B(145, 150, 165, 255));
        }
        else if (_cdRemaining[i] > 0.0f)
        {
            const float h = SKILL_BTN_SIZE * (_cdRemaining[i] / SKILL_CD[i]);
            _skillCdShade[i]->drawSolidRect(Vec2::ZERO, Vec2(SKILL_BTN_SIZE, h),
                                            Color4F(0.0f, 0.0f, 0.0f, 0.65f));
            _skillCdLabel[i]->setString(StringUtils::format("%d", static_cast<int>(_cdRemaining[i]) + 1));
        }
        else
        {
            _skillCdLabel[i]->setString("");
            _skillNameLabel[i]->setTextColor(Color4B::WHITE);
        }
        // 瞄准中的按钮放大一点 = 选中态（零贴图的按钮高亮方案）
        _skillBtns[i]->setScale(i == _aimingSkill ? 1.15f : 1.0f);
    }
}

void MainScene::onTouchesBegan(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
#if CG_DEBUG_TOOLS
    for (auto&& touch : touches)
    {
        if (_debugModeMenuBtnRect.containsPoint(touch->getLocation()))
        {
            stopContinuousFire();
            AXLOGI("[ModeNavigation] development menu requested");
            showDevelopmentMenu();
            return;
        }
        if (_debugTuneBtnRect.containsPoint(touch->getLocation()))
        {
            stopContinuousFire();
            toggleTuningPanel();
            return;
        }
    }
#endif
    if ((_upgradePanel && _upgradePanel->isVisible()) ||
        (_choicePanel && _choicePanel->isVisible()) ||
        (_modeSelectPanel && _modeSelectPanel->isVisible())
#if CG_DEBUG_TOOLS
        || (_developmentMenuPanel && _developmentMenuPanel->isVisible())
#endif
        || (_campaignMenuPanel && _campaignMenuPanel->isVisible())
        || (_roguelikeMenuPanel && _roguelikeMenuPanel->isVisible())
    )
    {
        return;  // 模态面板由 UI 系统处理，触摸不能穿透到战场
    }
    // 结算与升级页都是模态界面，不能穿透到射箭/技能/调试按钮。
    if (_settlementVisible)
    {
        if (_upgradePanel->isVisible())
        {
            return;  // UpgradePanel 内的 ui::Button / ScrollView 自己处理触摸
        }
        for (auto&& t : touches)
        {
            if (_settlementUpgradeBtn->isVisible() &&
                _settlementUpgradeRect.containsPoint(t->getLocation()))
            {
                _settlementLayer->setVisible(false);
                _upgradePanel->open();
                break;
            }
            if (_settlementRestartRect.containsPoint(t->getLocation()))
            {
                resetGame();
                break;
            }
            if (_settlementModeBtn->isVisible() &&
                _settlementModeRect.containsPoint(t->getLocation()))
            {
                AXLOGI("[ModeNavigation] settlement home requested");
                showCurrentModeMenu();
                break;
            }
        }
        return;
    }
    // 输入优先级：调试按钮 > 双指手势 > 射箭。
    // 命中测试：Rect::containsPoint 判断触点是否在按钮热区内——
    // 这就是最朴素的"UI 按钮"实现（一个矩形 + 点包含判定）
    for (auto&& t : touches)
    {
#if CG_DEBUG_TOOLS
        if (_debugBtnRect.containsPoint(t->getLocation()))
        {
            stopContinuousFire();
            buildBattlefield(_battleLayout == BattleLayout::Oblique25D ? BattleLayout::SideView
                                                                       : BattleLayout::Oblique25D);
            return;
        }
        if (_debugRestartBtnRect.containsPoint(t->getLocation()))
        {
            stopContinuousFire();
            resetGame();  // ↻ 重开：清场满血回第 1 波
            return;
        }
        if (_debugPrevBtnRect.containsPoint(t->getLocation()))
        {
            stopContinuousFire();
            gotoWave(-1);  // ◀ 上一波
            return;
        }
        if (_debugNextBtnRect.containsPoint(t->getLocation()))
        {
            stopContinuousFire();
            gotoWave(1);  // ▶ 下一波
            return;
        }
#endif
        // 技能按钮（正式玩法 UI）：按下只进/退瞄准模式，不显示范围圈
        for (int i = 0; i < SKILL_COUNT; ++i)
        {
            if (_skillBtnRects[i].containsPoint(t->getLocation()))
            {
                stopContinuousFire();
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
        stopContinuousFire();
        buildBattlefield(_battleLayout == BattleLayout::Oblique25D ? BattleLayout::SideView
                                                                   : BattleLayout::Oblique25D);
        return;
    }
#endif
    // 瞄准模式下的战场按下已在上面处理（显示范围圈），释放由 onTouchesEnded 结算
    if (!touches.empty())
    {
        // 按下立即射第一轮；保持按住时由 update() 按原有射速持续射击。
        // Touch 位置是世界坐标，拖动时会持续更新，因此也支持按住扫射转向。
        beginContinuousFire(touches.front());
    }
}

void MainScene::onTouchesMoved(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    if (_settlementVisible || (_upgradePanel && _upgradePanel->isVisible()) ||
        (_choicePanel && _choicePanel->isVisible()) ||
        (_modeSelectPanel && _modeSelectPanel->isVisible())
#if CG_DEBUG_TOOLS
        || (_developmentMenuPanel && _developmentMenuPanel->isVisible())
#endif
        || (_campaignMenuPanel && _campaignMenuPanel->isVisible())
        || (_roguelikeMenuPanel && _roguelikeMenuPanel->isVisible())
    )
    {
        return;
    }
    // 技能瞄准拖动：范围指示圈跟随手指（只有"战场上按下后"才进入这个状态）
    if (_aimingSkill >= 0 && _aimDragging && !touches.empty())
    {
        updateAimIndicator(touches[0]->getLocation());
        return;
    }
    if (_continuousFireActive)
    {
        for (auto&& t : touches)
        {
            if (t->getID() == _continuousFireTouchId)
            {
                _continuousFireTarget = t->getLocation();
                break;
            }
        }
    }
}

void MainScene::onTouchesEnded(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    for (auto&& t : touches)
    {
        stopContinuousFire(t->getID());
    }
    if (_settlementVisible || (_upgradePanel && _upgradePanel->isVisible()) ||
        (_choicePanel && _choicePanel->isVisible()) ||
        (_modeSelectPanel && _modeSelectPanel->isVisible())
#if CG_DEBUG_TOOLS
        || (_developmentMenuPanel && _developmentMenuPanel->isVisible())
#endif
        || (_campaignMenuPanel && _campaignMenuPanel->isVisible())
        || (_roguelikeMenuPanel && _roguelikeMenuPanel->isVisible())
    )
    {
        return;
    }
    // 技能释放：战场上抬起手指 = 在抬起处释放
    if (_aimingSkill >= 0 && _aimDragging && !touches.empty())
    {
        _aimDragging = false;
        castSkill(touches[0]->getLocation());
    }
}

void MainScene::onTouchesCancelled(const std::vector<ax::Touch*>& touches, ax::Event* event)
{
    for (auto&& t : touches)
    {
        stopContinuousFire(t->getID());
    }
    if (_aimDragging)
    {
        _aimDragging = false;
        _aimIndicator->setVisible(false);
    }
}

void MainScene::onKeyPressed(ax::EventKeyboard::KeyCode code, ax::Event* event)
{
#if CG_DEBUG_TOOLS
    // ImGui 属于开发层，即使闯关主页或工坊正在显示，也允许独立开关。
    if (code == EventKeyboard::KeyCode::KEY_T)
    {
        toggleTuningPanel();
        return;
    }
    if ((_upgradePanel && _upgradePanel->isVisible()) ||
        (_choicePanel && _choicePanel->isVisible()) ||
        (_modeSelectPanel && _modeSelectPanel->isVisible()) ||
        (_developmentMenuPanel && _developmentMenuPanel->isVisible()) ||
        (_campaignMenuPanel && _campaignMenuPanel->isVisible()) ||
        (_roguelikeMenuPanel && _roguelikeMenuPanel->isVisible()))
    {
        return;
    }
    // 桌面调试键：1/2 切视角、R 重开、Q/E 跳波、T 开发者工具、U 预览升级页。
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
    else if (code == EventKeyboard::KeyCode::KEY_U && !_settlementVisible)
    {
        sandboxOpenUpgradePreview();
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

        // 射箭冷却每帧衰减；沙盒“射箭无冷却”只在开发构建存在。
#if CG_DEBUG_TOOLS
        _fireCooldown = _sandboxNoFireCooldown ? 0.0f : std::max(0.0f, _fireCooldown - delta);
#else
        _fireCooldown = std::max(0.0f, _fireCooldown - delta);
#endif
        if (_fireCooldown <= 0.0f)
        {
            // 装填完成：恢复待发射箭；若增益改变多重箭数量，会在此同步表现。
            refreshWallBowLoadedArrows();
        }
        if (_continuousFireActive)
        {
            fireArrow(_continuousFireTarget);
        }

        // 游戏主循环：怪物移动、箭矢飞行、敌方弹丸、技能系统都在这里驱动
        updateMonsters(delta);
        updateArrows(delta);
        updateEnemyBolts(delta);
        updateSkills(delta);  // M1：回蓝/冷却/火球弹道/箭雨/特效
        break;
    }

    case GameState::pause:
    {
        // 三选一暂停战斗，但窗口旋转/缩放仍需重排模态界面。
        const auto& vs = _director->getVisibleSize();
        if (vs.width != _lastVisibleSize.width || vs.height != _lastVisibleSize.height)
        {
            buildBattlefield(_battleLayout);
        }
        break;
    }
    case GameState::menu1:
    case GameState::menu2:
    {
        const auto& vs = _director->getVisibleSize();
        if (vs.width != _lastVisibleSize.width || vs.height != _lastVisibleSize.height)
        {
            buildBattlefield(_battleLayout);
        }
        break;
    }

    case GameState::end:
    {
        // 结算层显示期间仍要响应旋转/窗口缩放，但 settlementVisible 会阻止重播波次。
        const auto& vs = _director->getVisibleSize();
        if (vs.width != _lastVisibleSize.width || vs.height != _lastVisibleSize.height)
        {
            buildBattlefield(_battleLayout);
        }
        break;
    }

    }  // switch
    refreshCombatHUD();
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
