#!/usr/bin/env python3
"""Export only explicitly selected account credentials; never contact providers."""
import argparse
import base64
import getpass
import hashlib
import json
import math
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unicodedata

CLIENT_IDS = {
    'codex': 'app_EMoamEEZ73f0CkXaXp7hrann',
    'claude': '9d1c250a-e61b-44d9-88ed-5944d1962f5e',
    'cursor': 'KbZUR41cY7W6zRSdpSUJ7I7mLYBKOCmB',
}
MAX_STORE_BYTES = 1024 * 1024


def read_json(path):
    with Path(path).open('rb') as stream:
        raw = stream.read(MAX_STORE_BYTES + 1)
    if len(raw) > MAX_STORE_BYTES:
        raise ValueError('credential store exceeds size limit')
    value = json.loads(raw)
    if not isinstance(value, dict):
        raise ValueError('unsupported credential store format')
    return value


def jwt_claims(token):
    # Unverified claims only supply expiry/account hints, never authenticate an account.
    try:
        payload = token.split('.')[1]
        value = json.loads(base64.urlsafe_b64decode(payload + '=' * (-len(payload) % 4)))
        return value if isinstance(value, dict) else {}
    except (ValueError, IndexError, UnicodeError):
        return {}


def credential(provider, source):
    """Synthetic checks, intentionally not run during the testing pause.

    >>> value = credential('cursor', {'access_token': 'fixture', 'expires_at': 1})
    >>> assert value['client_id'] == CLIENT_IDS['cursor'] and value['expires_at'] == 1
    >>> credential('codex', {'access_token': 'bad\\nheader'})
    Traceback (most recent call last):
        ...
    ValueError: credential field is invalid
    >>> credential('claude', {'access_token': 'fixture', 'expires_at': -1})
    Traceback (most recent call last):
        ...
    ValueError: credential expiry is invalid
    """
    result = {'provider': provider}
    for key in ('access_token', 'refresh_token', 'client_id', 'client_secret', 'account_id', 'project', 'scope'):
        value = source.get(key)
        if value is None:
            continue
        minimum = 32 if key == 'scope' else 33
        if not isinstance(value, str) or not value or len(value.encode()) >= 4096 or any(ord(c) < minimum or ord(c) > 126 for c in value):
            raise ValueError('credential field is invalid')
        result[key] = value
    if 'access_token' not in result and 'refresh_token' not in result:
        raise ValueError('no OAuth account credential available')
    if provider in CLIENT_IDS:
        result.setdefault('client_id', CLIENT_IDS[provider])
    expires = source.get('expires_at', jwt_claims(result.get('access_token', '')).get('exp'))
    if expires is not None:
        if isinstance(expires, bool) or not isinstance(expires, (int, float)) or not math.isfinite(expires) or expires < 0 or int(expires) != expires:
            raise ValueError('credential expiry is invalid')
        result['expires_at'] = int(expires)
    return result


def codex(path):
    store = read_json(path)
    tokens = store.get('tokens')
    if not isinstance(tokens, dict):
        raise ValueError('Codex ChatGPT OAuth login is required; API keys are not subscription quota credentials')
    return credential('codex', tokens)


