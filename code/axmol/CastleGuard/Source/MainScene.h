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

#include "CampaignConfig.h"
#include "GameData.h"
#include "MonsterBalance.h"
#include "RunBuff.h"
#include "axmol.h"

class UpgradePanel;
class ThreeChoicePanel;
class ModeSelectPanel;
class ModeHomePanel;
class DevelopmentMenuPanel;
enum class DevelopmentMenuEntry;

// 调试内容开关（CMake 选项 -DCG_DEBUG_TOOLS=ON/OFF 注入）：
// 1 = 开发版（调试按钮、ImGui 开发者工具、桌面调试键都可用）；
// 0 = 正式包，上述代码经预处理整块剔除，不编进二进制。
// 不经过 CMake 的兜底默认是开发版（1）。
#ifndef CG_DEBUG_TOOLS
#define CG_DEBUG_TOOLS 1
#endif

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

    enum class GameMode
    {
        Core,
        Campaign,
        Endless,
    };

public:
    bool init() override;
    void update(float delta) override;
#if CG_DEBUG_TOOLS
    void onEnter() override;   // 正式包里这两个 override 只为 ImGui 服务，一并剔除
    void onExit() override;
#endif

    // touch
    void onTouchesBegan(const std::vector<ax::Touch*>& touches, ax::Event* event);
    void onTouchesMoved(const std::vector<ax::Touch*>& touches, ax::Event* event);
    void onTouchesEnded(const std::vector<ax::Touch*>& touches, ax::Event* event);
    void onTouchesCancelled(const std::vector<ax::Touch*>& touches, ax::Event* event);

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
    void beginContinuousFire(ax::Touch* touch); // 按下立即射击，并记录长按瞄准点
    void stopContinuousFire(int touchId = -1); // 抬起/取消/进入模态界面时停止续射
    void updateArrows(float delta);             // 每帧驱动箭矢飞行与回收
    void updateWallBowAim(const ax::Vec2& target);  // 弓身实时朝向当前射击目标
    void playWallBowFireAnimation();            // 成功发射后播放空弓释放与回弹
    void refreshWallBowLoadedArrows();           // 装填完成时同步多重箭数量与扇形角度

    // 箭矢 = 池中的一个槽位。对象池核心思想：游戏循环里绝不 new/delete，
    // 频繁创建销毁会造成内存碎片和卡顿；开局一次建好，用完归还重复利用
    struct Arrow
    {
        ax::DrawNode* node = nullptr;  // 箭矢图形（池化复用）
        ax::Vec2 velocity;             // 当前速度向量（像素/秒）
        int damage = 1;                // 发射时固化伤害，飞行途中升级不会改变这支箭
        float knockbackDistance = 0.0f;  // 发射时固化击退距离，后续 Buff/换装不会追改
        bool critical = false;         // 暴击在发射时判定，用缩放提供即时视觉反馈
        int loadedShotIndex = -1;      // 装填时在多重箭扇形中的序号
        bool loaded = false;           // true 时同一个 node 正挂在弓上等待发射
        bool active = false;           // 该槽位是否正在飞行中
    };
    void reparentArrowNode(Arrow& arrow, ax::Node* parent, int localZOrder);
    void parkArrow(Arrow& arrow);      // 回到场景对象池并隐藏，避免随旧弓一起销毁
    std::vector<Arrow> _arrowPool;
    ax::Vec2 _archerPos = ax::Vec2::ZERO;      // 箭手发射点（随布局重建更新）
    ax::Node* _wallBowWeapon = nullptr;         // 挂在 _battlefield 下，重建战场时重新创建
    ax::Node* _wallBowVisual = nullptr;         // 只承载弓身动画，不影响父节点的瞄准旋转
    ax::Node* _wallBowArrowMount = nullptr;     // 真实对象池箭的弓上挂点

    // ---- 调试层（CG_DEBUG_TOOLS=0 时整块不编译，正式包零残留） ----
