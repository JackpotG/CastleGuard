import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

from game_launcher import GameLauncher, GameLaunchError

REAL_POPEN = subprocess.Popen


class GameLauncherTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.launcher = GameLauncher(self.directory.name)
        self.launcher.build.mkdir(parents=True)
        self.cmake = Path(self.directory.name) / "tools with spaces/cmake"
        self.cmake.parent.mkdir()
        self.cmake.touch()
        self.cmake.chmod(0o755)
        (self.launcher.build / "CMakeCache.txt").write_text(
            f"CMAKE_COMMAND:INTERNAL={self.cmake}\n"
            f"CMAKE_HOME_DIRECTORY:INTERNAL={self.launcher.game}\n"
            "CMAKE_MAKE_PROGRAM:FILEPATH=/opt/homebrew/bin/ninja\n")
        executable = self.launcher.bundle / "Contents/MacOS/CastleGuard"
        executable.parent.mkdir(parents=True)
        executable.touch()
        for parent in (self.launcher.game / "Content/config",
                       self.launcher.bundle / "Contents/Resources/config"):
            parent.mkdir(parents=True)
            (parent / "waves.json").write_text('{"levels": ["saved"]}')
        self.process = Mock()
        self.process.wait.return_value = 0
        self.process.poll.return_value = 0
        self.platform = patch("game_launcher.sys.platform", "darwin")
        self.platform.start()
        self.running = patch("game_launcher.subprocess.run", return_value=Mock(returncode=1))
        self.running.start()
        self.spawn = patch("game_launcher.subprocess.Popen", return_value=self.process)
        self.popen = self.spawn.start()

    def tearDown(self):
        self.launcher.close()
        self.spawn.stop()
        self.running.stop()
        self.platform.stop()
        self.launcher.lock_path.unlink(missing_ok=True)
        self.launcher.log_path.unlink(missing_ok=True)
        self.directory.cleanup()

    def test_prepares_saved_resources_before_system_launch_with_minimal_path(self):
        with patch.dict(os.environ, {"PATH": "/usr/bin:/bin"}, clear=True):
            self.assertIn("已保存", self.launcher.launch()["message"])
        build, launch = self.popen.call_args_list
        self.assertEqual(build.args[0], [str(self.cmake), "--build", str(self.launcher.build),
                                        "--target", "CastleGuard_prepare", "-j", "6"])
        self.assertIn("/opt/homebrew/bin", build.kwargs["env"]["PATH"])
        self.assertEqual(build.kwargs["env"]["AX_ROOT"], str(self.launcher.root / "engine/axmol"))
        self.assertEqual(launch.args[0], ["/usr/bin/open", "-n", "-a", str(self.launcher.bundle)])
        self.assertTrue(launch.kwargs["start_new_session"])

    def test_build_failure_never_opens_stale_game_and_can_retry(self):
        self.process.wait.return_value = 1
        with self.assertRaisesRegex(GameLaunchError, "游戏准备失败"):
            self.launcher.launch()
        self.assertEqual(self.popen.call_count, 1)
        self.process.wait.return_value = 0
        self.launcher.launch()
        self.assertEqual(self.popen.call_count, 3)

    def test_missing_or_moved_build_has_readable_error(self):
        cache = self.launcher.build / "CMakeCache.txt"
        cache.write_text(cache.read_text().replace(str(self.launcher.game), "/moved/game"))
        with self.assertRaisesRegex(GameLaunchError, "路径已失效"):
            self.launcher.launch()
        cache.unlink()
        with self.assertRaisesRegex(GameLaunchError, "尚未配置"):
            self.launcher.launch()
        self.popen.assert_not_called()

    def test_running_game_is_preserved(self):
        with patch("game_launcher.subprocess.run", return_value=Mock(returncode=0)):
            with self.assertRaisesRegex(GameLaunchError, "先退出游戏"):
                self.launcher.launch()
        self.popen.assert_not_called()

    def test_unsynced_saved_configuration_blocks_launch(self):
        (self.launcher.game / "Content/config/waves.json").write_text('{"levels": ["new save"]}')
        with self.assertRaisesRegex(GameLaunchError, "尚未全部同步"):
            self.launcher.launch()
        self.assertEqual(self.popen.call_count, 1)

    def test_concurrent_services_cannot_prepare_same_game(self):
        other = GameLauncher(self.launcher.root)
        with self.launcher.lock_path.open("a") as lock:
            import fcntl
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            with self.assertRaisesRegex(GameLaunchError, "另一个配置窗口"):
                other.launch()
        self.popen.assert_not_called()

    def test_timeout_stops_owned_process_group_without_opening_game(self):
        self.process.wait.side_effect = [subprocess.TimeoutExpired("build", 600), 0]
        self.process.poll.return_value = None
        self.process.pid = 12345
        with patch("game_launcher.os.killpg") as kill:
            with self.assertRaisesRegex(GameLaunchError, "超时"):
                self.launcher.launch()
        kill.assert_called_once_with(12345, 15)
        self.assertEqual(self.popen.call_count, 1)

    def test_system_launcher_failure_restores_availability(self):
        self.process.wait.side_effect = [0, 1]
        with self.assertRaisesRegex(GameLaunchError, "游戏未能打开"):
            self.launcher.launch()
        self.assertFalse(self.launcher._launch_lock.locked())

    def test_unresponsive_build_is_killed_and_reaped(self):
        self.process.poll.return_value = None
        self.process.pid = 12345
        self.process.wait.side_effect = [subprocess.TimeoutExpired("build", 3), 0]
        with patch("game_launcher.os.killpg") as kill:
            self.launcher._stop(self.process)
        self.assertEqual([call.args for call in kill.call_args_list], [(12345, 15), (12345, 9)])

    def test_closed_planner_cannot_start_preparation(self):
        self.launcher.close()
        with self.assertRaisesRegex(GameLaunchError, "已取消"):
            self.launcher.launch()
        self.popen.assert_not_called()

    def test_close_cancels_real_preparation_without_leaving_child(self):
        # An isolated sleeping child substitutes for the build, never the GUI game.
        with patch("game_launcher.subprocess.Popen") as spawn:
            started = threading.Event()

            def start(*args, **kwargs):
                child = REAL_POPEN(*args, **kwargs)
                started.set()
                return child

            spawn.side_effect = start
            failures = []

            def prepare():
                try:
                    with tempfile.TemporaryFile() as output:
                        self.launcher._run([sys.executable, "-c", "import time; time.sleep(30)"],
                                           output, os.environ.copy(), 40, "准备已取消")
                except GameLaunchError as error:
                    failures.append(error)

            thread = threading.Thread(target=prepare)
            thread.start()
            self.assertTrue(started.wait(timeout=3))
            self.launcher.close()
            thread.join(timeout=5)
            self.assertFalse(thread.is_alive())
            self.assertEqual(len(failures), 1)
            self.assertIsNone(self.launcher._process)


if __name__ == "__main__":
    unittest.main()
