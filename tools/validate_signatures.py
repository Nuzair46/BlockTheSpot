#!/usr/bin/env python3
"""Validate config.ini against an installed Spotify, without modifying it.

Requires Python 3 and Node.js (for syntax checking patched JavaScript).
"""

import argparse
import configparser
from pathlib import Path
import re
import struct
import subprocess
import zipfile


def pattern(signature):
    tokens = signature.split()
    if not tokens or any(not re.fullmatch(r"[0-9a-fA-F]{2}|\?\?", t) for t in tokens):
        raise ValueError("invalid or empty signature")
    # The native parser currently rejects FF, even as a literal byte.
    if any(t.upper() == "FF" for t in tokens):
        raise ValueError("FF is not supported by the native hex parser")
    return re.compile(b"".join(
        br"[\s\S]" if t == "??" else re.escape(bytes.fromhex(t)) for t in tokens
    ))


def apply_patch(data, section, suffix="", capacity=1024):
    signature = section[f"Signature{suffix}"]
    value_text = section[f"Value{suffix}"]
    if max(len(signature), len(value_text)) >= capacity - 1:
        raise ValueError("INI value exceeds the hook's string buffer")
    regex = pattern(signature)
    # Include overlapping matches when checking uniqueness.
    matches = list(re.finditer(b"(?=" + regex.pattern + b")", data))
    if len(matches) != 1:
        raise ValueError(f"expected one match, found {len(matches)}")
    value = bytes.fromhex(value_text)
    if not value or len(value) > len(signature.split()) or 255 in value:
        raise ValueError("replacement is incompatible with the native parser")
    start = matches[0].start() + section.getint(f"Offset{suffix}", 0)
    end = start + len(value)
    if start < 0 or end > len(data):
        raise ValueError("replacement extends outside the buffer")
    return data[:start] + value + data[end:], matches[0].start(), start


def text_section(path):
    data = path.read_bytes()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[:2] != b"MZ" or data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    machine, count = struct.unpack_from("<HH", data, pe + 4)
    if machine != 0x8664:
        raise ValueError("expected an x64 image")
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    for i in range(count):
        entry = pe + 24 + optional_size + i * 40
        if data[entry:entry + 8].rstrip(b"\0") == b".text":
            size, rva, raw_size, offset = struct.unpack_from("<IIII", data, entry + 8)
            return data[offset:offset + min(size, raw_size)], rva
    raise ValueError("missing .text section")


def numbered(section):
    keys = sorted(int(k) for k in section if k.isdecimal())
    if keys != list(range(1, len(keys) + 1)):
        raise ValueError("numbered entries must be contiguous starting at 1")
    # The loader reserves a slot for the empty entry ending each list.
    if len(keys) >= 10:
        raise ValueError("too many entries for the native buffer list")
    return [section[str(k)] for k in keys]


def validate(config_path, spotify_dir):
    config = configparser.ConfigParser(interpolation=None)
    with config_path.open() as source:
        config.read_file(source)

    data, rva = text_section(spotify_dir / "Spotify.dll")
    _, match, write = apply_patch(data, config["Developer"])
    print(f"Developer: unique match at RVA {rva + match:#x}, write at {rva + write:#x}")

    with zipfile.ZipFile(spotify_dir / "Apps" / "xpui.spa") as archive:
        for filename in numbered(config["Buffer_modify"]):
            data = archive.read(filename)
            original_size = len(data)
            for name in numbered(config[filename]):
                section = config[name]
                if "Signature_1" not in section:
                    raise ValueError(f"{name}: no signature")
                if "Signature_3" in section:
                    raise ValueError(f"{name}: hook supports at most two signatures")
                for index in (1, 2):
                    if f"Signature_{index}" not in section:
                        break
                    try:
                        data, match, _ = apply_patch(data, section, f"_{index}")
                    except ValueError as error:
                        raise ValueError(f"{filename}/{name}/{index}: {error}") from error
                    print(f"{filename}/{name}/{index}: unique match at {match:#x}")
            if len(data) != original_size:
                raise ValueError(f"{filename}: buffer size changed")
            result = subprocess.run(["node", "--check"], input=data, capture_output=True)
            if result.returncode:
                raise ValueError(f"{filename}: {result.stderr.decode()}")
            print(f"{filename}: patched JavaScript parses, size unchanged")

        # Validate the optional CSS patch too. The hook only writes when the
        # matching rule starts at byte zero, so embedded copies are ineligible.
        section = config["Homepage_vbar"]
        regex = pattern(section["Signature"])
        eligible = []
        for filename in archive.namelist():
            if filename.endswith(".css"):
                data = archive.read(filename)
                if regex.match(data):
                    patched, _, write = apply_patch(data, section, capacity=2048)
                    if data[write - 8:write + 4] != b"display:flex":
                        raise ValueError("CSS offset does not target display:flex")
                    if patched[write - 8:write + 4] != b"display:none":
                        raise ValueError("CSS replacement does not produce display:none")
                    eligible.append(filename)
        if len(eligible) != 1:
            raise ValueError(f"expected one eligible CSS file, found {eligible}")
        print(f"Homepage_vbar: valid in {eligible[0]} (Enable={section['Enable']})")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("spotify_dir", type=Path, help="directory containing Spotify.dll and Apps")
    parser.add_argument("--config", type=Path, default=Path(__file__).resolve().parents[1] / "config.ini")
    args = parser.parse_args()
    try:
        validate(args.config, args.spotify_dir)
    except (ValueError, KeyError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Validation failed: {error}\n")
    print("All configured signatures validated offline; runtime hooks still require a live check.")


if __name__ == "__main__":
    main()
