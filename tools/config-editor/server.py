"""Local editor for campaign and progression, with read-only monster balance data."""

import argparse
import hashlib
import json
import math
import os
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from tempfile import NamedTemporaryFile


EDITOR_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = EDITOR_DIR.parent.parent
CONFIG_PATHS = {
    "levels": Path("code/axmol/CastleGuard/Content/config/waves.json"),
    "tree": Path("code/axmol/CastleGuard/Content/config/upgrades.json"),
}
MONSTER_CONFIG_PATH = Path("code/axmol/CastleGuard/Content/config/monsters.json")
STATIC_FILES = {
    "/": ("index.html", "text/html; charset=utf-8"),
    "/app.js": ("app.js", "text/javascript; charset=utf-8"),
    "/preview-model.js": ("preview-model.js", "text/javascript; charset=utf-8"),
    "/style.css": ("style.css", "text/css; charset=utf-8"),
}
MAX_BODY_BYTES = 4 * 1024 * 1024


def revision(raw):
    return hashlib.sha256(raw).hexdigest()


def monster_speeds(project_root):
    data = json.loads((project_root / MONSTER_CONFIG_PATH).read_text(encoding="utf-8"))
    base_speed = data["baseSpeed"]
    monsters = data["monsters"]
    if (not isinstance(base_speed, (int, float)) or isinstance(base_speed, bool) or
            not math.isfinite(base_speed) or base_speed <= 0 or not isinstance(monsters, dict)):
        raise ValueError("Invalid monster speed data")
    speeds = {}
    for monster_id, attributes in monsters.items():
        multiplier = attributes["speedMultiplier"]
        if (not isinstance(multiplier, (int, float)) or isinstance(multiplier, bool) or
                not math.isfinite(multiplier) or multiplier <= 0):
            raise ValueError("Invalid monster speed multiplier")
        speeds[monster_id] = round(base_speed * multiplier, 4)
    return {"baseSpeed": base_speed, "types": speeds}


def make_handler(project_root=PROJECT_ROOT):
    class EditorHandler(BaseHTTPRequestHandler):
        def send_json(self, status, payload):
            body = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def target(self):
            name = self.path.removeprefix("/api/")
            return project_root / CONFIG_PATHS[name] if name in CONFIG_PATHS else None

        def do_GET(self):
            if self.path == "/api/monster-speeds":
                try:
                    self.send_json(200, monster_speeds(project_root))
                except (ValueError, TypeError, KeyError, OSError) as error:
                    self.log_error("monster speeds unavailable: %s", error)
                    self.send_json(422, {"error": "怪物属性配置缺失或无效"})
                return
            target = self.target()
            if target is not None:
                raw = target.read_bytes()
                try:
                    data = json.loads(raw)
                except json.JSONDecodeError as error:
                    self.send_json(422, {"error": f"配置文件 JSON 格式错误：{error}"})
                    return
                self.send_json(200, {"data": data, "revision": revision(raw)})
                return
            static = STATIC_FILES.get(self.path)
            if static is None:
                if self.path.startswith("/api/"):
                    self.send_json(404, {"error": "Unknown config"})
                else:
                    self.send_error(404)
                return
            filename, mime = static
            body = (EDITOR_DIR / filename).read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_PUT(self):
            target = self.target()
            if target is None or self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                self.send_json(400, {"error": "Invalid config path or content type"})
                return
            origin = self.headers.get("Origin")
            if origin and origin != f"http://{self.headers.get('Host')}":
                self.send_json(403, {"error": "Only this editor can save files"})
                return
            try:
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= MAX_BODY_BYTES:
                    raise ValueError("Invalid request size")
                payload = json.loads(self.rfile.read(size))
                data = payload["data"]
                if not isinstance(data, dict) or not isinstance(payload["revision"], str):
                    raise ValueError("Invalid document")
                if self.path == "/api/levels" and not isinstance(data.get("levels"), list):
                    raise ValueError("Missing levels")
                if self.path == "/api/tree" and (
                    not isinstance(data.get("upgrades"), list) or not isinstance(data.get("skills"), list)
                ):
                    raise ValueError("Missing upgrades or skills")
                body = (json.dumps(data, ensure_ascii=False, indent=2, allow_nan=False) + "\n").encode("utf-8")
            except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
                self.send_json(400, {"error": str(error)})
                return

            current = target.read_bytes()
            if payload["revision"] != revision(current):
                self.send_json(409, {"error": "文件已在其他位置修改，请重新载入"})
                return
            try:
                current_data = json.loads(current)
            except json.JSONDecodeError as error:
                self.send_json(422, {"error": f"配置文件 JSON 格式错误：{error}"})
                return
            if data == current_data:
                self.send_json(200, {"revision": revision(current)})
                return

            temporary = None
            try:
                with NamedTemporaryFile(dir=target.parent, prefix=".config-editor-", delete=False) as output:
                    temporary = Path(output.name)
                    output.write(body)
                    output.flush()
                    os.fsync(output.fileno())
                temporary.chmod(target.stat().st_mode & 0o777)
                os.replace(temporary, target)
            finally:
                if temporary and temporary.exists():
                    temporary.unlink()
            self.send_json(200, {"revision": revision(body)})

    return EditorHandler


def main():
    parser = argparse.ArgumentParser(description="CastleGuard config editor")
    parser.add_argument("--port", type=int, default=8780)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), make_handler())
    print(f"CastleGuard config editor: http://127.0.0.1:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
