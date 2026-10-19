#!/usr/bin/env bash
# Optional online-module dependencies. Does not touch the graphics toolchain or user data.
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
prefix=${BB_ONLINE_DEPS:-$PWD/deps-online-x86_64}
jobs=${BB_BUILD_JOBS:-2}
export MACOSX_DEPLOYMENT_TARGET=26.0
[[ $jobs =~ ^[1-9][0-9]*$ ]] || exit 1
mkdir -p "$prefix/src" "$prefix/logs"
fetch() {
    python3 - "$prefix/src" "$1" "$2" "$3" <<'PY'
import io,pathlib,sys,tarfile,urllib.request
base,name,repo,rev=sys.argv[1:]; dest=pathlib.Path(base)/name
if (dest/'.source-revision').exists():
    assert (dest/'.source-revision').read_text().strip()==rev
else:
    assert not dest.exists(), f'Unrecognized existing source: {dest}'
    request=urllib.request.Request(f'https://codeload.github.com/{repo}/tar.gz/{rev}',headers={'User-Agent':'Blood-Echoes-build'})
    data=urllib.request.urlopen(request,timeout=120).read()
    with tarfile.open(fileobj=io.BytesIO(data),mode='r:gz') as archive:
        top=archive.getmembers()[0].name.split('/')[0]
        archive.extractall(path=base,filter='data')
    (pathlib.Path(base)/top).rename(dest)
    (dest/'.source-revision').write_text(rev+'\n')
print(dest)
PY
}
if [[ ! -f $prefix/lib/libssl.a || $(cat "$prefix/.openssl-target" 2>/dev/null || true) != 26.0 ]]; then
    fetch openssl openssl/openssl 286ddeaac037533bbdce65b3c689e3f7ffebf0f6
    (
        cd "$prefix/src/openssl"
        [[ ! -f Makefile ]] || make clean
        ./Configure darwin64-x86_64-cc no-shared no-tests -mmacosx-version-min=26.0 --prefix="$prefix" --openssldir="$prefix/ssl"
        make -j "$jobs"
        make install_sw
    ) > "$prefix/logs/openssl.log" 2>&1 || { tail -50 "$prefix/logs/openssl.log"; exit 1; }
    echo 26.0 > "$prefix/.openssl-target"
fi
if [[ ! -f $prefix/lib/libprotobuf.a || ! -x $prefix/bin/protoc ]]; then
    fetch protobuf protocolbuffers/protobuf f0dc78d7e6e331b8c6bb2d5283e06aa26883ca7c
    cmake -S "$prefix/src/protobuf/cmake" -B "$prefix/src/protobuf/build-x86_64" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_INSTALL_PREFIX="$prefix" \
        -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON > "$prefix/logs/protobuf.log" 2>&1
    cmake --build "$prefix/src/protobuf/build-x86_64" --parallel "$jobs" >> "$prefix/logs/protobuf.log" 2>&1 || { tail -50 "$prefix/logs/protobuf.log"; exit 1; }
    cmake --install "$prefix/src/protobuf/build-x86_64" >> "$prefix/logs/protobuf.log" 2>&1
fi
if [[ ! -f $prefix/include/nlohmann/json.hpp ]]; then
    fetch json nlohmann/json 55f93686c01528224f448c19128836e7df245f72
    mkdir -p "$prefix/include"
    cp -R "$prefix/src/json/single_include/nlohmann" "$prefix/include/"
fi
if [[ ! -f $prefix/lib/libminiupnpc.a ]]; then
    fetch miniupnp miniupnp/miniupnp bf4215a7574f88aa55859db9db00e3ae58cf42d6
    cmake -S "$prefix/src/miniupnp/miniupnpc" -B "$prefix/src/miniupnp/build-x86_64" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64 \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0 -DCMAKE_INSTALL_PREFIX="$prefix" \
        -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_STATIC=ON -DUPNPC_BUILD_TESTS=OFF \
        -DUPNPC_BUILD_SAMPLE=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON > "$prefix/logs/miniupnp.log" 2>&1
    cmake --build "$prefix/src/miniupnp/build-x86_64" --parallel "$jobs" >> "$prefix/logs/miniupnp.log" 2>&1
    cmake --install "$prefix/src/miniupnp/build-x86_64" >> "$prefix/logs/miniupnp.log" 2>&1
fi
printf 'Online dependencies ready: %s\n' "$prefix"
