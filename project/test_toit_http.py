"""Check the Aether Toit HTTP port against a loopback server.

Pass the compiled scripts/toit/http.ae executable as the only argument.
"""
import http.server
import os
import subprocess
import sys
import threading
from pathlib import Path


BODY = b"0123456789" * 900


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/missing":
            self.send_error(404)
            return
        self.send_response(200)
        if self.path == "/truncated":
            self.send_header("Content-Length", str(len(BODY)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(BODY[:16])
            self.close_connection = True
            return
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        for chunk in (BODY[:4500], BODY[4500:]):
            self.wfile.write(f"{len(chunk):x}\r\n".encode() + chunk + b"\r\n")
        self.wfile.write(b"0\r\n\r\n")

    def log_message(self, *args):
        pass


def main(binary):
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            base = f"http://127.0.0.1:{server.server_port}"

            def run(path):
                return subprocess.run(
                    [binary], env={**os.environ, "TOIT_HTTP_URL": base + path},
                    capture_output=True, text=True, timeout=10,
                )

            success = run("/")
            assert success.returncode == 0 and f"Read {len(BODY)} bytes" in success.stdout, success
            missing = run("/missing")
            assert missing.returncode == 1 and "HTTP status 404" in missing.stdout, missing
            truncated = run("/truncated")
            assert truncated.returncode == 1 and "Read " not in truncated.stdout, truncated
        finally:
            server.shutdown()
            worker.join()
    print("Toit HTTP body count, status failure and truncated response PASS")


if __name__ == "__main__":
    main(Path(sys.argv[1]).resolve())
