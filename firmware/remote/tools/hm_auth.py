"""
The update token and signed requests for the remote's update server
(developer builds, port 8080): shared by hm_ota.py and devtools.py.

The token (OTA_TOKEN in secrets.env, shown in Settings > About) never goes
over the network. Each request fetches a one-time nonce from GET /nonce and
carries an HMAC of itself instead (main/ota.h, ota_authorized()):

    X-OTA-Nonce: <nonce>
    X-OTA-Auth:  hex HMAC-SHA256(token, nonce "\\n" method "\\n" path "\\n" body)
    X-OTA-Body:  hex SHA-256 of the body (only for a request with a body)

where path includes the query and body is the X-OTA-Body value, or "".
Nothing here prints the token.
"""

import hashlib
import hmac
import sys
import urllib.request
from pathlib import Path

PROJECT = Path(__file__).resolve().parent.parent


def read_token():
    env = PROJECT / "secrets.env"
    if not env.exists():
        sys.exit("secrets.env not found; OTA_TOKEN is needed")
    for line in env.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key.strip() == "OTA_TOKEN":
            value = value.strip()
            if len(value) >= 2 and value[0] == value[-1] == '"':
                value = value[1:-1]
            if value:
                return value
    sys.exit("OTA_TOKEN missing from secrets.env")


def signed_headers(base, method, path, token, body=None):
    """Headers that authorize one request to base (http://host:port)."""
    with urllib.request.urlopen(f"{base}/nonce", timeout=10) as r:
        nonce = r.read().decode("ascii").strip()
    body_hash = hashlib.sha256(body).hexdigest() if body else ""
    message = f"{nonce}\n{method}\n{path}\n{body_hash}".encode()
    headers = {
        "X-OTA-Nonce": nonce,
        "X-OTA-Auth": hmac.new(token.encode(), message, hashlib.sha256).hexdigest(),
    }
    if body:
        headers["X-OTA-Body"] = body_hash
    return headers
