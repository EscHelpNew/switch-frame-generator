"""Local checks only. Uses built host tools and never connects to a Switch."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build"
HOST = OUT / "host-win"
ENV = os.environ.copy()
MSYS = Path(os.environ.get("SFG_MSYS_ROOT", "C:/msys64"))
ENV["PATH"] = str(MSYS / "ucrt64/bin") + os.pathsep + ENV.get("PATH", "")


def command(name, *args):
    return subprocess.run([str(HOST / (name + ".exe")), *map(str, args)], cwd=ROOT,
                          env=ENV, capture_output=True, text=True, timeout=30)


def cache_directory():
    return OUT / "cache" / (OUT / "cache/selected.txt").read_text().strip()


class LocalChecks(unittest.TestCase):
    def test_scene_fall_bounce_and_fixed_step_parity(self):
        result = command("scene_test")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_generator_warmup_reset_and_lifetime(self):
        result = command("generator_recording_test", cache_directory())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_invalid_dll_is_rejected_without_cache(self):
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            root = Path(temporary)
            dll = root / "invalid.dll"
            dll.write_bytes(b"not a PE DLL" * 32)
            result = command("lsfg_prepare", dll, root / "cache")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertFalse((root / "cache/selected.txt").exists())

    def test_missing_dll_is_rejected(self):
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            result = command("lsfg_prepare", Path(temporary) / "absent.dll", Path(temporary) / "cache")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)

    def test_corrupt_shader_is_rejected_by_target_runtime(self):
        with tempfile.TemporaryDirectory(dir=OUT) as temporary:
            root = Path(temporary)
            for source in cache_directory().iterdir():
                if source.is_file():
                    shutil.copyfile(source, root / source.name)
            module = root / "00.dksh"
            data = bytearray(module.read_bytes())
            data[0] ^= 255
            module.write_bytes(data)
            result = command("generator_recording_test", root)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("cache validation failed", result.stderr)

    def test_packaged_modules_match_manifest_hashes(self):
        root = OUT / "romfs/lsfg"
        manifest = (root / "manifest.bin").read_bytes()
        self.assertEqual(struct.unpack_from("<II", manifest), (0x4746534C, 2))
        count = struct.unpack_from("<I", manifest, 120)[0]
        self.assertEqual(count, 25)
        self.assertEqual(zlib.crc32(manifest[168:]), struct.unpack_from("<I", manifest, 160)[0])
        for index in range(count):
            module = (root / f"{index:02d}.dksh").read_bytes()
            entry = 168 + index * 96
            self.assertEqual(len(module), struct.unpack_from("<I", manifest, entry + 8)[0])
            self.assertEqual(hashlib.sha256(module).digest(), manifest[entry + 16:entry + 48])
        allowed = {f"{i:02d}.dksh" for i in range(count)} | {"manifest.bin"}
        self.assertEqual({p.name for p in root.iterdir()}, allowed)
        self.assertFalse(any(p.suffix.lower() == ".dll" for p in (OUT / "romfs").rglob("*")))

    def test_arm64_nro_and_native_resolution_metadata(self):
        elf = (OUT / "switch-frame-generator.elf").read_bytes()
        self.assertEqual(elf[:6], b"\x7fELF\x02\x01")
        self.assertEqual(struct.unpack_from("<H", elf, 18)[0], 183)
        nro = (OUT / "switch-frame-generator.nro").read_bytes()
        self.assertEqual(nro[16:20], b"NRO0")
        asset_start = struct.unpack_from("<I", nro, 24)[0]
        self.assertEqual(nro[asset_start:asset_start + 4], b"ASET")
        metadata = json.loads((OUT / "build.json").read_text())
        self.assertEqual(metadata["resolution"], [1280, 720])
        self.assertEqual(metadata["nro_sha256"], hashlib.sha256(nro).hexdigest())
        manifest = (OUT / "romfs/lsfg/manifest.bin").read_bytes()
        self.assertEqual(metadata["dll_sha256"], manifest[8:40].hex())


if __name__ == "__main__":
    unittest.main(verbosity=2)
