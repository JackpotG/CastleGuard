"""The desktop host must get a fresh service and shut down only the service it owns."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from urllib.request import urlopen


class PlannerServiceTest(unittest.TestCase):
    def test_dynamic_ports_and_clean_shutdown(self):
        with tempfile.TemporaryDirectory() as directory:
            processes = []
            try:
                addresses = []
                for index in range(2):
                    ready = Path(directory) / f"ready-{index}.json"
                    process = subprocess.Popen([sys.executable, str(Path(__file__).with_name("server.py")),
                                                "--port", "0", "--ready-file", str(ready)],
                                               env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"},
                                               stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
                    processes.append((process, ready))
                    deadline = time.monotonic() + 10
                    while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(.02)
                    self.assertTrue(ready.exists(), "desktop service never became ready")
                    address = json.loads(ready.read_text())
                    self.assertEqual(address["pid"], process.pid)
                    addresses.append(address)
                    with urlopen(f'http://127.0.0.1:{address["port"]}/planner') as response:
                        self.assertEqual(response.status, 200)
                self.assertNotEqual(addresses[0]["port"], addresses[1]["port"])
                first, ready = processes[0]
                first.terminate()
                first.wait(timeout=5)
                self.assertEqual(first.returncode, 0)
                self.assertFalse(ready.exists())
                # Closing one app instance must not stop another instance's service.
                with urlopen(f'http://127.0.0.1:{addresses[1]["port"]}/api/levels') as response:
                    self.assertIn("levels", json.load(response)["data"])
            finally:
                for process, _ in processes:
                    if process.poll() is None:
                        process.terminate()
                    process.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
