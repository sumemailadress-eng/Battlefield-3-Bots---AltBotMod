#!/usr/bin/env python3
"""Build the complete release ZIP, then verify its actual contents and hashes."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zipfile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = args.output.resolve()
    if output.is_relative_to(root):
        raise SystemExit("Place the output ZIP outside the package tree.")
    setup = (root / "files/BotMod-Setup.ps1").read_text()
    declaration = re.search(r"\$RequiredPackageFiles\s*=\s*@\((.*?)\)", setup, re.S)
    if not declaration:
        raise SystemExit("Installer required-payload declaration is missing.")
    required = ["files/" + p.replace("\\", "/") for p in re.findall(r"'([^']+)'", declaration.group(1))]
    required += ["Install-BotMod.bat", "Uninstall-BotMod.bat", "files/BotMod-Setup.ps1"]
    missing = [p for p in required if not (root / p).is_file()]
    if missing:
        raise SystemExit("Required package file missing: " + ", ".join(missing))
    for name in ("Install-BotMod.bat", "Uninstall-BotMod.bat"):
        batch = (root / name).read_text()
        target = re.search(r'-File "%~dp0([^"%]+)"', batch)
        if not target or not (root / target.group(1).replace("\\", "/")).is_file():
            raise SystemExit("Broken installer launcher: " + name)
    payload = {}
    for p in sorted(root.rglob("*")):
        if not p.is_file() or p.name == "PACKAGE-SHA256.json":
            continue
        rel = p.relative_to(root)
        if "__pycache__" in rel.parts or p.suffix in {".pyc", ".orig", ".rej"}:
            continue
        payload[rel.as_posix()] = sha(p.read_bytes())
    manifest = root / "PACKAGE-SHA256.json"
    manifest.write_text(json.dumps(payload, indent=2) + "\n")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for rel in sorted([*payload, "PACKAGE-SHA256.json"]):
            z.write(root / rel, rel)
    with zipfile.ZipFile(output) as z:
        if z.testzip() is not None:
            raise SystemExit("ZIP CRC verification failed.")
        names = set(z.namelist())
        if names != set(payload) | {"PACKAGE-SHA256.json"}:
            raise SystemExit("Archive entries differ from the package manifest.")
        for rel in required:
            if rel not in names:
                raise SystemExit("ZIP omitted required installer file: " + rel)
        recorded = json.loads(z.read("PACKAGE-SHA256.json"))
        for rel, digest in recorded.items():
            if sha(z.read(rel)) != digest:
                raise SystemExit("Archive payload hash mismatch: " + rel)
    print(json.dumps({"archive": str(output), "bytes": output.stat().st_size,
                      "sha256": sha(output.read_bytes()), "payloads_verified": len(payload),
                      "required_installer_files_verified": len(required)}))


if __name__ == "__main__":
    main()
