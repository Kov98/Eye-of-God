#!/usr/bin/env python3
"""Synthetic Microsoft Graph mock for Phase 0 tests.

No real tenant, no real token, no real user data. Run:

    python cloud/tests/mock_graph_server.py --port 8765

Then call with:
    Authorization: Bearer test-token
"""
from __future__ import annotations

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

FIXTURES = Path(__file__).resolve().parent / "fixtures"


class GraphHandler(BaseHTTPRequestHandler):
    server_version = "OlhoDeDeusMockGraph/0.1"

    def log_message(self, fmt: str, *args) -> None:
        # Deliberately do not log headers (they contain the test bearer token).
        print(f"mock-graph: {self.address_string()} {fmt % args}")

    def _send_json(self, status: int, payload: object) -> None:
        body = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _fixture(self, name: str) -> object:
        with (FIXTURES / name).open("r", encoding="utf-8") as handle:
            return json.load(handle)

    def do_POST(self) -> None:  # noqa: N802
        path = urlparse(self.path).path.rstrip("/")
        if path != "/oauth/token":
            self._send_json(404, {
                "error": {"code": "NotFound", "message": "No synthetic route."}
            })
            return
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", errors="replace")
        form = parse_qs(body)
        code = (form.get("code") or [""])[0]
        verifier = (form.get("code_verifier") or [""])[0]
        if code != "demo-code" or not verifier:
            self._send_json(400, {
                "error": "invalid_grant",
                "error_description": "use code=demo-code and a non-empty code_verifier",
            })
            return
        self._send_json(200, {
            "access_token": "test-token",
            "refresh_token": "mock-refresh-token",
            "token_type": "Bearer",
            "expires_in": 3600,
        })
    def do_GET(self) -> None:  # noqa: N802
        if self.headers.get("Authorization") != "Bearer test-token":
            self._send_json(401, {
                "error": {
                    "code": "InvalidAuthenticationToken",
                    "message": "Use Authorization: Bearer test-token in the mock.",
                }
            })
            return

        path = urlparse(self.path).path.rstrip("/")
        if path == "/v1.0/me":
            self._send_json(200, self._fixture("m365_me.json"))
        elif path == "/v1.0/me/messages":
            self._send_json(200, self._fixture("m365_messages_page.json"))
        elif path == "/v1.0/me/drive/root/children":
            self._send_json(200, self._fixture("m365_drive_page.json"))
        elif path.startswith("/v1.0/users/") and path.endswith("/messages"):
            self._send_json(200, self._fixture("m365_messages_page.json"))
        elif path.startswith("/v1.0/users/") and path.endswith("/drive/root/children"):
            self._send_json(200, self._fixture("m365_drive_page.json"))
        elif path.startswith("/v1.0/users/"):
            self._send_json(200, self._fixture("m365_me.json"))
        elif path == "/health":
            self._send_json(200, {"status": "ok", "synthetic": True})
        else:
            self._send_json(404, {
                "error": {"code": "NotFound", "message": "No synthetic route."}
            })


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), GraphHandler)
    print(f"mock graph on http://127.0.0.1:{args.port} (synthetic only)")
    server.serve_forever()


if __name__ == "__main__":
    main()
