#!/usr/bin/env python3
"""One-time Google Desktop OAuth; no daemon and no third-party dependencies."""
import argparse
import base64
import hashlib
import http.client
import http.server
import json
import os
from pathlib import Path
import secrets
import socket
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

SCOPE = "https://www.googleapis.com/auth/calendar.events.readonly"
BODY_MAX = 16384
TOKEN_MAX = 4096
OUTPUT = Path(__file__).resolve().parent.parent / ".local" / "google-account.json"


class AuthorizationError(Exception):
    """Only fixed, non-secret messages may reach the terminal."""


def credential(value):
    return (isinstance(value, str) and 0 < len(value.encode("utf-8")) <= TOKEN_MAX
            and all(0x20 < ord(c) < 0x7f for c in value))


def read_client(path):
    with path.open("rb") as source:
        data = source.read(BODY_MAX + 1)
    if len(data) > BODY_MAX:
        raise AuthorizationError("Client JSON exceeds the size limit.")
    root = json.loads(data)
    client = root.get("installed") if isinstance(root, dict) else None
    if (not isinstance(client, dict) or not credential(client.get("client_id"))
            or not credential(client.get("client_secret"))):
        raise AuthorizationError("Download a Desktop app OAuth client JSON from Google Cloud.")
    return client["client_id"], client["client_secret"]


def receive_code(client_id, state, challenge):
    result = {}

    class Callback(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass  # The default request log contains the authorization code.

        def reply(self, status, message):
            body = message.encode("ascii")
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'none'")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if len(self.path) > 8192 or self.headers.get("Host") != f"127.0.0.1:{self.server.server_port}":
                self.reply(400, "Invalid callback.")
                return
            try:
                parts = urllib.parse.urlsplit(self.path)
                query = urllib.parse.parse_qs(parts.query, keep_blank_values=True,
                                             strict_parsing=True, max_num_fields=8)
                states = query.get("state", [])
                matches = (len(states) == 1 and secrets.compare_digest(
                    states[0].encode("utf-8"), state.encode("ascii")))
                if parts.path != "/oauth2callback" or parts.scheme or parts.netloc or not matches:
                    self.reply(400, "Invalid callback state. Return to the Google consent page.")
                    return
                codes = query.get("code", [])
                if "error" in query:
                    result["denied"] = True
                    self.reply(400, "Authorization was not granted. Return to the terminal.")
                elif len(codes) == 1 and credential(codes[0]):
                    result["code"] = codes[0]
                    self.reply(200, "Authorization received. Close this tab and return to the terminal.")
                else:
                    self.reply(400, "Missing or invalid authorization code.")
            except (ValueError, UnicodeError):
                self.reply(400, "Invalid callback.")

    class LoopbackServer(http.server.HTTPServer):
        def get_request(self):
            connection, address = super().get_request()
            connection.settimeout(5)
            return connection, address

        def handle_error(self, *_args):
            pass  # Never emit request paths or exception bodies.

    with LoopbackServer(("127.0.0.1", 0), Callback) as server:
        server.timeout = 1
        redirect_uri = f"http://127.0.0.1:{server.server_port}/oauth2callback"
        parameters = {
            "client_id": client_id, "redirect_uri": redirect_uri, "response_type": "code",
            "scope": SCOPE, "state": state, "code_challenge": challenge,
            "code_challenge_method": "S256", "access_type": "offline", "prompt": "consent",
        }
        url = "https://accounts.google.com/o/oauth2/v2/auth?" + urllib.parse.urlencode(parameters)
        print("Opening Google consent in the system browser; complete authorization within 5 minutes.")
        if not webbrowser.open(url, new=1):
            raise AuthorizationError("Could not open the system browser. Set a default browser and retry.")
        deadline = time.monotonic() + 300
        while not result and time.monotonic() < deadline:
            server.handle_request()
        if result.get("denied"):
            raise AuthorizationError("Google authorization was not granted.")
        if "code" not in result:
            raise AuthorizationError("Authorization timed out. Run the command again.")
        return result["code"], redirect_uri


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        raise AuthorizationError("Unexpected redirect from the Google token endpoint.")


def exchange(client_id, client_secret, code, redirect_uri, verifier):
    body = urllib.parse.urlencode({
        "client_id": client_id, "client_secret": client_secret, "code": code,
        "redirect_uri": redirect_uri, "code_verifier": verifier,
        "grant_type": "authorization_code",
    }).encode("ascii")
    request = urllib.request.Request("https://oauth2.googleapis.com/token", data=body,
                                     headers={"Content-Type": "application/x-www-form-urlencoded",
                                              "Accept": "application/json"}, method="POST")
    try:
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=20) as response:
            if response.status != 200:
                raise AuthorizationError("Google rejected the token exchange. Retry authorization.")
            raw = response.read(BODY_MAX + 1)
    except urllib.error.HTTPError as error:
        error.close()
        raise AuthorizationError("Google rejected the token exchange. Check OAuth client setup and retry.") from None
    if len(raw) > BODY_MAX:
        raise AuthorizationError("Google token response exceeds the size limit.")
    token = json.loads(raw)
    if not isinstance(token, dict):
        raise AuthorizationError("Google returned an invalid token response.")
    seconds = token.get("expires_in")
    if (not credential(token.get("access_token")) or not credential(token.get("refresh_token"))
            or not isinstance(token.get("token_type"), str) or token["token_type"].lower() != "bearer"
            or type(seconds) is not int or not 0 < seconds <= 604800
            or not isinstance(token.get("scope"), str) or SCOPE not in token["scope"].split()):
        raise AuthorizationError("Google did not grant offline Calendar access. Revoke the app grant and retry consent.")
    return {"provider": "google", "access_token": token["access_token"],
            "refresh_token": token["refresh_token"], "client_id": client_id,
            "client_secret": client_secret, "expires_at": int(time.time()) + seconds}


def save_record(record):
    encoded = (json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")
    if len(encoded) > 12288:
        raise AuthorizationError("Account record exceeds the device import limit.")
    directory = OUTPUT.parent
    if directory.is_symlink():
        raise AuthorizationError("Refusing a symlink for the private credentials directory.")
    directory.mkdir(mode=0o700, parents=False, exist_ok=True)
    os.chmod(directory, 0o700)
    descriptor, temporary = tempfile.mkstemp(prefix=".google-account-", dir=directory)
    try:
        os.fchmod(descriptor, 0o600)
        with os.fdopen(descriptor, "wb") as destination:
            destination.write(encoded)
            destination.flush()
            os.fsync(destination.fileno())
        os.replace(temporary, OUTPUT)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True, help="Downloaded Google Desktop client JSON")
    arguments = parser.parse_args()
    try:
        client_id, client_secret = read_client(arguments.client)
        verifier = secrets.token_urlsafe(48)
        challenge = base64.urlsafe_b64encode(hashlib.sha256(verifier.encode("ascii")).digest()).rstrip(b"=").decode("ascii")
        code, redirect_uri = receive_code(client_id, secrets.token_urlsafe(32), challenge)
        save_record(exchange(client_id, client_secret, code, redirect_uri, verifier))
    except AuthorizationError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, ValueError, UnicodeError, urllib.error.URLError, socket.timeout,
            http.client.HTTPException, RecursionError, webbrowser.Error):
        print("Authorization failed: check the client JSON, network and local file permissions; retry.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Authorization cancelled.", file=sys.stderr)
        return 130
    print("Saved .local/google-account.json with mode 600; import it over USB, then close this tool.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
