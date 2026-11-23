# CastleGuard 配置工具

在仓库根目录运行：

```bash
python3 tools/config-editor/server.py
```

浏览器打开 `http://127.0.0.1:8780`。左侧切换关卡或成长节点，中间查看波次、编队和技能树，右侧修改当前出怪组或成长节点。顶部“保存配置”直接写入以下源文件：

- `code/axmol/CastleGuard/Content/config/waves.json`
- `code/axmol/CastleGuard/Content/config/upgrades.json`

关卡组的“使用指定移速”对应 `useConfiguredSpeed`。开启后填写 `configuredSpeed`（像素/秒）；整组怪物使用这个绝对速度，关闭时沿用怪物类型倍率和个体随机速度。当前第 7 关的混合横排是示例。

技能树工具编辑既有 ID 的布局、前置关系和数值。新升级/法术 ID 仍需要先在游戏的 C++ 枚举与效果逻辑中接入；编辑器不会凭空创建可运行的新技能。页面提供快速输入检查，保存后仍应运行游戏测试，最终以游戏自己的配置解析结果为准。

服务只监听本机 `127.0.0.1`。保存采用文件版本比对和原子替换：如果配置被别处修改，页面会要求重新载入，避免覆盖新内容。两个页面的未保存状态相互独立；保存按钮保存当前页面。

服务测试：

```bash
python3 -m unittest discover -s tools/config-editor -p 'test_*.py'
```
