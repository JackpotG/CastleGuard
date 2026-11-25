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
            if kind == "monsters":
                target.write_bytes((PROJECT_ROOT / MONSTER_CONFIG_PATH).read_bytes())
            else:
                target.write_text(json.dumps({"levels": []} if kind == "levels" else
                                             {"upgrades": [], "skills": []}), encoding="utf-8")
        schema = Path("code/axmol/CastleGuard/Content/config/upgrade-attributes.json")
        (self.root / schema).write_bytes((PROJECT_ROOT / schema).read_bytes())
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
        with urlopen(self.url + "/monster-visual.js") as response:
            self.assertEqual(response.status, 200)
            self.assertIn("javascript", response.headers["Content-Type"])
            self.assertIn(b"MonsterVisual", response.read())
        with urlopen(self.url + "/tree-position.js") as response:
            self.assertEqual(response.status, 200)
            self.assertIn("javascript", response.headers["Content-Type"])
            self.assertIn(b"TreePosition", response.read())

    def test_shared_connection_router_is_served(self):
        for asset, marker in [("tree-routing.js", b"TreeRouting"),
                              ("tree-router-runtime.js", b"createTreeConnectionRouter")]:
            with urlopen(self.url + "/" + asset) as response:
                self.assertEqual(response.status, 200)
                self.assertIn("javascript", response.headers["Content-Type"])
                self.assertIn(marker, response.read())

    def test_planner_entry_and_home_asset_are_served(self):
        for asset, marker in [("/planner", "游戏策划配置".encode()),
                              ("/planner-home.js", b"PlannerHome")]:
            with urlopen(self.url + asset) as response:
                self.assertEqual(response.status, 200)
                self.assertIn(marker, response.read())

    def test_upgrade_attribute_schema_is_shared_and_read_only(self):
        status, response = self.request("/api/attributes")
        self.assertEqual(status, 200)
        self.assertEqual(response["data"]["attributes"][0]["id"], "arrow_damage")
        self.assertEqual(self.request("/api/attributes", {"data": {}})[0], 400)

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

    def test_monster_config_roundtrip_and_conflict(self):
        loaded = self.request("/api/monsters")[1]
        changed = loaded["data"]
        changed["baseSpeed"] = 80
        changed["monsters"]["fast"]["speedMultiplier"] = 2.0
        status, saved = self.request("/api/monsters", {"revision": loaded["revision"], "data": changed})
        self.assertEqual(status, 200)
        self.assertNotEqual(saved["revision"], loaded["revision"])
        self.assertEqual(self.request("/api/monster-speeds")[1]["types"]["fast"], 160)
        self.assertEqual(self.request("/api/monsters")[1]["data"], changed)
        self.assertEqual(self.request("/api/monsters", {"revision": loaded["revision"], "data": changed})[0], 409)

    def test_monster_config_rejects_values_game_cannot_load(self):
        loaded = self.request("/api/monsters")[1]
        cases = [
            ("baseSpeed", 0),
            ("baseSpeed", 10 ** 1000),
            ("baseBodyRadius", 49),
            ("maxHp", 0),
            ("speedMultiplier", 10.1),
            ("actionPower", 1.5),
            ("coinDrop", 101),
            ("stopRatioMax", -1),
        ]
        for field, value in cases:
            with self.subTest(field=field):
                data = json.loads(json.dumps(loaded["data"]))
                if field in ("baseSpeed", "baseBodyRadius"):
                    data[field] = value
                else:
                    data["monsters"]["normal"][field] = value
                self.assertEqual(self.request("/api/monsters", {"revision": loaded["revision"], "data": data})[0], 400)
        data = json.loads(json.dumps(loaded["data"]))
        data["monsters"]["normal"]["stopRatioMin"] = 0.5
        self.assertEqual(self.request("/api/monsters", {"revision": loaded["revision"], "data": data})[0], 400)
        data = json.loads(json.dumps(loaded["data"]))
        del data["monsters"]["fast"]
        self.assertEqual(self.request("/api/monsters", {"revision": loaded["revision"], "data": data})[0], 400)
        self.assertEqual(self.request("/api/monsters")[1], loaded)


if __name__ == "__main__":
    unittest.main()
