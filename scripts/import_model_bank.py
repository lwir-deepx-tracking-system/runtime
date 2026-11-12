#!/usr/bin/env python3
"""Copy a verified model-bank snapshot into Runtime's local, Git-ignored model store.

The source is a models directory containing MANIFEST.json, detect/ and reid/.
No model is declared executable by this copy step; YAML and DXRT contract checks
remain separate requirements.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def import_models(source: Path, target: Path) -> tuple[int, int]:
    source = source.resolve(strict=True)
    manifest = json.loads((source / "MANIFEST.json").read_text())
    files = manifest.get("_files")
    links = manifest.get("_links")
    if not isinstance(files, dict) or not isinstance(links, dict) or not files:
        raise ValueError("MANIFEST.json requires _files and _links")
    for name, relative in links.items():
        if relative not in files or manifest.get(name) != files[relative]:
            raise ValueError(f"Manifest link/hash mismatch: {name}")

    # Validate the entire source before touching Runtime files.
    for relative, expected in files.items():
        path = (source / relative).resolve(strict=True)
        if not path.is_relative_to(source) or path.suffix != ".dxnn" or not path.is_file():
            raise ValueError(f"Unsafe model path: {relative}")
        if digest(path) != expected:
            raise ValueError(f"SHA-256 mismatch: {relative}")

    manifest_target = target / "MANIFEST.json"
    if manifest_target.exists() and json.loads(manifest_target.read_text()) != manifest:
        raise ValueError("Existing Runtime manifest differs")
    for relative, expected in files.items():
        destination = target / relative
        if destination.is_symlink():
            raise ValueError(f"Runtime model target is a symlink: {destination}")
        if destination.exists() and digest(destination) != expected:
            raise ValueError(f"Existing Runtime model has different SHA-256: {destination}")

    copied = skipped = 0
    for relative, expected in files.items():
        destination = target / relative
        if destination.exists():
            skipped += 1
            continue
        destination.parent.mkdir(parents=True, exist_ok=True)
        fd, temporary = tempfile.mkstemp(prefix=".model-", dir=destination.parent)
        try:
            with os.fdopen(fd, "wb") as output, (source / relative).open("rb") as input_file:
                shutil.copyfileobj(input_file, output, 1024 * 1024)
            if digest(Path(temporary)) != expected:
                raise ValueError(f"Copy SHA-256 mismatch: {relative}")
            os.replace(temporary, destination)
            copied += 1
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    target.mkdir(parents=True, exist_ok=True)
    if not manifest_target.exists():
        manifest_target.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    return copied, skipped


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True, help="model-bank models/ directory")
    parser.add_argument("--target", type=Path,
                        default=Path(__file__).resolve().parents[1] / "models" / "imported")
    arguments = parser.parse_args()
    copied_count, skipped_count = import_models(arguments.source, arguments.target)
    print(f"Verified model-bank import: copied={copied_count} existing={skipped_count} target={arguments.target}")
