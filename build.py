"""Build private LSFG shader cache and a native Switch NRO. Never deploys."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "build"
MSYS = Path(os.environ.get("SFG_MSYS_ROOT", "C:/msys64"))
HOST = OUT / "host-win"
SDK_VALUE = os.environ.get("DEVKITPRO", "")
SDK = MSYS / "opt/devkitpro" if SDK_VALUE in ("", "/opt/devkitpro") else Path(SDK_VALUE)
UPSTREAM = ROOT / "third_party/drastic/third_party/lsfg-deko3d"


def run(args, *, env=None, log=None):
    args = [str(a) for a in args]
    print("Running:", Path(args[0]).name, " ".join(args[1:4]), flush=True)
    if log:
        with log.open("w", encoding="utf-8") as output:
            result = subprocess.run(args, cwd=ROOT, env=env, stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            print(log.read_text(errors="replace")[-8000:])
            raise RuntimeError(f"Command failed ({result.returncode}); see {log}")
    else:
        subprocess.run(args, cwd=ROOT, env=env, check=True)


def host_environment():
    env = os.environ.copy()
    env["PATH"] = str(MSYS / "ucrt64/bin") + os.pathsep + str(MSYS / "usr/bin") + os.pathsep + env.get("PATH", "")
    env["PYTHONPATH"] = str(OUT / "python") + os.pathsep + env.get("PYTHONPATH", "")
    # switch.specs resolves its linker script through DEVKITPRO; native tools
    # cannot interpret the MSYS /opt/devkitpro inherited from this shell.
    env["DEVKITPRO"] = SDK.as_posix()
    env["DEVKITA64"] = (SDK / "devkitA64").as_posix()
    return env


def host_build(env):
    if not (OUT / "deps/uam/.source-revision").exists() or not (OUT / "deps/spirv-cross/.source-revision").exists():
        run([sys.executable, ROOT / "tools/fetch_deps.py"], env=env)
    if not (OUT / "python/mako").is_dir():
        run([sys.executable, "-m", "pip", "install", "--target", OUT / "python", "-r", ROOT / "tools/requirements-host.txt"])
    ninja = OUT / "ninja/bin/ninja.exe"
    if not ninja.is_file():
        run([sys.executable, "-m", "pip", "install", "--target", OUT / "ninja", "ninja==1.13.0"])
    cmake = Path(os.environ.get("SFG_CMAKE", "C:/Program Files/CMake/bin/cmake.exe"))
    if not cmake.is_file():
        raise RuntimeError("Native Windows CMake required; set SFG_CMAKE (not MSYS CMake)")
    run([cmake, "-S", ROOT, "-B", HOST, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
         f"-DCMAKE_C_COMPILER={MSYS / 'ucrt64/bin/gcc.exe'}",
         f"-DCMAKE_CXX_COMPILER={MSYS / 'ucrt64/bin/g++.exe'}",
         f"-DCMAKE_MAKE_PROGRAM={ninja}", f"-DUAM_PYTHON={sys.executable}",
         f"-DSFG_LIBNX_INCLUDE={SDK / 'libnx/include'}",
         f"-DUAM_BISON={MSYS / 'usr/bin/bison.exe'}", f"-DUAM_FLEX={MSYS / 'usr/bin/flex.exe'}"], env=env, log=OUT / "configure.log")
    run([cmake, "--build", HOST, "-j", str(min(os.cpu_count() or 2, 8))], env=env, log=OUT / "host-build.log")


def prepare(env, dll):
    run([HOST / "lsfg_prepare.exe", dll, OUT / "cache"], env=env, log=OUT / "prepare.log")
    print((OUT / "prepare.log").read_text()[-550:])
    key = (OUT / "cache/selected.txt").read_text().strip()
    if not re.fullmatch("[0-9a-f]{64}", key):
        raise RuntimeError("Invalid prepared cache key")
    run([HOST / "generator_recording_test.exe", OUT / "cache" / key], env=env)


def target_build(env, dll):
    name = (OUT / "cache/selected.txt").read_text().strip()
    if not re.fullmatch("[0-9a-f]{64}", name):
        raise RuntimeError("Invalid cache selection")
    cache = OUT / "cache" / name
    manifest = (cache / "manifest.bin").read_bytes()
    dll_hash = hashlib.sha256(dll.read_bytes()).hexdigest()
    if len(manifest) < 168 or manifest[8:40].hex() != dll_hash:
        raise RuntimeError("Prepared cache belongs to a different DLL; rerun full build")
    romfs = OUT / "romfs"
    (romfs / "shaders").mkdir(parents=True, exist_ok=True)
    (romfs / "lsfg").mkdir(parents=True, exist_ok=True)
    # Explicit allowlist, never recursively package root/private directories.
    for path in cache.iterdir():
        if path.is_file() and (path.name == "manifest.bin" or re.fullmatch(r"[0-9]{2}\.dksh", path.name)):
            shutil.copyfile(path, romfs / "lsfg" / path.name)
    for stage, source, output in [("vert", "fullscreen.vert", "fullscreen"), ("frag", "scene.frag", "scene"), ("frag", "hud.frag", "hud")]:
        run([SDK / "tools/bin/uam.exe", "-s", stage, "-o", romfs / "shaders" / (output + ".dksh"), ROOT / "shaders" / source], env=env)
    digest = hashlib.sha256()
    for folder in ("source", "include", "shaders", "third_party/drastic"):
        for path in sorted((ROOT / folder).rglob("*")):
            if path.is_file():
                digest.update(path.relative_to(ROOT).as_posix().encode())
                digest.update(path.read_bytes())
    digest.update(bytes.fromhex(dll_hash))
    build_id = digest.hexdigest()[:16]
    config = OUT / "config"
    config.mkdir(exist_ok=True)
    (config / "build_info.hpp").write_text(f'#pragma once\n#define SFG_BUILD_ID "{build_id}"\n#define SFG_DLL_HASH "{dll_hash}"\n')
    a64 = SDK / "devkitA64/bin/aarch64-none-elf-g++.exe"
    objects = OUT / "target"
    objects.mkdir(exist_ok=True)
    flags = ["-std=gnu++20", "-O2", "-g", "-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE",
             "-ffunction-sections", "-fdata-sections", "-fno-rtti", "-D__SWITCH__", "-D_GNU_SOURCE",
             "-I" + str(ROOT / "include"), "-I" + str(UPSTREAM / "include"), "-I" + str(config), "-isystem", str(SDK / "libnx/include")]
    sources = sorted((ROOT / "source").glob("*.cpp")) + [p for p in sorted((UPSTREAM / "src").glob("*.cpp"))
        if p.name not in ("compile_uam.cpp", "prepare.cpp", "translate.cpp")]
    object_paths = []
    for path in sources:
        obj = objects / (path.stem + ".o")
        warning_flags = ["-Wall", "-Wextra", "-Werror"] if path.parent == ROOT / "source" else ["-w"]
        run([a64, *flags, *warning_flags, "-c", path, "-o", obj], env=env)
        object_paths.append(obj)
    elf = OUT / "switch-frame-generator.elf"
    nro = OUT / "switch-frame-generator.nro"
    nacp = OUT / "switch-frame-generator.nacp"
    run([a64, *flags, f"-specs={SDK / 'libnx/switch.specs'}", "-Wl,--gc-sections",
         "-Wl,-Map," + str(OUT / "switch-frame-generator.map"), *object_paths,
         "-L" + str(SDK / "libnx/lib"), "-ldeko3dd", "-lnx", "-lm", "-o", elf], env=env)
    run([SDK / "tools/bin/nacptool.exe", "--create", "Switch Frame Generator", "SFG contributors", "0.1.0", nacp], env=env)
    run([SDK / "tools/bin/elf2nro.exe", elf, nro, f"--nacp={nacp}", f"--romfsdir={romfs}"], env=env)
    metadata = {"build_id": build_id, "dll_sha256": dll_hash, "cache_key": name,
                "nro_sha256": hashlib.sha256(nro.read_bytes()).hexdigest(), "nro_bytes": nro.stat().st_size,
                "resolution": [1280, 720], "performance": True, "flow": "1/4", "generated_frames": 1}
    (OUT / "build.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps(metadata, indent=2))
    print("Built:", nro)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, default=ROOT / "Lossless.dll")
    parser.add_argument("--host-only", action="store_true")
    parser.add_argument("--target-only", action="store_true")
    parser.add_argument("--test", action="store_true", help="also run upstream real-DLL compiler/binding/cache checks")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("Use native Windows Python, e.g. py -3 build.py, not MSYS /usr/bin/python")
    if not args.dll.is_file():
        raise RuntimeError("Provide your own Lossless.dll in the project root, or --dll PATH")
    OUT.mkdir(exist_ok=True)
    env = host_environment()
    if not args.target_only:
        host_build(env)
        run([HOST / "scene_test.exe"], env=env)
        run([HOST / "frame_pacer_test.exe"], env=env)
        prepare(env, args.dll)
    if args.test:
        run([HOST / "lsfg_validation.exe", args.dll, OUT / "validation-cache"], env=env, log=OUT / "validation-host.log")
        print((OUT / "validation-host.log").read_text()[-350:])
    if not args.host_only:
        target_build(env, args.dll)
        if args.test:
            run([sys.executable, ROOT / "tests/check.py"], env=env)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print("Build failed:", error, file=sys.stderr)
        sys.exit(1)
