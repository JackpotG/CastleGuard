#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
apply-wx-patches.py — CastleGuard 微信小游戏产物补丁脚本

为什么需要它：
  wx-transformer v1.0.35 对 Cocos2dx/Axmol 路径适配不全（详见 docs/04 排雷 13/16/17），
  每次重新转换后必须给产物重打三处补丁，否则小游戏无法运行。
  产物目录（wechat-build/）不入库，本脚本是"补丁的唯一权威来源"，进仓库、可复现。

用法：
  python3 tools/wechat/apply-wx-patches.py            # 打补丁（幂等，已打过的自动跳过）
  python3 tools/wechat/apply-wx-patches.py --check    # 只校验补丁是否齐全，退出码 0=齐全
  python3 tools/wechat/apply-wx-patches.py --target <minigame目录>  # 指定产物目录（测试用）

补丁清单（均带 [CastleGuard patch] 注释，产物里可搜索定位）：
  1. framework.js  —— rawData 时序修复（排雷 13）
  2. cg-compat.js  —— 环境垫片 matchMedia/createCanvas（排雷 16，新建文件）
  3. game.js       —— 首行引入 cg-compat.js（排雷 16）
  4. framework.js  —— createWindow 强制物理分辨率（排雷 17）
  5. wx-game-kit/config.json —— appID 对齐 + 关闭 timelog/monitor 弹窗（排雷 12/15）
  6. project.config.json —— appid 对齐（转换器会写成默认测试号，
     不一致时开发者工具报"登录用户不是该小程序的开发者"）
"""
import json
import sys
from pathlib import Path

SCRIPT = Path(__file__).resolve()
ROOT = SCRIPT.parents[2]                      # 仓库根目录（tools/wechat/ 的上两级）
DEFAULT_TARGET = ROOT / "tools/wechat/wx-transformer/wechat-build/minigame"
TRANSFORM_CONFIG = ROOT / "code/axmol/CastleGuard/wechat-transform.config.json"

# ---------------------------------------------------------------------------
# 补丁 2 的本体：cg-compat.js 环境垫片（每次运行完整重写，保证与仓库一致）
# ---------------------------------------------------------------------------
CG_COMPAT_JS = """// [CastleGuard patch] 微信小游戏运行环境兼容性补丁（由 apply-wx-patches.py 生成，勿手改）
// 排雷 16：小游戏没有 window.matchMedia，Axmol 用它判断触屏 → 固定按触屏返回
// 时序坑（2026-11-17 iOS 真机踩中）：本文件先于 wx-game-kit 执行，真机上 window 全局
// 由插件异步创建，此刻 window 可能尚不存在，一次性安装会落空 → 拦截 window 的首次赋值
// + 短轮询兜底，保证 window 一出现就装上 matchMedia。
function cgInstallMatchMedia(win) {
  if (!win || typeof win.matchMedia === 'function') return;
  win.matchMedia = function (query) {
    var q = String(query);
    return {
      matches: /pointer\\s*:\\s*coarse/.test(q),
      media: q,
      addListener: function () {},
      removeListener: function () {},
      addEventListener: function () {},
      removeEventListener: function () {},
      dispatchEvent: function () { return false; }
    };
  };
}
(function () {
  var G = (typeof GameGlobal !== 'undefined') ? GameGlobal :
          (typeof globalThis !== 'undefined' ? globalThis : this);
  var _win = G.window;
  if (_win) cgInstallMatchMedia(_win);
  // 拦截后续对 window 全局的赋值（wx-game-kit 初始化时创建 window 走这条路）
  try {
    Object.defineProperty(G, 'window', {
      configurable: true,
      enumerable: true,
      get: function () { return _win; },
      set: function (v) { _win = v; cgInstallMatchMedia(v); }
    });
  } catch (e) {}
  // 轮询兜底：若插件用 Object.defineProperty 重建 window（绕过赋值拦截）也能覆盖到；
  // window 对象被整体替换时重新安装。约 10 秒后停轮询（引擎早已初始化完）。
  var polls = 0;
  var timer = setInterval(function () {
    try {
      var w = G.window;
      if (w) {
        if (w !== _win) _win = w;
        cgInstallMatchMedia(w);
        if (++polls > 600) clearInterval(timer);
      }
    } catch (e) {}
  }, 16);
})();

// 排雷 17 辅助：转换器把 glfwGetWindowSize 改成物理分辨率，
// 主屏 canvas 缓冲区在部分环境是 CSS 尺寸 → 强制对齐，避免视口裁切
if (typeof wx !== 'undefined' && typeof wx.createCanvas === 'function' && !wx.__cgCanvasPatched) {
  wx.__cgCanvasPatched = true;
  var _cgOrigCreateCanvas = wx.createCanvas.bind(wx);
  var _cgFirstCanvas = true;
  wx.createCanvas = function () {
    var c = _cgOrigCreateCanvas();
    if (_cgFirstCanvas) {
      _cgFirstCanvas = false;
      try {
        var si = wx.getSystemInfoSync();
        var pw = si.screenWidth * si.pixelRatio;
        var ph = si.screenHeight * si.pixelRatio;
        if (pw > 0 && ph > 0 && (c.width !== pw || c.height !== ph)) {
          c.width = pw;
          c.height = ph;
        }
      } catch (e) {}
    }
    return c;
  };
}

