#!/usr/bin/env python3
"""One-time Cursor browser login for a separate device session; standard library only."""
import argparse
import base64
import hashlib
import http.client
import json
import math
import os
from pathlib import Path
import secrets
import re
import socket
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import webbrowser

# ponytail: private first-party protocol; recheck the installed Cursor loginLink/poll source when it changes.
LOGIN_URL = "https://cursor.com/loginDeepControl"
POLL_URL = "https://api2.cursor.sh/auth/poll"
CLIENT_ID = "KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB"
BODY_MAX = 16384
OUTPUT = Path(__file__).resolve().parent.parent / ".local" / "cursor-account.json"


class AuthorizationError(Exception):
    """Only fixed, non-secret messages may reach the terminal."""


def credential(value, maximum=4095):
    return (isinstance(value, str) and 0 < len(value.encode("utf-8")) <= maximum
            and all(0x20 < ord(c) < 0x7f for c in value))


def challenge(verifier):
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    return base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")


def login_url(flow_id, verifier):
    return LOGIN_URL + "?" + urllib.parse.urlencode({
        "challenge": challenge(verifier), "uuid": flow_id, "mode": "login",
        "supportsSelectedTeamLogin": "true",
    })


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise AuthorizationError("Duplicate fields in the login response.")
        value[key] = item
    return value


def team_id(value):
    if value is None or value == "":
        return None
    if type(value) in (int, float):
        if value < 0 or value > 2147483647 or not math.isfinite(value) or int(value) != value:
            raise AuthorizationError("Cursor returned an invalid selected team identifier.")
        return str(int(value)) if value else None
    if isinstance(value, str) and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:-]{0,255}", value):
        if value.isascii() and value.isdecimal():
            return team_id(int(value))
        return value
    raise AuthorizationError("Cursor returned an invalid selected team identifier.")


def account_record(value, now):
    if not isinstance(value, dict) or "error" in value:
        raise AuthorizationError("Cursor returned an invalid login response.")
    access, refresh = value.get("accessToken"), value.get("refreshToken")
    if access is None and refresh is None:
        return None  # The client also keeps polling before tokens arrive.
    if not credential(access) or not credential(refresh):
        raise AuthorizationError("Cursor did not return device-compatible session credentials.")
    selected_team = team_id(value.get("selectedTeamId"))
    parts = access.split(".")
    if len(parts) != 3 or any(not part for part in parts):
        raise AuthorizationError("Cursor returned an invalid access token.")
    payload = base64.b64decode(parts[1] + "=" * (-len(parts[1]) % 4), altchars=b"-_", validate=True)
    claims = json.loads(payload, object_pairs_hook=unique_object)
    if not isinstance(claims, dict):
        raise AuthorizationError("Cursor returned invalid token metadata.")
    expires = claims.get("exp")
    if type(expires) is not int or not now < expires <= 4102444800:
        raise AuthorizationError("Cursor returned an invalid access-token expiry.")
    # JWT metadata is decoded only from the authenticated polling endpoint's response.
    account_id = claims.get("sub")
    if not credential(account_id, 256):
        raise AuthorizationError("Cursor did not return an account identifier.")
    record = {"provider": "cursor", "access_token": access, "refresh_token": refresh,
              "client_id": CLIENT_ID, "account_id": account_id, "expires_at": expires}
    if selected_team is not None:
        record["team_id"] = selected_team
    return record


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *_args, **_kwargs):
        raise AuthorizationError("Unexpected redirect from Cursor's polling endpoint. Restart authorization.")


