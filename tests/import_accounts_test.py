"""Synthetic USB import regression; no account stores or physical serial ports."""
import contextlib
import importlib.util
import io
import json
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('import_accounts', root/'tools/import_accounts.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class Serial:
    def __init__(self, port=None, *args, **kwargs):
        self.dtr = self.rts = True
        self.port, self.opened, self.data = port, False, bytearray()
        if port is not None:
            self.open()
    def open(self):
        assert not self.dtr and not self.rts, 'USB reset signals must be disabled before opening'
        assert self.port == 'fixture-port'
        self.opened = True
    def __enter__(self):
        assert self.opened
        return self
    def __exit__(self, *args):
        assert self.data.startswith(b'account ') and self.data.endswith(b'\n')
    def reset_input_buffer(self): pass
    def write(self, data): self.data.extend(data)
    def flush(self): pass
    def readline(self): return b'SH_ACCOUNT OK\n'

with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)/'fixture.json'
    path.write_text(json.dumps({'provider':'claude', 'access_token':'synthetic-token'}))
    output = io.StringIO()
    with patch.object(module.serial, 'Serial', Serial), patch.object(module.time, 'sleep'), \
         patch.object(sys, 'argv', ['import_accounts', '--port', 'fixture-port', '--file', str(path)]), \
         contextlib.redirect_stdout(output):
        assert module.main() == 0
    assert 'claude: imported' in output.getvalue() and 'synthetic-token' not in output.getvalue()
print('PASS private importer: reset signals disabled before open; acknowledgement handled; token not printed')
