#!/bin/sh
# Build the mod. `./build.sh test` also runs the tests (WSL: runs a Windows test program).
set -eu
cd "$(dirname "$0")"
CC=i686-w64-mingw32-gcc
CFLAGS="-std=gnu11 -O2 -Wall -Wextra"
mkdir -p build

$CC $CFLAGS -shared -static -static-libgcc -s -Wl,--kill-at -Wl,--enable-stdcall-fixup \
    -o build/d3d9.dll src/proxy.c src/profile.c src/counts.c src/settings.c src/label.c src/menu.c src/checkpoint.c src/steam.c src/d3d9.def -ladvapi32 -luser32
$CC $CFLAGS -static -o build/harness.exe test/harness.c -luser32
$CC $CFLAGS -static -o build/menu_test.exe test/menu_test.c src/label.c src/counts.c src/profile.c

if [ "${1:-}" = test ]; then
    # made-up fixtures by default; CONFIG and REPORT point the tests at real files instead
    config="${CONFIG:-test/fixtures/config}"
    report="${REPORT:-test/fixtures/report.json}"
    # parser: native build with memory checks
    gcc $CFLAGS -fsanitize=address,undefined -fno-sanitize-recover \
        -o build/profile_test test/profile_test.c src/profile.c
    # old AddressSanitizer crashes at random on recent kernels unless ASLR is off
    setarch -R python3 test/test_profile.py build/profile_test "$config"
    gcc $CFLAGS -fsanitize=undefined -o build/counts_test test/counts_test.c src/counts.c src/profile.c
    python3 test/test_counts.py build/counts_test "$config" "$report"
    gcc $CFLAGS -fsanitize=undefined -o build/label_test test/label_test.c src/label.c src/counts.c src/profile.c
    build/label_test
    gcc $CFLAGS -fsanitize=address,undefined -fno-sanitize-recover -o build/chapter_test \
        test/chapter_test.c src/checkpoint.c src/counts.c src/profile.c
    setarch -R python3 test/test_chapter.py build/chapter_test "$config" "$report" \
        "$(dirname "$config")/savegame.aws"
    (cd build && ./menu_test.exe)
    # the settings file is tested on a Windows disk: saving fails on \\wsl network paths
    $CC $CFLAGS -static -o build/settings_test.exe test/settings_test.c src/settings.c
    wintemp=$(wslpath "$(cmd.exe /c 'echo %TEMP%' 2>/dev/null | tr -d '\r')" )
    scratch=$(mktemp -d "$wintemp/awmod-test.XXXXXX")
    cp build/settings_test.exe "$scratch/"
    (cd "$scratch" && ./settings_test.exe)
    $CC $CFLAGS -static -o build/steam_test.exe test/steam_test.c src/steam.c
    mkdir "$scratch/steam"
    cp build/steam_test.exe "$scratch/steam/"
    (cd "$scratch/steam" && ./steam_test.exe)
    rm -rf "$scratch"
    rm -f build/alanwake_collectibles.ini
    (
        ALANWAKE_COLLECTIBLES_PROFILE=$(wslpath -w "$config")
        export ALANWAKE_COLLECTIBLES_PROFILE WSLENV=ALANWAKE_COLLECTIBLES_PROFILE
        cd build && ./harness.exe ./d3d9.dll && cat alanwake_collectibles.log
    )
fi