def claude(credential_file=None):
    if credential_file:
        store = read_json(credential_file)
    else:
        if sys.platform != 'darwin':
            raise ValueError('specify --claude-credentials on this platform')
        service = 'Claude Code-credentials'
        configured = os.environ.get('CLAUDE_SECURESTORAGE_CONFIG_DIR')
        if configured is None:
            configured = os.environ.get('CLAUDE_CONFIG_DIR')
        if configured:
            normalized = unicodedata.normalize('NFC', configured)
            service += '-' + hashlib.sha256(normalized.encode()).hexdigest()[:8]
        # Capture both streams: Keychain output contains secrets and must never reach stdout.
        reply = subprocess.run(['security', 'find-generic-password', '-a', getpass.getuser(), '-s', service, '-w'],
                               capture_output=True, check=False, timeout=10)
        if reply.returncode != 0:
            raise ValueError('selected Claude Keychain credential is unavailable')
        if len(reply.stdout) > MAX_STORE_BYTES:
            raise ValueError('credential store exceeds size limit')
        store = json.loads(reply.stdout)
    tokens = store.get('claudeAiOauth')
    if not isinstance(tokens, dict):
        raise ValueError('unsupported Claude credential store version')
    source = {key: tokens.get(other) for key, other in (
        ('access_token', 'accessToken'), ('refresh_token', 'refreshToken'))}
    scopes = tokens.get('scopes')
    if scopes is not None:
        if not isinstance(scopes, list) or any(not isinstance(s, str) or not s or ' ' in s for s in scopes):
            raise ValueError('unsupported Claude OAuth scope format')
        if scopes:
            source['scope'] = ' '.join(scopes)
    if tokens.get('expiresAt') is not None:
        expires = tokens['expiresAt']
        if isinstance(expires, bool) or not isinstance(expires, (int, float)) or not math.isfinite(expires) or expires < 0:
            raise ValueError('credential expiry is invalid')
        source['expires_at'] = int(expires / 1000)
    return credential('claude', source)


def cursor(path):
    # The database may contain chats; query only the two named authentication keys.
    uri = Path(path).resolve().as_uri() + '?mode=ro'
    with sqlite3.connect(uri, uri=True) as database:
        rows = database.execute("SELECT key,value FROM ItemTable WHERE key IN (?,?)",
                                ('cursorAuth/accessToken', 'cursorAuth/refreshToken')).fetchall()
    values = dict(rows)
    source = {key: values.get(other) for key, other in (
        ('access_token', 'cursorAuth/accessToken'), ('refresh_token', 'cursorAuth/refreshToken'))}
    return credential('cursor', source)


def antigravity(path):
    # ponytail: native AG OAuthTokenInfo persistence is unverified; use a selected OAuth JSON until its codec is documented.
    if not path:
        raise ValueError('native Antigravity export is unsupported; specify a provider OAuth credential JSON')
    source = read_json(path)
    if source.get('type') not in (None, 'authorized_user'):
        raise ValueError('Antigravity requires user OAuth credentials')
    if not source.get('client_id') or not source.get('client_secret') or not source.get('project'):
        raise ValueError('Antigravity OAuth client and cloud project are required')
    return credential('antigravity', source)


def write_private(path, data):
    path = Path(path).expanduser().resolve()
    if '.local' not in path.parts:
        raise ValueError('credential output must be inside a .local directory')
    path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    fd, temporary = tempfile.mkstemp(prefix='.account-', dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, 'w') as stream:
            json.dump(data, stream, separators=(',', ':'))
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main():
    home = Path.home()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('providers', nargs='+', choices=('codex', 'claude', 'cursor', 'antigravity'))
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1] / '.local' / 'ai-accounts.json')
    parser.add_argument('--codex-auth', type=Path, default=Path(os.environ.get('CODEX_HOME', home / '.codex')) / 'auth.json')
    parser.add_argument('--claude-credentials', type=Path)
    parser.add_argument('--cursor-db', type=Path, default=home / 'Library/Application Support/Cursor/User/globalStorage/state.vscdb')
    parser.add_argument('--antigravity-credentials', type=Path)
    args = parser.parse_args()
    readers = {
        'codex': lambda: codex(args.codex_auth),
        'claude': lambda: claude(args.claude_credentials),
        'cursor': lambda: cursor(args.cursor_db),
        'antigravity': lambda: antigravity(args.antigravity_credentials),
    }
    try:
        accounts = [readers[provider]() for provider in dict.fromkeys(args.providers)]
        write_private(args.output, {'accounts': accounts})
    except (OSError, ValueError, TypeError, sqlite3.Error, subprocess.SubprocessError):
        # Avoid source exception messages: JSON errors, subprocesses and DBs can include account data.
        print('Export failed: selected credentials are unavailable or unsupported. See docs/ai-account-sources.md.', file=sys.stderr)
        return 1
    print('Selected credentials exported to a private .local JSON file (mode 0600).')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