def receive_account(flow_id, verifier):
    print("Opening Cursor sign-in in the system browser; finish within 5 minutes.")
    print("This is a full Cursor session, not a quota-only grant. Review browser sign-in.")
    print("If the browser offers Open Cursor, decline it; this tool receives the session by polling.")
    if not webbrowser.open(login_url(flow_id, verifier), new=1):
        raise AuthorizationError("Could not open the system browser. Set a default browser and retry.")
    opener = urllib.request.build_opener(NoRedirect())
    url = POLL_URL + "?" + urllib.parse.urlencode({"uuid": flow_id, "verifier": verifier})
    deadline = time.monotonic() + 300
    while time.monotonic() < deadline:
        request = urllib.request.Request(url, headers={
            "Accept": "application/json", "x-cursor-client-type": "ide",
            "x-ghost-mode": "true", "x-new-onboarding-completed": "false",
        }, method="GET")
        try:
            with opener.open(request, timeout=min(20, max(0.1, deadline - time.monotonic()))) as response:
                if response.status != 200:
                    raise AuthorizationError("Cursor returned an unexpected polling status. Restart authorization.")
                raw = response.read(BODY_MAX + 1)
        except urllib.error.HTTPError as error:
            status = error.code
            error.close()
            if status != 404:
                if status == 403:
                    raise AuthorizationError("Cursor refused this sign-in. Check account or organization restrictions.") from None
                if status == 429:
                    raise AuthorizationError("Cursor rate-limited sign-in polling. Wait before trying again.") from None
                raise AuthorizationError("Cursor login polling failed. Restart authorization.") from None
        else:
            if len(raw) > BODY_MAX:
                raise AuthorizationError("Cursor login response exceeds the size limit.")
            value = json.loads(raw, object_pairs_hook=unique_object)
            record = account_record(value, int(time.time()))
            if record is not None:
                return record
        time.sleep(0.5)  # Matches the installed client's polling interval.
    raise AuthorizationError("Cursor authorization timed out. Restart authorization.")


def save_record(record):
    encoded = (json.dumps(record, ensure_ascii=True, separators=(",", ":")) + "\n").encode("ascii")
    if len(encoded) > 12288:
        raise AuthorizationError("Account record exceeds the device import limit.")
    directory = OUTPUT.parent
    if directory.is_symlink():
        raise AuthorizationError("Refusing a symlink for the private credentials directory.")
    directory.mkdir(mode=0o700, parents=False, exist_ok=True)
    os.chmod(directory, 0o700)
    descriptor, temporary = tempfile.mkstemp(prefix=".cursor-account-", dir=directory)
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
    parameters = urllib.parse.parse_qs(urllib.parse.urlsplit(login_url("fixture", verifier)).query)
    assert parameters["uuid"] == ["fixture"] and parameters["mode"] == ["login"]
    assert "verifier" not in parameters and "scope" not in parameters
    assert account_record({}, 1000) is None
    payload = base64.urlsafe_b64encode(json.dumps({"exp": 2000, "sub": "fixture"}).encode()).rstrip(b"=").decode()
    value = {"accessToken": "fixture." + payload + ".fixture", "refreshToken": "fixture", "authId": "fixture"}
    assert account_record(value, 1000)["expires_at"] == 2000
    assert account_record(dict(value, selectedTeamId=1), 1000)["team_id"] == "1"
    for empty in (None, "", 0, "0"):
        assert "team_id" not in account_record(dict(value, selectedTeamId=empty), 1000)
    assert team_id("0012") == "12" and team_id("team-abc") == "team-abc"
    for bad in (dict(value, selectedTeamId=-1), dict(value, selectedTeamId=float("inf")),
                dict(value, selectedTeamId=0.5), dict(value, selectedTeamId=True),
                dict(value, selectedTeamId="bad\nheader"), dict(value, accessToken="invalid"),
                dict(value, refreshToken="bad\nheader")):
        try:
            account_record(bad, 1000)
        except AuthorizationError:
            pass
        else:
            raise AssertionError("Invalid login response was accepted")


def main():
    argparse.ArgumentParser(description=__doc__).parse_args()
    try:
        verifier = secrets.token_urlsafe(32)
        save_record(receive_account(str(uuid.uuid4()), verifier))
    except AuthorizationError as error:
        print(str(error), file=sys.stderr)
        return 1
    except (OSError, ValueError, TypeError, UnicodeError, urllib.error.URLError,
            socket.timeout, http.client.HTTPException, RecursionError, webbrowser.Error):
        print("Cursor authorization failed. Check network and local file permissions, then restart authorization.", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Authorization cancelled.", file=sys.stderr)
        return 130
    print("Saved .local/cursor-account.json with mode 600. Import it over USB, then close the tool.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