#if CG_DEBUG_TOOLS
    // 常驻调试按钮（模拟器里没有键盘也无法双指，用它切换视角）
    void initDebugUI();
    void layoutDebugUI(const ax::Vec2& origin, float top);  // 随布局重建重排按钮
    ax::Node* _debugBtn = nullptr;
    ax::Rect _debugBtnRect;  // 按钮热区（世界坐标，随布局重建更新）
    ax::Node* _debugRestartBtn = nullptr;  // 重开按钮（清场满血回第 1 波）
    ax::Rect _debugRestartBtnRect;
    ax::Node* _debugPrevBtn = nullptr;     // ◀ 上一波
    ax::Rect _debugPrevBtnRect;
    ax::Node* _debugNextBtn = nullptr;     // ▶ 下一波
    ax::Rect _debugNextBtnRect;
    ax::Node* _debugTuneBtn = nullptr;     // ⚙ 手感调优面板（ImGui）
    ax::Rect _debugTuneBtnRect;
    ax::Node* _debugModeMenuBtn = nullptr; // 开发总入口（四宫格）
    ax::Rect _debugModeMenuBtnRect;
    void gotoWave(int offset);             // 调试跳波：offset = ±1（清场但不动血量）

    // ---- 手感调优面板（ImGui 扩展） ----
    void toggleTuningPanel();              // 显示/隐藏面板（添加/移除渲染循环）
    void onImGuiDraw();                    // ImGui 每帧回调：实时调参与沙盒工具
    void drawTuningTab();
    void drawMonsterTuningTab();
    void drawSandboxTab();
    void syncSandboxDraft();
    void sandboxKillAllMonsters();
    void sandboxOpenUpgradePreview();
    void sandboxGotoLevel(int offset);
    bool _tuningVisible = false;
    bool _sandboxInvincibleWall  = false;
    bool _sandboxInfiniteMana    = false;
    bool _sandboxNoSkillCooldown = false;
    bool _sandboxNoFireCooldown  = false;
    int _monsterTuningType       = 0;
    int _monsterTuningDirtyType  = -1;
    DebugPlayerData _sandboxDraft;