// 诊断：打印三层尺寸（排查分辨率问题时保留，正式发布可删）
try {
  var _si = wx.getSystemInfoSync();
  console.log('[CG-DIAG] systemInfo: screen=' + _si.screenWidth + 'x' + _si.screenHeight +
    ' pixelRatio=' + _si.pixelRatio + ' window=' + _si.windowWidth + 'x' + _si.windowHeight +
    ' platform=' + _si.platform + ' model=' + _si.model);
} catch (e) { console.log('[CG-DIAG] systemInfo error', e); }
try {
  console.log('[CG-DIAG] window shim: innerWidth=' + (typeof window !== 'undefined' ? window.innerWidth : 'n/a') +
    ' innerHeight=' + (typeof window !== 'undefined' ? window.innerHeight : 'n/a') +
    ' devicePixelRatio=' + (typeof window !== 'undefined' ? window.devicePixelRatio : 'n/a'));
} catch (e) {}
setTimeout(function () {
  try {
    var cv = (typeof GameGlobal !== 'undefined' && GameGlobal.canvas) ? GameGlobal.canvas : null;
    if (!cv && typeof Module !== 'undefined') cv = Module.canvas;
    if (!cv && typeof document !== 'undefined' && document.getElementById) cv = document.getElementById('canvas');
    console.log('[CG-DIAG] main canvas: ' + (cv ? (cv.width + 'x' + cv.height) : 'NOT FOUND'));
  } catch (e) { console.log('[CG-DIAG] canvas error', e); }
}, 3000);
"""

# ---------------------------------------------------------------------------
# 补丁 1a/1b/4 的锚点与替换内容（锚点取自转换器生成的原始代码，换版本需复核）
# ---------------------------------------------------------------------------

# 补丁 1a：在数据处理器入口捕获 rawData（插件回调返回后会把 rawData 置 null）
P1A_ANCHOR = "WXGameKit.loader.setCustomProcessRawDataHandler(function (Module) {\n  (() => {"
P1A_INSERT = (
    "WXGameKit.loader.setCustomProcessRawDataHandler(function (Module) {\n"
    "  // [CastleGuard patch] 插件在本回调返回后会把 Module.rawData 置 null，\n"
    "  // 而 runWithFS 经 preRun 延迟执行，必须在入口处立即捕获数据引用。\n"
    "  var _cgRawData = Module.rawData;\n"
    "  (() => {"
)
P1A_MARKER = "var _cgRawData = Module.rawData;"

# 补丁 1b：有 rawData 时直接消费，跳过浏览器 IndexedDB/fetch 流程
P1B_ANCHOR = "        openDatabase(\n          (db) => checkCachedPackage(db, PACKAGE_PATH + PACKAGE_NAME,"
P1B_INSERT = (
    "        // [CastleGuard patch] 微信小游戏环境无 indexedDB/fetch：直接消费插件给的数据\n"
    "        if (_cgRawData) {\n"
    "          processPackageData(_cgRawData);\n"
    "        } else\n"
    "        openDatabase(\n"
    "          (db) => checkCachedPackage(db, PACKAGE_PATH + PACKAGE_NAME,"
)
P1B_MARKER = "if (_cgRawData) {"

# 补丁 4：GLFW 窗口强制物理分辨率
P4_ANCHOR = "  createWindow:(width, height, title, monitor, share) => {\n        var i, id;"
P4_INSERT = (
    "  createWindow:(width, height, title, monitor, share) => {\n"
    "        // [CastleGuard patch] 微信小游戏：窗口必须等于物理屏幕分辨率，\n"
    "        // 否则 canvas 缓冲区与屏幕显示坐标系错位，画面裁剪偏移（排雷 17）\n"
    "        try {\n"
    "          var _si = wx.getSystemInfoSync();\n"
    "          var _pw = _si.screenWidth * _si.pixelRatio;\n"
    "          var _ph = _si.screenHeight * _si.pixelRatio;\n"
    "          if (_pw > 0 && _ph > 0) { width = _pw; height = _ph; }\n"
    "        } catch (e) {}\n"
    "        var i, id;"
)
P4_MARKER = "窗口必须等于物理屏幕分辨率"

# 补丁 3：game.js 首行引入垫片
P3_LINE = "import './cg-compat'; // [CastleGuard patch] 环境补丁须最先执行\n"


class PatchError(Exception):
    pass


def patch_file_once(path: Path, anchor: str, insert: str, marker: str, name: str, check_only: bool) -> str:
    """幂等替换：marker 已存在则跳过；anchor 找不到则报错（转换器版本可能变了）。"""
    text = path.read_text(encoding="utf-8")
    if marker in text:
        return f"[跳过] {name}（已存在）"
    if check_only:
        raise PatchError(f"[缺失] {name}")
    if anchor not in text:
        raise PatchError(
            f"[失败] {name}：找不到锚点代码。wx-transformer 版本可能已变更，"
            f"请对照 docs/04 排雷档案人工复核补丁位置。"
        )
    path.write_text(text.replace(anchor, insert, 1), encoding="utf-8")
    return f"[应用] {name}"


def main() -> int:
    args = sys.argv[1:]
    check_only = "--check" in args
    target = DEFAULT_TARGET
    if "--target" in args:
        target = Path(args[args.index("--target") + 1]).resolve()

    if not target.is_dir():
        print(f"错误：产物目录不存在 {target}\n请先运行 wx-transformer 转换。")
        return 1

    framework = target / "framework.js"
    game_js = target / "game.js"
    compat = target / "cg-compat.js"
    kit_config = target / "wx-game-kit/config.json"
    for f in (framework, game_js, kit_config):
        if not f.is_file():
            print(f"错误：产物缺少文件 {f}")
            return 1

    results = []
    try:
        # 补丁 1：rawData 时序（两处）
        results.append(patch_file_once(framework, P1A_ANCHOR, P1A_INSERT, P1A_MARKER,
                                       "补丁1a framework.js 入口捕获 rawData", check_only))
        results.append(patch_file_once(framework, P1B_ANCHOR, P1B_INSERT, P1B_MARKER,
                                       "补丁1b framework.js 跳过浏览器装载流程", check_only))
        # 补丁 4：createWindow 物理分辨率
        results.append(patch_file_once(framework, P4_ANCHOR, P4_INSERT, P4_MARKER,
                                       "补丁4  framework.js createWindow 物理分辨率", check_only))
        # 补丁 2：cg-compat.js（权威内容，直接重写）
        if check_only:
            if not compat.is_file() or "matchMedia" not in compat.read_text(encoding="utf-8"):
                raise PatchError("[缺失] 补丁2  cg-compat.js 环境垫片")
            results.append("[跳过] 补丁2  cg-compat.js 环境垫片（已存在）")
        else:
            compat.write_text(CG_COMPAT_JS, encoding="utf-8")
            results.append("[应用] 补丁2  cg-compat.js 环境垫片（重写为仓库权威版本）")
        # 补丁 3：game.js 首行引入
        game_text = game_js.read_text(encoding="utf-8")
        if "cg-compat" in game_text:
            results.append("[跳过] 补丁3  game.js 引入垫片（已存在）")
        elif check_only:
            raise PatchError("[缺失] 补丁3  game.js 引入垫片")
        else:
            game_js.write_text(P3_LINE + game_text, encoding="utf-8")
            results.append("[应用] 补丁3  game.js 首行引入 cg-compat.js")
        # 补丁 5：config.json 对齐 appID + 关闭开发弹窗
        cfg = json.loads(kit_config.read_text(encoding="utf-8"))
        app_id = None
        if TRANSFORM_CONFIG.is_file():
            app_id = json.loads(TRANSFORM_CONFIG.read_text(encoding="utf-8")).get("appID")
        changed = []
        if app_id and cfg.get("project", {}).get("app", {}).get("appID") != app_id:
            cfg["project"]["app"]["appID"] = app_id
            changed.append(f"appID→{app_id}")
        dev = cfg.setdefault("develop", {})
        if dev.get("hideTimeLogModal") is not True:
            dev["hideTimeLogModal"] = True
            changed.append("关闭timelog弹窗")
        mon = dev.setdefault("monitor", {})
        for k in ("enable", "showSuggestModal", "showResultAfterLaunch"):
            if mon.get(k) is not False:
                mon[k] = False
                changed.append(f"monitor.{k}=false")
        if changed:
            if check_only:
                raise PatchError(f"[缺失] 补丁5  config.json（{', '.join(changed)}）")
            kit_config.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")
            results.append(f"[应用] 补丁5  config.json（{', '.join(changed)}）")
        else:
            results.append("[跳过] 补丁5  config.json（已就绪）")
        # 补丁 6：project.config.json 的 appid 对齐（转换器会写成默认测试号，
        # 不一致时开发者工具报"登录用户不是该小程序的开发者"）
        proj_cfg_path = target / "project.config.json"
        if not proj_cfg_path.is_file():
            raise PatchError("[失败] 补丁6  缺少 project.config.json")
        pcfg = json.loads(proj_cfg_path.read_text(encoding="utf-8"))
        if app_id and pcfg.get("appid") != app_id:
            if check_only:
                raise PatchError(f"[缺失] 补丁6  project.config.json appid（当前 {pcfg.get('appid')}）")
            pcfg["appid"] = app_id
            proj_cfg_path.write_text(json.dumps(pcfg, ensure_ascii=False, indent=2), encoding="utf-8")
            results.append(f"[应用] 补丁6  project.config.json appid→{app_id}")
        else:
            results.append("[跳过] 补丁6  project.config.json（已就绪）")
    except PatchError as e:
        print(str(e))
        return 2

    for r in results:
        print(r)
    print("校验通过：补丁齐全。" if check_only else "完成：产物已可直接导入微信开发者工具。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
