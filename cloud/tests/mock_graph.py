import json
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 18999
DELAY_MS = int(sys.argv[2]) if len(sys.argv) > 2 else 0
messages = {
    "value": [
        {
            "id": "m1",
            "subject": "integration message",
            "receivedDateTime": "2026-10-30T00:00:00Z",
            "from": {"emailAddress": {"address": "sender@example.test"}},
            "toRecipients": [{"emailAddress": {"address": "analyst@example.test"}}],
            "bodyPreview": "fixture",
            "hasAttachments": False,
        }
    ]
}


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if DELAY_MS > 0:
            time.sleep(DELAY_MS / 1000.0)
        path = self.path.split("?", 1)[0]
        if path.endswith("/messages"):
            payload = messages
        elif path.endswith("/drive/root/children"):
            payload = {"value": []}
        elif "/users/" in path:
            payload = {"id": "integration-user", "userPrincipalName": "analyst@example.test"}
        else:
            payload = {"error": "not found"}
        data = json.dumps(payload).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def log_message(self, format, *args):
        pass


ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
