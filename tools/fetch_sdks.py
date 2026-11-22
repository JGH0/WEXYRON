#!/usr/bin/env python3
"""Download the vendor frame-generation SDK files the game builds against.

The SDK licences allow redistributing their binaries with the game, but not
committing them (or their headers) to this repository, so they are fetched
from pinned upstream releases into third_party/sdks/ (git-ignored):

    python tools/fetch_sdks.py           # fetch whatever is missing
    python tools/fetch_sdks.py --force   # download everything again

Without these files the game still builds and runs; the frame generation
options that need them report "not installed".

  AMD FSR SDK v2.4.0          (frame generation, Vulkan, Windows x64)
  NVIDIA Streamline v2.14.16  (DLSS frame generation + Reflex, Vulkan,
                               Windows x64). Only the needed members are read
                               out of the 276 MB release zip (HTTP ranges).
"""

import argparse
import io
import sys
import urllib.request
import zipfile
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

SL_TAG = "v2.14.16"
SL_ZIP = (f"https://github.com/NVIDIA-RTX/Streamline/releases/download/{SL_TAG}/"
          f"streamline-sdk-{SL_TAG}.zip")
# Production (signed, non-watermarked) runtime: interposer, the DLSS-G,
# Reflex and PCL plugins, the DLSS-G model and the Vulkan low-latency layer.
SL_BINARIES = [
    "bin/x64/sl.interposer.dll",
    "bin/x64/sl.common.dll",
    "bin/x64/sl.dlss_g.dll",
    "bin/x64/sl.reflex.dll",
    "bin/x64/sl.pcl.dll",
    "bin/x64/nvngx_dlssg.dll",
    "bin/x64/NvLowLatencyVk.dll",
]
SL_LICENSES = [
    "license.txt",                       # Streamline (MIT)
    "3rd-party-licenses.md",
    "bin/x64/nvngx_dlss.license.txt",    # NVIDIA RTX SDKs licence (DLSS / NGX)
    "bin/x64/reflex.license.txt",        # NVIDIA SDK licence (Reflex)
]

# (download url, destination path relative to DEST)
FILES = [(FSR_BASE + f, Path("fidelityfx") / f) for f in FSR_FILES]


class HttpRangeFile(io.RawIOBase):
    """Read-only, seekable view of a remote file through HTTP Range requests,
    so zipfile can pick single members out of a large archive."""

    BLOCK = 1 << 20

    def __init__(self, url: str):
        super().__init__()
        self.url = url
        self.pos = 0
        self.blocks = {}
        with self._get(0, 0) as response:
            content_range = response.headers.get("Content-Range", "")
            if response.status != 206 or "/" not in content_range:
                raise OSError("server does not support range requests")
            self.size = int(content_range.rsplit("/", 1)[1])
        self.downloaded = 0

    def _get(self, first: int, last: int):
        request = urllib.request.Request(self.url, headers={"Range": f"bytes={first}-{last}"})
        return urllib.request.urlopen(request, timeout=120)

    def readable(self):
        return True

    def seekable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, offset, whence=io.SEEK_SET):
        base = {io.SEEK_SET: 0, io.SEEK_CUR: self.pos, io.SEEK_END: self.size}[whence]
        self.pos = max(0, base + offset)
        return self.pos

    def _block(self, index: int) -> bytes:
        if index not in self.blocks:
            first = index * self.BLOCK
            last = min(self.size, first + self.BLOCK) - 1
            with self._get(first, last) as response:
                data = response.read()
            self.downloaded += len(data)
            if len(self.blocks) > 64:
                self.blocks.clear()
            self.blocks[index] = data
        return self.blocks[index]

    def readinto(self, buffer):
        if self.pos >= self.size:
            return 0
        n = min(len(buffer), self.size - self.pos)
        out = 0
        while out < n:
            index, offset = divmod(self.pos, self.BLOCK)
            chunk = self._block(index)[offset:offset + (n - out)]
            buffer[out:out + len(chunk)] = chunk
            out += len(chunk)
            self.pos += len(chunk)
        return out


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


def fetch_streamline(force: bool) -> int:
    """Headers, runtime DLLs and licences out of the Streamline release zip."""
    base = DEST / "streamline"
    stamp = base / "VERSION"
    if stamp.exists() and stamp.read_text().strip() == f"Streamline {SL_TAG}" and not force:
        return 0
    print(f"fetch Streamline {SL_TAG} (selected members of the release zip)")
    remote = HttpRangeFile(SL_ZIP)
    archive = zipfile.ZipFile(io.BufferedReader(remote, buffer_size=1 << 16))
    names = set(archive.namelist())
    wanted = [n for n in sorted(names) if n.startswith("include/") and n.endswith(".h")]
    wanted += SL_BINARIES + SL_LICENSES
    missing = [n for n in wanted if n not in names]
    if missing:
        raise OSError(f"not in the Streamline zip: {', '.join(missing)}")
    for name in wanted:
        target = base / name
        target.parent.mkdir(parents=True, exist_ok=True)
        tmp = target.with_suffix(target.suffix + ".part")
        with archive.open(name) as src, open(tmp, "wb") as out:   # zipfile checks the CRC
            while True:
                chunk = src.read(1 << 20)
                if not chunk:
                    break
                out.write(chunk)
        tmp.replace(target)
    stamp.write_text(f"Streamline {SL_TAG}\n")
    print(f"fetch_sdks: Streamline: {len(wanted)} file(s), {remote.downloaded >> 20} MB transferred")
    return len(wanted)


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

    try:
        fetched += fetch_streamline(args.force)
    except Exception as error:  # network, no range support, ...
        print(f"fetch_sdks: {SL_ZIP}: {error}", file=sys.stderr)
        return 1

    print(f"fetch_sdks: {fetched} file(s) downloaded into {DEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
