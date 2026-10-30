import http.server
import ssl
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 19443
CERT = sys.argv[2]
KEY = sys.argv[3]
PAYLOAD = (b"OlhoDeDeus-HTTPS-stream-" * 65536)[:1024 * 1024]


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path != "/file":
            self.send_error(404)
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(PAYLOAD)))
        self.end_headers()
        self.wfile.write(PAYLOAD)

    def log_message(self, format, *args):
        pass


context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain(CERT, KEY)
server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), Handler)
server.socket = context.wrap_socket(server.socket, server_side=True)
server.serve_forever()
