"""Fetch pinned compiler sources into ignored build/deps (no global installs)."""
from pathlib import Path, PurePosixPath
import hashlib
import json
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = {
    "uam": ("PalindromicBreadLoaf/uam", "c0d76aa9107d67edcd35a1b717ace87ee7939c68"),
    "spirv-cross": ("KhronosGroup/SPIRV-Cross", "ebe2aa0cd80f5eb5cd8a605da604cacf72205f3b"),
}


def main():
    output = ROOT / "build/deps"
    output.mkdir(parents=True, exist_ok=True)
    for name, (repo, revision) in DEPS.items():
        destination = output / name
        marker = destination / ".source-revision"
        if marker.is_file() and marker.read_text().strip() == revision:
            print(f"{name}: {revision} (already present)")
            continue
        archive = output / f"{name}-{revision}.zip"
        if not archive.is_file():
            request = urllib.request.Request(
                f"https://codeload.github.com/{repo}/zip/{revision}",
                headers={"User-Agent": "switch-frame-generator"},
            )
            print(f"Downloading {repo}@{revision}", flush=True)
            with urllib.request.urlopen(request, timeout=90) as response:
                archive.write_bytes(response.read())
        with zipfile.ZipFile(archive) as source:
            for entry in source.infolist():
                relative = PurePosixPath(entry.filename)
                parts = relative.parts[1:]
                if not parts or entry.is_dir():
                    continue
                if relative.is_absolute() or any(p in ("..", ".") for p in parts):
                    raise ValueError("Unsafe archive member")
                target = destination.joinpath(*parts)
                if not target.resolve().is_relative_to(destination.resolve()):
                    raise ValueError("Archive member escapes dependency directory")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read(entry))
        marker.write_text(revision + "\n")
        (output / f"{name}-provenance.json").write_text(json.dumps({
            "repository": repo, "commit": revision,
            "archive_sha256": hashlib.sha256(archive.read_bytes()).hexdigest(),
        }, indent=2) + "\n")
        print(f"{name}: ready")


if __name__ == "__main__":
    main()
