#!/bin/sh
# Make the release zip in ../dist from build/d3d9.dll (or the DLL given as first argument).
set -eu
cd "$(dirname "$0")"
dll="${1:-build/d3d9.dll}"
version=$(sed -n 's/^#define MOD_VERSION "\(.*\)"$/\1/p' src/proxy.c)
[ -n "$version" ] && [ -f "$dll" ] || { echo "build first: ./build.sh" >&2; exit 1; }
grep -qa "$version" "$dll" || { echo "$dll is not version $version" >&2; exit 1; }
mkdir -p ../dist
python3 -I - "$dll" "$version" <<'PY'
import sys, zipfile
from pathlib import Path

dll, version = Path(sys.argv[1]), sys.argv[2]
out = Path("../dist") / f"AlanWakeCollectibles-{version}.zip"
texts = {"AlanWakeCollectibles-README.txt": Path("release/README.txt")}
if Path("../LICENSE").exists():
    texts["AlanWakeCollectibles-LICENSE.txt"] = Path("../LICENSE")
else:
    print("WARNING: no LICENSE file yet; the zip has none", file=sys.stderr)
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    z.write(dll, "d3d9.dll")
    for name, path in texts.items():  # Windows line endings for Notepad
        z.writestr(name, path.read_text(encoding="utf-8").replace("\n", "\r\n"))
    print(out.resolve())
    for info in z.infolist():
        print(f"  {info.file_size:7}  {info.filename}")
PY
