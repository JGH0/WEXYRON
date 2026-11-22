#!/usr/bin/env python3
"""Download the vendor frame-generation SDK files the game builds against.

The SDK licences allow redistributing their binaries with the game, but not
committing them (or their headers) to this repository, so they are fetched
from pinned upstream releases into third_party/sdks/ (git-ignored):

    python tools/fetch_sdks.py           # fetch whatever is missing
    python tools/fetch_sdks.py --force   # download everything again

Without these files the game still builds and runs; the frame generation
options that need them report "not installed".

  AMD FSR SDK v2.4.0  (frame generation, Vulkan, Windows x64)
"""

import argparse
import hashlib
import sys
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "third_party" / "sdks"

FSR_TAG = "v2.4.0"
FSR_BASE = f"https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/{FSR_TAG}/"
FSR_FILES = [
    "Kits/FidelityFX/docs/license.md",
    "Kits/FidelityFX/api/include/ffx_api.h",
    "Kits/FidelityFX/api/include/ffx_api_types.h",
    "Kits/FidelityFX/api/include/ffx_api_loader.h",
    "Kits/FidelityFX/api/include/vk/ffx_api_vk.h",
    "Kits/FidelityFX/framegeneration/include/ffx_framegeneration.h",
    "Kits/FidelityFX/framegeneration/include/ffx_framegeneration_api_types.h",
    "Kits/FidelityFX/framegeneration/include/vk/ffx_api_framegeneration_vk.h",
    "Kits/FidelityFX/signedbin/amd_fidelityfx_loader_vk.dll",
    "Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_vk.dll",
]

# (download url, destination path relative to DEST)
FILES = [(FSR_BASE + f, Path("fidelityfx") / f) for f in FSR_FILES]


def fetch(url: str, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    tmp = target.with_suffix(target.suffix + ".part")
    with urllib.request.urlopen(url, timeout=120) as response, open(tmp, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    tmp.replace(target)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true", help="download even if present")
    args = parser.parse_args()

    fetched = 0
    for url, rel in FILES:
        target = DEST / rel
        if target.exists() and not args.force:
            continue
        print(f"fetch {rel}")
        try:
            fetch(url, target)
        except Exception as error:  # network, 404, ...
            print(f"fetch_sdks: {url}: {error}", file=sys.stderr)
            return 1
        fetched += 1
    stamp = DEST / "fidelityfx" / "VERSION"
    stamp.write_text(f"FidelityFX-SDK {FSR_TAG}\n")
    print(f"fetch_sdks: {fetched} file(s) downloaded into {DEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
