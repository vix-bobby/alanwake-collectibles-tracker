"""The C parser must agree with tools/collectibles.py, and reject damaged files safely.

  python3 mod/test/test_profile.py PROFILE_TEST_BINARY CONFIG
"""
import random
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from collectibles import read_profile  # noqa: E402

binary, config = sys.argv[1], Path(sys.argv[2])
out = subprocess.run([binary, str(config)], capture_output=True, text=True, check=True).stdout
got = {}
for line in out.splitlines():
    head, numbers = line.split(":")
    got[head.split()[0]] = {int(n) for n in numbers.split()}
expected = read_profile(config)
assert got == expected, {k: (got.get(k), expected.get(k)) for k in expected if got.get(k) != expected.get(k)}
print("matches collectibles.py:", {k: len(v) for k, v in got.items()})

# truncated and corrupted input: any exit code is fine, crashing is not
random.seed(0)
data = config.read_bytes()
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / "config"
    cases = [data[:n] for n in range(len(data))]
    for _ in range(3000):
        broken = bytearray(data)
        for _ in range(random.randint(1, 8)):
            broken[random.randrange(len(broken))] = random.randrange(256)
        cases.append(bytes(broken))
    for case in cases:
        path.write_bytes(case)
        code = subprocess.run([binary, str(path)], capture_output=True, timeout=20).returncode
        assert code in (0, 1), (code, len(case))
print(f"{len(cases)} damaged inputs handled")