#endif
    void clearField();  // 全部对象池回收（重开与调试跳波共用）
    void resetGame();   // 按当前模式开始下一局
    float _fireCooldown = 0.0f;            // 射速上限冷却（游戏机制，正式包保留）
    bool _continuousFireActive = false;     // 长按是否仍在持续射击
    int _continuousFireTouchId = -1;        // 只跟踪启动长按的那根手指
    ax::Vec2 _continuousFireTarget = ax::Vec2::ZERO;  // 可随拖动实时更新的瞄准点

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
        ax::DrawNode* node = nullptr;   // 占位外观（池化复用；出生时才按类型画皮）
        int lane           = 0;         // 车道编号（统一数据模型：车道 + 推进）
        ax::Vec2 targetPos;             // 推进终点（出生点+同一份抖动，保证走直线不纠偏）
        ax::Vec2 wanderTarget;          // 游荡航点（飞行怪专用：攻击期间持续飞往的下一个点）
        float wanderPhase    = 0.0f;    // 游荡摆动相位（正弦摆动的累加器，让轨迹带弧度）
        float windupTimer    = 0.0f;    // 攻击前摇剩余时间（飞行怪/Boss：>0 时悬停蓄力不能动）
        int  maxHp           = 1;       // 血量上限（狂暴怪按 hp/maxHp 算加速；治疗回血不超过它）
        int  shieldCharges   = 0;       // 一次性全能护盾层数（护符怪：挡任何伤害一次）
        float ghostTimer     = 0.0f;    // 幽灵怪隐身周期计时
        bool  ghostHidden    = false;   // 幽灵怪当前是否隐身（隐身时箭打不中，AOE 照吃）
        float breathPhase    = 0.0f;    // 呼吸动画相位；每次出生随机，避免怪群同步起伏
        float breathSpeed    = 2.5f;    // 呼吸相位速度（弧度/秒），出生时随机
        float speed        = 70.0f;     // 个体速度（类型倍率 × 85%~115% 随机）
        float bodyR        = 11.0f;     // 体型半径（随类型缩放，命中判定也用它）
        MonsterType type   = MonsterType::Normal;
        MonsterState state = MonsterState::Walk;
        float attackTimer    = 0.0f;    // 攻击节奏计时
        float attackRange    = 6.0f;    // 开火距离：近战=6px 贴墙，远程=停火线
        float attackInterval = 1.0f;    // 攻击间隔（近战每秒一口，远程更慢）
        int actionPower       = 5;       // 伤害/治疗量/召唤数量，出生时从怪物参数固化
        float slowTimer      = 0.0f;    // 冰冻剩余时间（>0 时移速打折扣，技能系统）
        float stunTimer      = 0.0f;    // 麻痹剩余时间（>0 时不能移动不能攻击，闪电技能）
        ax::Vec2 knockbackDirection;    // 指向远离城墙的单位向量
        float knockbackRemaining = 0.0f;  // 尚未平滑结算的击退距离
        int  hp              = 1;       // 血量（当前全是 1 血；M1 护盾怪起开始 >1）
        ax::DrawNode* slowMark = nullptr;  // 冰冻标记（蓝环，挂在怪节点上，减速时可见）
        ax::DrawNode* stunMark = nullptr;  // 麻痹标记（黄电纹，麻痹时可见）
        ax::DrawNode* windupMark = nullptr;  // 蓄力标记（橙环，飞行怪攻击前摇时可见）
        bool active        = false;
    };
    void initMonsterPool();
    // 按波次配置放一只怪（lane=-1 随机车道；from=出生方向）
    void spawnMonster(MonsterType type, int lane, const std::string& from);
    // 战中生怪入口：分裂怪的子体、召唤怪的召唤物都走这（在指定位置直接激活）
    void spawnMinion(MonsterType type, const ax::Vec2& pos);
    Monster* allocMonster();  // 从池里找一个空槽，找不到返回 nullptr
    // 槽位初始化：类型数值/外观/状态复位全在这里——两个生怪入口共用，不重复写
    void setupMonster(Monster& m, MonsterType type, const ax::Vec2& spawnPos, const ax::Vec2& targetPos);
    void applyMonsterSkin(Monster& m);    // 按类型重画占位外观（槽位与外观分离）
    void updateMonsters(float delta);     // 每帧驱动移动/攻击/死亡
    ax::Vec2 laneSpawnPos(int lane) const;  // 车道 → 出怪位置（按当前布局计算）
    ax::Vec2 laneWallPos(int lane) const;   // 车道 → 城墙目标点（按当前布局计算）
    int laneCount() const;                  // 当前布局的有效车道数
    ax::Vec2 pickFlyerWaypoint() const;     // 飞行怪游荡航点：远区空域内随机取一点
    void applyKnockback(Monster& m, float sourceDistance);  // 箭命中后按怪物抗性排队平滑位移

    std::vector<Monster> _monsterPool;
    MonsterBalance _monsterBalance;
    int _wallHP    = 100;           // 城墙当前血量
    int _wallMaxHP = 100;           // 本局开战时固化的城墙上限
    void damageWall(int dmg);        // 城墙扣血统一入口（近战啃咬/远程弹丸都走这）

    // ---- 共享战斗 HUD：玩法核心 / 闯关 / 肉鸽共用 ----
    void initCombatHUD();
    void layoutCombatHUD(const ax::Vec2& origin, const ax::Size& visibleSize);
    void refreshCombatHUD();
    ax::DrawNode* _wallHealthBar = nullptr;
    ax::Label* _wallHealthLabel = nullptr;
    ax::Rect _wallHealthRect;
    ax::DrawNode* _bossHealthBar = nullptr;
    ax::Label* _bossHealthLabel = nullptr;
    ax::Rect _bossHealthRect;

    // ---- 结算与经济（M1）----
    // 奖励规则与持久化归 GameData；场景只负责展示和触发结算。
    void initEconomyUI();
    void layoutEconomyUI(const ax::Vec2& origin, float right, float top);
    void refreshEconomyHUD();
    void showSettlement(bool victory);
    void presentSettlement(const SettlementSummary& result, bool victory);
    void offerRunBuff();
    void chooseRunBuff(RunBuffId id);
    void offerCampaignReward(int stage, bool resumeRunAfterChoice);
    void chooseCampaignReward(CampaignRewardId id);
    void refreshRunBuffHUD();
    PlayerCombatStats currentCombatStats() const;
    void showEntryMenu();
    void showModeSelect();
    void showCampaignMenu();
    void showRoguelikeMenu();
    void showCurrentModeMenu();
    void selectMode(GameMode mode);
    void beginCampaignBattle();
    void startCoreRun();
    void startCampaignRun();
    void startEndlessRun();
    void resetBattleForRun();
    void prepareEndlessWave(int waveNumber);
    void advanceEndlessWave();
    ax::Label* _coinLabel               = nullptr;
    ax::Label* _crystalLabel            = nullptr;
    ax::Node* _settlementLayer          = nullptr;
    ax::DrawNode* _settlementBackdrop   = nullptr;
    ax::DrawNode* _settlementPanel      = nullptr;
    ax::Node* _settlementRestartBtn     = nullptr;
    ax::Label* _settlementRestartLabel  = nullptr;
    ax::Node* _settlementUpgradeBtn     = nullptr;
    ax::Node* _settlementModeBtn        = nullptr;
    ax::Label* _settlementHomeLabel     = nullptr;
    ax::Label* _settlementTitle         = nullptr;
    ax::Label* _settlementRewards       = nullptr;
    ax::Label* _settlementTotals        = nullptr;
    ax::Rect _settlementRestartRect;
    ax::Rect _settlementUpgradeRect;
    ax::Rect _settlementModeRect;
    bool _settlementVisible             = false;
    UpgradePanel* _upgradePanel         = nullptr;
    ThreeChoicePanel* _choicePanel      = nullptr;
    ModeSelectPanel* _modeSelectPanel   = nullptr;
    ModeHomePanel* _campaignMenuPanel   = nullptr;
    ModeHomePanel* _roguelikeMenuPanel  = nullptr;
