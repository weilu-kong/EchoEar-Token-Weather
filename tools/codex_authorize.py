#!/usr/bin/env python3
"""One-time Codex browser OAuth for a separate device session; standard library only."""
import argparse
import base64
import errno
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

# ponytail: the first-party login contract can change; recheck the cited official login sources before updating this tool.
CLIENT_ID = "app_EMoamEEZ73f0CkXaXp7hrann"
SCOPE = "openid profile email offline_access api.connectors.read api.connectors.invoke"
AUTHORIZE_URL = "https://auth.openai.com/oauth/authorize"
TOKEN_URL = "https://auth.openai.com/oauth/token"
ORIGINATOR = "codex_cli_rs"
STATE_SUFFIX = ".onboarding_entrypoint=life_sciences"
BODY_MAX = 16384
OUTPUT = Path(__file__).resolve().parent.parent / ".local" / "codex-account.json"


class AuthorizationError(Exception):
    """Only fixed, non-secret messages may reach the terminal."""


def credential(value, maximum=4095):
    return (isinstance(value, str) and 0 < len(value.encode("utf-8")) <= maximum
            and all(0x20 < ord(c) < 0x7f for c in value))


def challenge(verifier):
    return base64.urlsafe_b64encode(hashlib.sha256(verifier.encode("ascii")).digest()).rstrip(b"=").decode("ascii")


def authorization_url(redirect_uri, state, proof):
    parameters = {
        "response_type": "code", "client_id": CLIENT_ID, "redirect_uri": redirect_uri,
        "code_challenge": proof, "code_challenge_method": "S256", "state": state,
        "scope": SCOPE, "id_token_add_organizations": "true",
        "codex_cli_simplified_flow": "true", "originator": ORIGINATOR,
    }
    return AUTHORIZE_URL + "?" + urllib.parse.urlencode(parameters)


def callback_result(path, state):
    if len(path) > 8192:
        raise AuthorizationError("Invalid callback.")
    parts = urllib.parse.urlsplit(path)
    if parts.path != "/auth/callback" or parts.scheme or parts.netloc or parts.fragment:
        raise AuthorizationError("Invalid callback.")
    query = urllib.parse.parse_qs(parts.query, keep_blank_values=True, strict_parsing=True, max_num_fields=16)
    if any(len(values) != 1 for values in query.values()):
        raise AuthorizationError("Duplicate callback fields.")
    received = query.get("state", [""])[0]
    if received.endswith(STATE_SUFFIX):
        received = received[:-len(STATE_SUFFIX)]
    if not secrets.compare_digest(received.encode("utf-8"), state.encode("ascii")):
        raise AuthorizationError("Invalid callback state.")
    if "error" in query:
        return {"denied": True}
    code = query.get("code", [None])[0]
    if not credential(code):
        raise AuthorizationError("Missing or invalid authorization code.")
    return {"code": code}


def receive_code(state, proof):
    result = {}

    class Callback(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_args):
            pass  # Request paths contain the one-time authorization code.

        def reply(self, status, message):
            body = message.encode("ascii")
            self.send_response(status)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Referrer-Policy", "no-referrer")
            self.send_header("Content-Security-Policy", "default-src 'none'")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.headers.get_all("Host", []) != [f"127.0.0.1:{self.server.server_port}"]:
                self.reply(400, "Invalid callback host.")
                return
            try:
                value = callback_result(self.path, state)
            except (AuthorizationError, ValueError, UnicodeError):
                self.reply(400, "Invalid callback. Return to the Codex sign-in page.")
                return
            result.update(value)
            if value.get("denied"):
                self.reply(400, "Authorization was not granted. Return to the terminal.")
            else:
                self.reply(200, "Authorization received. Close this tab and return to the terminal.")

    class LoopbackServer(http.server.HTTPServer):
        def get_request(self):
            connection, address = super().get_request()
            connection.settimeout(5)
            return connection, address

        def handle_error(self, *_args):
            pass  # Never print a callback URL or exception payload.

    server = None
    for port in (1455, 1457):
        try:
            server = LoopbackServer(("127.0.0.1", port), Callback)
            break
        except OSError as error:
            if error.errno != errno.EADDRINUSE:
                raise
    if server is None:
        raise AuthorizationError("Codex callback ports 1455 and 1457 are occupied. Close the other login flow and retry.")
    with server:
        server.timeout = 1
        redirect_uri = f"http://127.0.0.1:{server.server_port}/auth/callback"
        print("Opening Codex sign-in in the system browser; complete authorization within 5 minutes.")
        print("The official requested scope includes connector read and invoke permissions. Review browser consent.")
        if not webbrowser.open(authorization_url(redirect_uri, state, proof), new=1):
            raise AuthorizationError("Could not open the system browser. Set a default browser and retry.")
        deadline = time.monotonic() + 300
        while not result and time.monotonic() < deadline:
            server.handle_request()
        if result.get("denied"):
            raise AuthorizationError("Codex authorization was not granted.")
        if "code" not in result:
            raise AuthorizationError("Authorization timed out. Run the command again.")
        return result["code"], redirect_uri


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise AuthorizationError("Duplicate fields in the token response.")
        value[key] = item
    return value


