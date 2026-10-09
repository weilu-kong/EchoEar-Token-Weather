#!/usr/bin/env python3
"""Import private account records over USB; no credentials are printed."""
import argparse
import json
from pathlib import Path
import time
import serial

PROVIDERS = {'codex', 'antigravity', 'claude', 'cursor', 'google'}


def records(path):
    """Bounded fixture checks; not run during the user's verification pause.

    >>> import tempfile
    >>> with tempfile.TemporaryDirectory() as directory:
    ...     fixture = Path(directory) / 'fixture.json'
    ...     fixture.write_text('{"provider":"google","access_token":"fixture"}')
    ...     assert records(fixture)[0][0] == 'google'
    ...     fixture.write_text('{"provider":"unknown","access_token":"fixture"}')
    ...     try:
    ...         records(fixture)
    ...     except ValueError:
    ...         pass
    ...     else:
    ...         raise AssertionError('unknown provider accepted')
    """
    raw = path.read_bytes()
    if len(raw) > 65536:
        raise ValueError('Account file exceeds size limit')
    data = json.loads(raw)
    accounts = data.get('accounts', [data]) if isinstance(data, dict) else []
    if not isinstance(accounts, list) or not accounts or len(accounts) > 5:
        raise ValueError('Invalid account records')
    result = []
    for account in accounts:
        if not isinstance(account, dict) or account.get('provider') not in PROVIDERS:
            raise ValueError('Unknown provider')
        token = account.get('access_token')
        if not isinstance(token, str) or not token or len(token) > 4096:
            raise ValueError('Missing or invalid access token')
        encoded = json.dumps(account, separators=(',', ':'), ensure_ascii=True).encode('ascii')
        if len(encoded) > 12288:
            raise ValueError('Account record exceeds device limit')
        result.append((account['provider'], b'account ' + encoded + b'\n'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--file', type=Path, required=True)
    args = parser.parse_args()
    try:
        accounts = records(args.file)
        connection = serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=5)
        connection.dtr = False
        connection.rts = False
        connection.port = args.port
        connection.open()
        with connection:
            time.sleep(8)  # Opening USB can reset the board; wait for its console without asserting reset signals.
            connection.reset_input_buffer()
            for provider, command in accounts:
                for start in range(0, len(command), 128):
                    connection.write(command[start:start + 128])
                    time.sleep(0.025)
                connection.flush()
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    line = connection.readline()
                    if line.strip() == b'SH_ACCOUNT OK':
                        print(f'{provider}: imported')
                        break
                    if line.startswith(b'SH_ACCOUNT '):
                        raise ValueError('Device rejected account; check schema and available NVS space')
                else:
                    raise ValueError('No account acknowledgement; device needs the account-enabled firmware')
    except (OSError, ValueError, serial.SerialException):
        print('Import failed. Check account file, USB port and firmware; credentials were not printed.')
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
