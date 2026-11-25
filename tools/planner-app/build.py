"""Build a small native macOS host using the installed Command Line Tools."""
import os
from pathlib import Path
import platform
import plistlib
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
APP = ROOT / "游戏策划配置.app"
BUNDLE_ID = "dev.stark.castleguard.planner"


def build():
    if sys.platform != "darwin":
        raise RuntimeError("桌面应用需要在 macOS 构建；其他系统可直接使用浏览器配置工具。")
    workspace = HERE / "build"
    workspace.mkdir(exist_ok=True)
    sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], text=True).strip()
    flags = ["-sdk", sdk, "-module-cache-path", str(workspace / "module-cache")]
    with tempfile.TemporaryDirectory(prefix="app-", dir=workspace) as folder:
        bundle = Path(folder) / APP.name
        executable = bundle / "Contents/MacOS/CastleGuardPlanner"
        resources = bundle / "Contents/Resources"
        executable.parent.mkdir(parents=True)
        resources.mkdir()
        subprocess.run(["xcrun", "swiftc", str(HERE / "main.swift"), *flags,
                        "-target", f"{platform.machine()}-apple-macosx13.0", "-O",
                        "-framework", "AppKit", "-framework", "WebKit", "-o", str(executable)], check=True)
        iconset = Path(folder) / "Planner.iconset"
        iconset.mkdir()
        subprocess.run(["xcrun", "swift", *flags, str(HERE / "render-icon.swift"), str(iconset)], check=True)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(resources / "Planner.icns")], check=True)
        info = {
            "CFBundleName": "游戏策划配置", "CFBundleDisplayName": "游戏策划配置",
            "CFBundleIdentifier": BUNDLE_ID, "CFBundleExecutable": executable.name,
            "CFBundlePackageType": "APPL", "CFBundleShortVersionString": "1.0", "CFBundleVersion": "1",
            "CFBundleIconFile": "Planner.icns", "LSMinimumSystemVersion": "13.0",
            "NSHighResolutionCapable": True,
            "NSAppTransportSecurity": {"NSAllowsLocalNetworking": True},
            "PlannerProjectRoot": str(ROOT), "PlannerPython": str(Path(sys.executable).resolve()),
        }
        (bundle / "Contents/Info.plist").write_bytes(plistlib.dumps(info))
        subprocess.run(["codesign", "--force", "--sign", "-", str(bundle)], check=True)
        if APP.exists():
            previous = plistlib.loads((APP / "Contents/Info.plist").read_bytes())
            if previous.get("CFBundleIdentifier") != BUNDLE_ID:
                raise RuntimeError(f"目标位置已有其他应用，未覆盖：{APP}")
            shutil.rmtree(APP)
        os.replace(bundle, APP)
    print(f"[PlannerApp] 已生成：{APP}")
    return APP


if __name__ == "__main__":
    try:
        build()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"[PlannerApp] 构建失败：{error}", file=sys.stderr)
        sys.exit(1)
