import { _decorator, Component, Node, Label, Color, Layers } from 'cc';

// @ccclass 装饰器：把这个 TypeScript 类注册为 Cocos 引擎认识的"组件类型"。
// 引擎内部的场景序列化、组件系统都靠这个名字找到它。
const { ccclass } = _decorator;

/**
 * 第 1 课：Hello World —— 一个游戏的诞生
 *
 * 本脚本演示引擎的四个核心概念：
 *   1. 组件（Component）：挂在节点上的"行为单元"，游戏逻辑的最小载体
 *   2. 节点树（Scene Graph）：屏幕上的一切都是树上的节点
 *   3. 生命周期：start() 出生调一次，update(dt) 每帧调一次
 *   4. 游戏循环：引擎在背后执行 输入 → 更新 → 渲染，每秒 60 次
 *
 * 用法：在场景中任选一个节点（建议 Canvas），在属性检查器里
 * "添加组件 → 自定义脚本 → HelloWorld"。这是本课唯一一次
 * 使用编辑器界面，之后所有内容都由代码创建。
 */
@ccclass('HelloWorld')
export class HelloWorld extends Component {

    // private 成员：组件自己的"记忆"，外部不需要知道
    private helloLabel: Label | null = null;
    private elapsed: number = 0;

    /**
     * 生命周期：start()
     * 节点第一次进入场景、即将开始第一帧更新之前，被引擎调用一次。
     * 适合放"初始化"逻辑：创建节点、读配置、连接事件。
     */
    start() {
        // ---- 用代码创建一个节点 ----
        // Node 是场景树上的一个"格子"，本身不可见，
        // 只负责位置、旋转、缩放、父子关系。
        const labelNode = new Node('HelloLabel');
        this.node.addChild(labelNode);   // 挂到本节点下面，成为场景树的一部分

        // Layer：相机只渲染自己"看得见"的层。
        // 新建的节点默认在 DEFAULT 层（3D 层），UI 相机看不见它，
        // 必须改成 UI_2D，否则屏幕上什么都不显示——纯代码开发最常踩的坑。
        labelNode.layer = Layers.Enum.UI_2D;

        // ---- 给节点挂上一个 Label 组件，它才真正"可见" ----
        // 节点是骨架，组件是血肉：Label 组件让节点具备"绘制文字"的能力。
        this.helloLabel = labelNode.addComponent(Label);
        this.helloLabel.string = 'Hello, Defender!';
        this.helloLabel.fontSize = 48;
        this.helloLabel.color = Color.WHITE;
    }

    /**
     * 生命周期：update(dt)
     * 引擎的游戏循环每渲染一帧就调用一次。
     *
     * dt = delta time，上一帧到这一帧经过的真实秒数。
     * 为什么移动/动画都要乘 dt？因为帧率会波动（60fps、30fps 的机器
     * 每秒调用次数不同），用"秒"做单位才能保证任何机器上速度一致。
     */
    update(dt: number) {
        this.elapsed += dt;

        if (this.helloLabel) {
            // 让文字左右摇摆：sin 函数产生 -1 ~ 1 的周期值，放大 15 倍作为角度
            const angle = Math.sin(this.elapsed * 2) * 15;
            this.helloLabel.node.setRotationFromEuler(0, 0, angle);

            // 顺便观察帧率：每 60 帧在控制台打印一次 dt
            // （你会看到 dt ≈ 0.0167 秒 = 1/60，这就是"60 帧"的含义）
            if (Math.floor(this.elapsed * 60) % 60 === 0) {
                console.log(`运行中... dt = ${dt.toFixed(4)} 秒`);
            }
        }
    }
}

/* ================= 课后练习（自己写，别复制答案） =================
 *
 * 练习 1（参数直觉）：把 fontSize、摇摆幅度、摇摆速度各改一遍，
 *   每改一次重新预览，感受"改代码 → 看效果"的迭代循环。
 *
 * 练习 2（输入 → 游戏循环闭环）：让文字跟随手指。
 *   提示：在 start() 里注册触摸事件——
 *     import { input, Input, EventTouch, Vec3 } from 'cc';
 *     input.on(Input.EventType.TOUCH_START, this.onTouch, this);
 *   onTouch(event: EventTouch) 里用 event.getUILocation() 拿到
 *   触摸点的 UI 坐标，赋给 labelNode.setPosition(new Vec3(x, y, 0))。
 *   想一下：这对应游戏循环里的哪一环？
 *
 * 练习 3（为下一课埋伏笔）：触摸时 console.log 出触摸点坐标，
 *   观察屏幕中心和四个角的坐标值。下一课"射箭系统"要做的，
 *   就是从城堡位置指向这个坐标的一根向量。
 */
