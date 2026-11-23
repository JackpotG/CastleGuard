import json
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from server import CONFIG_PATHS, MONSTER_CONFIG_PATH, PROJECT_ROOT, make_handler


class ConfigEditorServerTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        for kind, relative in CONFIG_PATHS.items():
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(json.dumps({"levels": []} if kind == "levels" else
                                         {"upgrades": [], "skills": []}), encoding="utf-8")
        monster_target = self.root / MONSTER_CONFIG_PATH
        monster_target.parent.mkdir(parents=True, exist_ok=True)
        monster_target.write_bytes((PROJECT_ROOT / MONSTER_CONFIG_PATH).read_bytes())
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), make_handler(self.root))
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}"

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.directory.cleanup()

    def request(self, path, data=None, content_type="application/json"):
        payload = None if data is None else json.dumps(data).encode("utf-8")
        request = Request(self.url + path, data=payload,
                          headers={"Content-Type": content_type},
                          method="PUT" if data is not None else "GET")
        try:
            with urlopen(request) as response:
                return response.status, json.load(response)
        except HTTPError as error:
            return error.code, json.load(error)

    def test_roundtrip_and_stale_revision(self):
        status, loaded = self.request("/api/levels")
        self.assertEqual(status, 200)
        changed = {"levels": [{"name": "测试关", "clearCrystals": 1, "waves": []}]}
        status, saved = self.request("/api/levels", {"revision": loaded["revision"], "data": changed})
        self.assertEqual(status, 200)
        self.assertNotEqual(saved["revision"], loaded["revision"])
        status, conflict = self.request("/api/levels", {"revision": loaded["revision"], "data": changed})
        self.assertEqual(status, 409)
        self.assertIn("重新载入", conflict["error"])
        self.assertEqual(self.request("/api/levels")[1]["data"], changed)

    def test_rejects_bad_documents_and_paths(self):
        loaded = self.request("/api/tree")[1]
        status, _ = self.request("/api/tree", {"revision": loaded["revision"], "data": {"skills": []}})
        self.assertEqual(status, 400)
        status, _ = self.request("/api/tree", {"revision": loaded["revision"], "data": loaded["data"]},
                                 content_type="text/plain")
        self.assertEqual(status, 400)
        status, _ = self.request("/api/../../README.md")
        self.assertEqual(status, 404)
        self.assertEqual(self.request("/api/tree")[1], loaded)

    def test_invalid_source_json_has_a_readable_error(self):
        (self.root / CONFIG_PATHS["levels"]).write_text("{broken", encoding="utf-8")
        status, response = self.request("/api/levels")
        self.assertEqual(status, 422)
        self.assertIn("JSON 格式错误", response["error"])

    def test_preview_model_script_is_served(self):
        with urlopen(self.url + "/preview-model.js") as response:
            self.assertEqual(response.status, 200)
            self.assertIn("javascript", response.headers["Content-Type"])
            self.assertIn(b"buildWave", response.read())

    def test_monster_speeds_come_from_shared_config(self):
        status, response = self.request("/api/monster-speeds")
        self.assertEqual(status, 200)
        self.assertEqual(len(response["types"]), 13)
        self.assertEqual(response["types"]["normal"], 70)
        self.assertEqual(response["types"]["fast"], 126)
        self.assertEqual(response["types"]["shield"], 52.5)
        monster_target = self.root / MONSTER_CONFIG_PATH
        config = json.loads(monster_target.read_text(encoding="utf-8"))
        config["monsters"]["fast"]["speedMultiplier"] = 2.0
        monster_target.write_text(json.dumps(config), encoding="utf-8")
        self.assertEqual(self.request("/api/monster-speeds")[1]["types"]["fast"], 140)


if __name__ == "__main__":
    unittest.main()
