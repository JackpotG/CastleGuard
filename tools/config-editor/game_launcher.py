"""Prepare the saved game resources, then hand GUI startup to macOS."""

import fcntl
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading


class GameLaunchError(Exception):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


class GameLauncher:
    def __init__(self, project_root):
        self.root = Path(project_root).resolve()
        self.game = self.root / "code/axmol/CastleGuard"
        self.build = self.game / "build"
        self.bundle = self.build / "bin/CastleGuard/CastleGuard.app"
        key = hashlib.sha256(str(self.root).encode()).hexdigest()[:16]
        self.lock_path = Path(tempfile.gettempdir()) / f"castleguard-game-{key}.lock"
        self.log_path = self.lock_path.with_suffix(".log")
        self._launch_lock = threading.Lock()
        self._process_lock = threading.Lock()
        self._process = None
        self._closed = False

    @staticmethod
    def _stop(process):
        if process.poll() is None:
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()

    def close(self):
        # Closing the planner cancels its preparation, never the independent game.
        with self._process_lock:
            self._closed = True
            if self._process:
                self._stop(self._process)

    def _run(self, command, output, environment, timeout, failure):
        with self._process_lock:
            if self._closed:
                raise GameLaunchError(503, "配置工具已关闭，已取消打开游戏")
            process = subprocess.Popen(command, cwd=self.root, env=environment,
                                       stdout=output, stderr=subprocess.STDOUT,
                                       start_new_session=True)
            self._process = process
        try:
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self._stop(process)
                raise GameLaunchError(504, f"游戏准备超时，请稍后重试。启动记录：{self.log_path}")
            if code:
                raise GameLaunchError(500, f"{failure}。启动记录：{self.log_path}")
        finally:
            with self._process_lock:
                self._process = None

    def _check_running(self):
        result = subprocess.run(["/usr/bin/pgrep", "-x", "CastleGuard"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                timeout=5, check=False)
        if result.returncode == 0:
            raise GameLaunchError(409, "游戏已经打开，请先退出游戏再打开，以载入新配置")
        if result.returncode != 1:
            raise GameLaunchError(503, "暂时无法检查游戏状态，请稍后重试")

    def _build_environment(self):
        cache = self.build / "CMakeCache.txt"
        if not cache.is_file():
            raise GameLaunchError(503, "尚未配置桌面版游戏，请先按环境搭建文档完成桌面构建")
        entries = {}
        for line in cache.read_text(encoding="utf-8").splitlines():
            if "=" in line and not line.startswith(("#", "//")):
                key, value = line.split("=", 1)
                entries[key.split(":", 1)[0]] = value
        cmake = Path(entries.get("CMAKE_COMMAND", ""))
        source = Path(entries.get("CMAKE_HOME_DIRECTORY", ""))
        if source.resolve() != self.game or not cmake.is_file() or not os.access(cmake, os.X_OK):
            raise GameLaunchError(503, "桌面构建路径已失效，请按环境搭建文档重新配置桌面版")
        environment = os.environ.copy()
        environment["AX_ROOT"] = str(self.root / "engine/axmol")
        paths = [str(cmake.parent), str(Path(entries.get("CMAKE_MAKE_PROGRAM", "/usr/bin/make")).parent),
                 "/usr/bin", "/bin", "/usr/sbin", "/sbin", environment.get("PATH", "")]
        environment["PATH"] = os.pathsep.join(paths)
        return str(cmake), environment

    def _check_resources(self):
        if not (self.bundle / "Contents/MacOS/CastleGuard").is_file():
            raise GameLaunchError(500, "未找到桌面版游戏，请重新构建后再打开")
        sources = self.game / "Content/config"
        resources = self.bundle / "Contents/Resources/config"
        for source in sources.glob("*.json"):
            # Release builds intentionally omit this debug-only fixture.
            if source.name == "core-waves.json":
                continue
            target = resources / source.name
            if not target.is_file() or source.read_bytes() != target.read_bytes():
                raise GameLaunchError(409, "配置尚未全部同步到游戏，请保存后重新打开游戏")

    def launch(self):
        if sys.platform != "darwin":
            raise GameLaunchError(501, "打开游戏目前支持 macOS 桌面版")
        if not self._launch_lock.acquire(blocking=False):
            raise GameLaunchError(409, "正在准备游戏，请稍候")
        try:
            # Multiple planner windows/services must not build the same bundle together.
            with self.lock_path.open("a") as lock:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                except BlockingIOError:
                    raise GameLaunchError(409, "另一个配置窗口正在准备游戏，请稍候")
                self._check_running()
                cmake, environment = self._build_environment()
                print(f"[PlannerGame] preparing saved configs; log={self.log_path}", flush=True)
                with self.log_path.open("w", encoding="utf-8") as output:
                    self._run([cmake, "--build", str(self.build), "--target", "CastleGuard_prepare", "-j", "6"],
                              output, environment, 600, "游戏准备失败")
                    self._check_resources()
                    self._check_running()
                    self._run(["/usr/bin/open", "-n", "-a", str(self.bundle)],
                              output, environment, 20, "游戏未能打开")
                print("[PlannerGame] handed game startup to macOS", flush=True)
                return {"message": "游戏正在打开，使用已保存的配置"}
        except (OSError, subprocess.SubprocessError) as error:
            print(f"[PlannerGame] launch failed: {error}", file=sys.stderr, flush=True)
            raise GameLaunchError(503, "暂时无法打开游戏，请检查桌面版环境后重试") from error
        finally:
            self._launch_lock.release()
