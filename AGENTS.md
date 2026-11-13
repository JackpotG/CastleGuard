# CastleGuard —— 项目指令（迁移自 Kimi Chat 项目）

本项目是以复刻优化《Defender II》为主线的游戏开发学习项目，按 Axmol（微信小游戏）→ Unity → UE 三引擎串行推进。请始终以项目文件中的上下文文档和开发清单为准回答问题；回答侧重"教会我"而非替我做完，代码要解释关键概念；当前阶段以开发清单中未勾选的最早任务为准。

## 项目资料位置（均以本仓库根目录为基准）

- `README.md`：项目总览、三条铁律、当前状态
- `docs/01-项目上下文.md`：原作分析、优化方向、MVP 范围、合规红线、技术路线备忘
- `docs/02-学习路线图.md`：三引擎阶段规划、渲染学习路径、里程碑
- `docs/03-第一阶段-Cocos开发清单.md`：第 1~7 周任务清单（M0~M3，含勾选状态）
- `docs/04-环境搭建与工具链.md`：M0 搭建记录（工具链讲解、常用命令速查、排雷档案）
- `engine/axmol/`：Axmol v2.11.5 引擎源码与自包含工具链（**不入库**，按 docs/04 重新拉取；1kiss.ps1 需打本地补丁：允许无 Xcode 纯 CLT 构建）
- `code/axmol/CastleGuard/`：当前游戏工程（C++ 竖屏，包名 dev.stark.castleguard）
- `code/cocos/01-hello-world/`：旧 Cocos Creator 路线残留，仅留档勿用
- `tools/wechat/`：微信转换工具 wx-transformer v1.0.35 与能力 SDK v1.0.3（二进制不入库）

## 环境要点

- 技术路线（2026-11-13 定稿）：**Axmol（Cocos2d-x C++ 裸引擎）+ WASM → 微信小游戏**，不用 Cocos Creator。如文档与旧表述冲突，以文档为准。
- 本机不装完整 Xcode，桌面端用 CLT + Ninja 构建；WASM 端用 emsdk **3.1.73**（3.1.51/3.1.53 缺 `AL/alext.h`，不可用）。
- 微信小游戏产物由 wx-transformer 生成，导入开发者工具时须从**「小游戏」标签页**导入并选择**测试号**；产物内 `wx-game-kit/config.json` 的 appID 必须与当前测试号一致。
- 微信开发者工具 Stable 2.02.2609250 (macOS arm64) 官方包有自检 bug（缺 package.nw/consistency.dat），需绕过 Go 启动器直接启动 Electron 内核，详见 docs/04 排雷档案。
