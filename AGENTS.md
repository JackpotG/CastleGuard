# CastleGuard —— 项目指令（迁移自 Kimi Chat 项目）

本项目是以复刻优化《Defender II》为主线的游戏开发学习项目，按 Axmol（微信小游戏）→ Unity → UE 三引擎串行推进。请始终以项目文件中的上下文文档和开发清单为准回答问题；回答侧重"教会我"而非替我做完，代码要解释关键概念；当前阶段以开发清单中未勾选的最早任务为准。

## 知识点归档（铁律）

Stark 的概念性提问（"X 是什么意思""为什么要这样"等）说明他需要学习这个点：
解答后必须把问答整理进 `docs/05-知识点手册.md`（问题 → 一句话答案 → 详解要点 →
关联代码/文档 → 日期 → 延伸可学），按主题分类归档，并在提交代码时一并提交。

## 项目资料位置（均以本仓库根目录为基准）

- `README.md`：项目总览、三条铁律、当前状态
- `docs/01-项目上下文.md`：原作分析、优化方向、MVP 范围、合规红线、技术路线备忘
- `docs/02-学习路线图.md`：三引擎阶段规划、渲染学习路径、里程碑
- `docs/03-第一阶段-Cocos开发清单.md`：第 1~7 周任务清单（M0~M3，含勾选状态）
- `docs/04-环境搭建与工具链.md`：M0 搭建记录（工具链讲解、常用命令速查、排雷档案）
- `docs/05-知识点手册.md`：概念问答知识库（Stark 的提问 = 学习点，解答后必须归档，见下方铁律）
- `engine/axmol/`：Axmol v2.11.5 引擎源码与自包含工具链（**不入库**，按 docs/04 重新拉取；1kiss.ps1 需打本地补丁：允许无 Xcode 纯 CLT 构建）
- `code/axmol/CastleGuard/`：当前游戏工程（C++ 竖屏，包名 dev.stark.castleguard）
- `code/cocos/01-hello-world/`：旧 Cocos Creator 路线残留，仅留档勿用
- `tools/wechat/`：微信转换工具 wx-transformer v1.0.35 与能力 SDK v1.0.3（二进制不入库）

## 环境要点

- 技术路线（2026-11-13 定稿）：**Axmol（Cocos2d-x C++ 裸引擎）+ WASM → 微信小游戏**，不用 Cocos Creator。如文档与旧表述冲突，以文档为准。
- 本机不装完整 Xcode，桌面端用 CLT + Ninja 构建；WASM 端用 emsdk **3.1.73**（3.1.51/3.1.53 缺 `AL/alext.h`，不可用）。
- 微信小游戏产物由 wx-transformer 生成，导入开发者工具时须从**「小游戏」标签页**导入；运行依赖官方 WXGameKit 插件（wxe5a48f1ed5f544b7），**测试号无法授权插件，必须用正式小游戏 AppID**（当前 wx4aa6e6f072d866eb）；产物内 `wx-game-kit/config.json` 的 appID、project.config.json 的 appid、源转换配置的 appID 三处必须一致。
- wx-transformer v1.0.35 对 Cocos2dx 适配不全，产物需打补丁（rawData 时序、matchMedia、createWindow 分辨率、appID 对齐等）：每次重跑转换器后执行 `python3 tools/wechat/apply-wx-patches.py`（幂等，`--check` 可校验），锚点失效时对照 docs/04 排雷 13/16/17 人工复核。
- 微信开发者工具 Stable 2.02.2609250 (macOS arm64) 官方包有自检 bug（缺 package.nw/consistency.dat），需绕过 Go 启动器直接启动 Electron 内核，详见 docs/04 排雷档案。
