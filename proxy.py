#!/usr/bin/env python3
"""Proxy HTTP local : Pico (HTTP plain) → PRIM (HTTPS TLS 1.3)

Usage:
    export PRIM_API_KEY=your_key
    python3 proxy.py
"""

import os
import sys
import urllib.request
import urllib.error
from http.server import BaseHTTPRequestHandler, HTTPServer

PRIM_BASE = "https://prim.iledefrance-mobilites.fr"
API_KEY   = os.environ.get("PRIM_API_KEY", "")
PORT      = 8888

FORWARD_HEADERS = {"date", "content-type"}


class ProxyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        url = PRIM_BASE + self.path
        req = urllib.request.Request(url, headers={
            "apikey": API_KEY,
            "Accept": "application/json",
            "User-Agent": "transilien-proxy/1.0",
        })
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                body = resp.read()
                self.send_response(resp.status)
                for key, val in resp.headers.items():
                    if key.lower() in FORWARD_HEADERS:
                        self.send_header(key, val)
                # Assure Content-Length correct après lecture complète
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
        except urllib.error.HTTPError as e:
            self.send_error(e.code, str(e))
        except Exception as e:
            self.send_error(502, str(e))

    def log_message(self, fmt, *args):
        print(f"[proxy] {self.address_string()} - {fmt % args}", flush=True)


if __name__ == "__main__":
    if not API_KEY:
        print("ERREUR : PRIM_API_KEY non défini", file=sys.stderr)
        sys.exit(1)
    server = HTTPServer(("0.0.0.0", PORT), ProxyHandler)
    print(f"Proxy PRIM écoute sur :{PORT}", flush=True)
    server.serve_forever()
