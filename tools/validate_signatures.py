#!/usr/bin/env python3
"""Validate installed Spotify assets using the same C++ patch engine as the DLL."""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import zipfile

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


def run(engine, *args, check=True):
    result = subprocess.run([str(engine), *map(str, args)], capture_output=True, text=True)
    if check and result.returncode:
        raise ValueError(result.stderr.strip() or "patch engine failed")
    return result


def validate(config, spotify, engine, dump_dir=None):
    info = run(engine, 'inspect', config)
    files = [line.split('\t', 1)[1] for line in info.stdout.splitlines() if line.startswith('FILE\t')]
    print(info.stdout.strip())
    with tempfile.TemporaryDirectory(prefix='bts-validate-') as temp:
        work = Path(temp)
        source, output = work/'input', work/'output'
        native, _ = text_section(spotify/'Spotify.dll')
        source.write_bytes(native)
        print(run(engine, 'apply', config, 'Developer', source, output).stdout.strip())
        with zipfile.ZipFile(spotify/'Apps'/'xpui.spa') as archive:
            for filename in files:
                clean = archive.read(filename)
                source.write_bytes(clean)
                print(run(engine, 'apply', config, filename, source, output).stdout.strip())
                patched = output.read_bytes()
                if len(clean) != len(patched):
                    raise ValueError(f'{filename}: byte length changed')
                check = subprocess.run(['node', '--check'], input=patched, capture_output=True)
                if check.returncode:
                    raise ValueError(f'{filename}: {check.stderr.decode()}')
                print(f'{filename}: JavaScript syntax passed')
                if dump_dir:
                    dump_dir.mkdir(parents=True, exist_ok=True)
                    (dump_dir/filename).write_bytes(clean)
            eligible = []
            for filename in archive.namelist():
                if not filename.endswith('.css'):
                    continue
                clean = archive.read(filename)
                source.write_bytes(clean)
                result = run(engine, 'apply', config, 'Homepage_vbar', source, output, check=False)
                if result.returncode == 0:
                    eligible.append(filename)
                    if len(clean) != len(output.read_bytes()):
                        raise ValueError('CSS byte length changed')
            if len(eligible) != 1:
                raise ValueError(f'Expected one eligible CSS file, found {eligible}')
            print(f'Homepage_vbar: {eligible[0]} validated (including disabled option)')
    print('All signatures validated. Runtime status is reported in blockthespot-status.txt.')


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('spotify_dir', type=Path)
    parser.add_argument('--config', type=Path, default=root/'config.ini')
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--dump-dir', type=Path, help='write clean configured JS files for signature maintenance')
    args = parser.parse_args()
    engine = args.engine
    if engine is None:
        engine = root/'out'/'tools'/('patch-tool.exe' if os.name == 'nt' else 'patch-tool')
    try:
        if not engine.is_file():
            raise ValueError('Build the shared engine first: tools/build.ps1 on Windows or python3 tools/test.py elsewhere')
        if not shutil.which('node'):
            raise ValueError('Node.js is required for patched JavaScript syntax checks')
        validate(args.config.resolve(), args.spotify_dir, engine.resolve(), args.dump_dir)
    except (ValueError, KeyError, OSError, struct.error, zipfile.BadZipFile) as error:
        parser.exit(1, f'Validation failed: {error}\n')


if __name__ == '__main__':
    main()