def jwt_claims(token):
    if not credential(token, BODY_MAX):
        raise AuthorizationError("Codex returned an invalid token.")
    parts = token.split(".")
    if len(parts) != 3 or any(not part for part in parts):
        raise AuthorizationError("Codex returned an invalid token.")
    payload = base64.b64decode(parts[1] + "=" * (-len(parts[1]) % 4), altchars=b"-_", validate=True)
    claims = json.loads(payload, object_pairs_hook=unique_object)
    if not isinstance(claims, dict):
        raise AuthorizationError("Codex returned invalid token metadata.")
    return claims


def account_record(token, now):
    if (not isinstance(token, dict) or "error" in token
            or not credential(token.get("access_token")) or not credential(token.get("refresh_token"))):
        raise AuthorizationError("Codex did not return device-compatible offline credentials.")
    # Metadata is decoded only from the authenticated token endpoint's response.
    identity = jwt_claims(token.get("id_token"))
    auth = identity.get("https://api.openai.com/auth")
    if not isinstance(auth, dict) or not credential(auth.get("chatgpt_account_id"), 256):
        raise AuthorizationError("Codex did not return a selected ChatGPT account.")
    if auth.get("chatgpt_account_is_fedramp") is True:
        raise AuthorizationError("The device's current quota adapter does not support this account's routing.")
    expires = jwt_claims(token["access_token"]).get("exp")
    if type(expires) is not int or not now < expires <= 4102444800:
        raise AuthorizationError("Codex returned an invalid access-token expiry.")
    token_type = token.get("token_type", "bearer")
    if not isinstance(token_type, str) or token_type.lower() != "bearer":
        raise AuthorizationError("Codex returned an unsupported token type.")
    return {"provider": "codex", "access_token": token["access_token"],
            "refresh_token": token["refresh_token"], "client_id": CLIENT_ID,
            "account_id": auth["chatgpt_account_id"], "expires_at": expires, "scope": SCOPE}


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        raise AuthorizationError("Unexpected redirect from the Codex token endpoint; restart authorization.")


def exchange(code, redirect_uri, verifier):
    body = urllib.parse.urlencode({
        "grant_type": "authorization_code", "client_id": CLIENT_ID, "code": code,
        "redirect_uri": redirect_uri, "code_verifier": verifier,
    }).encode("ascii")
    request = urllib.request.Request(TOKEN_URL, data=body,
                                     headers={"Content-Type": "application/x-www-form-urlencoded",
                                              "Accept": "application/json"}, method="POST")
    try:
        # An expired response or redirect may have consumed the code: never retry this POST.
        with urllib.request.build_opener(NoRedirect()).open(request, timeout=20) as response:
            if response.status != 200:
                raise AuthorizationError("Codex rejected the token exchange. Restart authorization.")
            raw = response.read(BODY_MAX + 1)
    except urllib.error.HTTPError as error:
        error.close()
        raise AuthorizationError("Codex rejected the token exchange. Restart authorization.") from None
    if len(raw) > BODY_MAX:
        raise AuthorizationError("Codex token response exceeds the device import limit.")
    return account_record(json.loads(raw, object_pairs_hook=unique_object), int(time.time()))


def save_record(record):
    encoded = (json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")
    if len(encoded) > 12288:
        raise AuthorizationError("Account record exceeds the device import limit.")
    directory = OUTPUT.parent
    if directory.is_symlink():
        raise AuthorizationError("Refusing a symlink for the private credentials directory.")
    directory.mkdir(mode=0o700, parents=False, exist_ok=True)
    os.chmod(directory, 0o700)
    descriptor, temporary = tempfile.mkstemp(prefix=".codex-account-", dir=directory)
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


def fixture_checks():
    """Synthetic checks only; never starts login or accesses credential stores."""
    verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"
    assert challenge(verifier) == "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"
    assert callback_result("/auth/callback?state=fixture&code=sample", "fixture") == {"code": "sample"}
    assert callback_result("/auth/callback?state=fixture&error=access_denied", "fixture") == {"denied": True}
    for path in ("/auth/callback?state=wrong&code=sample", "/auth/callback?state=fixture&state=fixture&code=sample"):
        try:
            callback_result(path, "fixture")
        except AuthorizationError:
            pass
        else:
            raise AssertionError("Invalid callback was accepted")
    def token(payload):
        encoded = base64.urlsafe_b64encode(json.dumps(payload).encode()).rstrip(b"=").decode()
        return "fixture." + encoded + ".fixture"
    record = account_record({"access_token": token({"exp": 2000}), "refresh_token": "fixture",
                             "id_token": token({"https://api.openai.com/auth": {"chatgpt_account_id": "fixture"}})}, 1000)
    assert record["provider"] == "codex" and record["expires_at"] == 2000 and record["account_id"] == "fixture"


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    try:
        verifier = secrets.token_urlsafe(64)
        code, redirect_uri = receive_code(secrets.token_urlsafe(32), challenge(verifier))
        save_record(exchange(code, redirect_uri, verifier))
    except AuthorizationError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, ValueError, TypeError, UnicodeError, urllib.error.URLError,
            socket.timeout, http.client.HTTPException, RecursionError, webbrowser.Error):
        print("Codex authorization failed. Check network and local file permissions, then restart authorization.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Authorization cancelled.", file=sys.stderr)
        return 130
    print("Saved .local/codex-account.json with mode 600. Import this device session over USB, then close the tool.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