#if CG_DEBUG_TOOLS
    void showDevelopmentMenu();
    void selectDevelopmentMode(DevelopmentMenuEntry entry);
    DevelopmentMenuPanel* _developmentMenuPanel = nullptr;
#endif
    ax::Label* _runBuffLabel            = nullptr;
    RunBuffModel _runBuffs;
    CampaignRewardModel _campaignRewards;
    SettlementSummary _pendingSettlement;
    bool _hasPendingSettlement          = false;
    bool _resumeCampaignAfterReward     = false;
    std::string _lastCampaignRewardName;
    GameMode _gameMode                  = GameMode::Campaign;
    int _endlessWaveNumber              = 0;
    int _endlessCompletedWaves          = 0;

    // 敌方弹丸：远程怪的"箭"。和玩家箭矢同一个对象池思想，
    // 区别只是方向相反、命中的是城墙（看见弹丸飞 = 远程怪在工作，不是隔空扣血）
    struct EnemyBolt
    {
        ax::DrawNode* node = nullptr;
        ax::Vec2 velocity;
        ax::Vec2 targetPos;          // 瞄准的城墙点（到达即结算伤害）
        int damage   = 0;
        bool active  = false;
    };
    void initEnemyBoltPool();
    void fireEnemyBolt(const ax::Vec2& from, const ax::Vec2& target, int damage);
    void updateEnemyBolts(float delta);
    std::vector<EnemyBolt> _boltPool;

    // ---- 技能系统（M1：AOE 火球 / 冰冻减速 / 箭雨）----
    // 三段结构：技能按钮（HUD）→ 瞄准模式（点战场选落点）→ 释放结算。
    // 学习点：① 冷却+耗蓝双资源 ② AOE=落点圆 vs 怪身圆的距离判定
    //         ③ 特效也走对象池，并收进独立层级（_effectLayer）管理
    enum class SkillType
    {
        Fireball = 0,  // AOE 直伤
        Freeze,        // 范围减速
        ArrowRain,     // 持续多段点杀
        Lightning,     // 主目标点杀 + 范围概率麻痹（原作电系 1 阶）
        Count,
    };
    static constexpr int SKILL_COUNT = static_cast<int>(SkillType::Count);

    void initSkillBar();                                       // 技能按钮 + 蓝条（init 建一次）
    void layoutSkillBar(const ax::Vec2& origin, float right);  // 随布局重建重排
    void tryAimSkill(int index);                               // 点按钮：进入/切换/取消瞄准
    bool isSkillAvailable(SkillId id) const;                   // 核心验证恢复基线技能，不改存档
    void castSkill(const ax::Vec2& pos);                       // 瞄准模式下点战场：释放
    void updateSkills(float delta);                            // 每帧：回蓝/冷却/弹道/特效
    void refreshSkillHUD();                                    // 蓝条与冷却遮罩重画
    void killMonster(Monster& m);                              // 击杀统一入口（进 Dead 态）
    bool damageMonster(Monster& m, int dmg);  // 返回是否真正扣血；≤0 走击杀

    float _mana    = 100.0f;                    // 蓝量（自动回复，上限 100）
    float _cdRemaining[SKILL_COUNT] = {0.0f};   // 各技能剩余冷却
    int _aimingSkill = -1;                      // 瞄准中的技能序号；-1 = 未瞄准
    bool _aimDragging = false;                  // 按住按钮拖动中（抬起=释放）
    ax::DrawNode* _aimIndicator = nullptr;      // 范围指示圈（瞄准时跟随手指/鼠标）
    void updateAimIndicator(const ax::Vec2& pos);  // 重画指示圈并移到指定位置
    ax::Node* _skillBtns[SKILL_COUNT]     = {nullptr};
    ax::Rect _skillBtnRects[SKILL_COUNT];       // 按钮热区（世界坐标）
    ax::DrawNode* _skillCdShade[SKILL_COUNT] = {nullptr};  // 冷却遮罩（高度=剩余比例）
    ax::Label* _skillCdLabel[SKILL_COUNT]  = {nullptr};    // 冷却剩余秒数
    ax::Label* _skillNameLabel[SKILL_COUNT] = {nullptr};   // 按钮上的技能名
    ax::DrawNode* _manaBar = nullptr;           // 蓝条（每帧重画，只有两条矩形很便宜）
    ax::Label* _skillHint  = nullptr;           // 瞄准/提示文本
    float _skillHintTimer  = 0.0f;              // 提示自动隐藏计时（0=常驻的瞄准提示）

    // 火球弹体：抛物线飞行（lerp + 拱高），到点爆炸结算 AOE
    struct Fireball
    {
        ax::DrawNode* node = nullptr;
        ax::Vec2 from, target;
        float t     = 0.0f;   // 飞行进度 0→1
        bool active = false;
    };
    std::vector<Fireball> _fireballPool;
    void initFireballPool();

    // 箭雨的每一支下落箭：只记"何时落、落在哪"，到点才结算+出特效
    struct RainTick
    {
        float delay = 0.0f;   // 距释放多少秒落下
        ax::Vec2 pos;
    };
    std::vector<RainTick> _rainQueue;  // 待落下的箭（结算后即删）

    // 通用特效池：爆炸扩散环 / 冰冻区域 / 箭雨落痕，全是"活 N 秒自动归池"的临时图形
    struct Fx
    {
        ax::DrawNode* node = nullptr;
        float age = 0.0f, duration = 0.0f;
        int kind      = 0;    // 0=爆炸环 1=冰冻区 2=箭雨落痕
        ax::Vec2 pos;
        float radius  = 0.0f;
        bool active   = false;
    };
    std::vector<Fx> _fxPool;
    ax::Node* _effectLayer = nullptr;   // 特效专用层：zOrder 4（战场/箭矢之上，HUD 之下）
    void initFxPool();
    void spawnFx(int kind, const ax::Vec2& pos, float radius, float duration);

    // ---- 五关战役（JSON 只读配置 + 独立运行时状态）----
    // 每组运行状态单独存：配置保持只读（同一波可以反复重打）
    struct SpawnGroupRuntime
    {
        int spawned  = 0;
        float timer  = 0.0f;           // 从 -delay 起步，到 0 出第一只，之后每 interval 一只
    };
    enum class WaveState
    {
        Idle,      // 未开始
        Spawning,  // 正在按配置出怪
        Resting,   // 本波已清场，休整倒计时
        AllClear,  // 全部波次打完
    };
    bool loadCampaign();                 // 读 config/waves.json 的五关结构（失败用内置兜底）
    void activateLevel(size_t index, bool announceUnlocks);
    void startLevel(size_t index);
    const LevelConfig& currentLevel() const;
    void showSkillUnlocks(const std::vector<SkillId>& skills);
    void beginWave(size_t index);        // 开始某一波（重置该波运行时状态）
    void updateWaves(float delta);       // 每帧驱动波次状态机
    ax::Vec2 groupSpawnPos(const std::string& from, int lane) const;  // 按出生方向算出生点

    CampaignConfig _campaign;
    CampaignConfig _coreBattle;
    std::vector<WaveConfig> _waves;              // 当前关卡的波次镜像（只读）
    size_t _levelIndex   = 0;
    std::vector<SpawnGroupRuntime> _groupRt;     // 当前波各组运行状态
    size_t _waveIndex    = 0;
    WaveState _waveState = WaveState::Idle;
    float _restTimer     = 0.0f;
    ax::Label* _waveLabel = nullptr;             // HUD：顶部居中波次提示

    GameState _gameState                            = GameState::init;
    ax::EventListenerTouchAllAtOnce* _touchListener = nullptr;
    ax::EventListenerKeyboard* _keyboardListener    = nullptr;
    int _sceneID                                    = 0;
    ax::Node* _battlefield                          = nullptr;
    BattleLayout _battleLayout                      = BattleLayout::Oblique25D;
    // 记录上次布局时的可见尺寸，尺寸变化（旋转/窗口缩放）时自动重排战场
    ax::Size _lastVisibleSize                       = ax::Size::ZERO;
};

