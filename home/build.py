#!/usr/bin/env python3
"""Builds the home screen (C++, SDL2, OpenGL ES 2) and packages it as a webOS .ipk.

The TV's userspace is 32-bit ARM (armhf). The binary is cross-compiled with
the homebrew webOS toolchain, buildroot-nc4 (github.com/openlgtv/buildroot-nc4,
arm-webos-linux-gnueabi_sdk-buildroot), and links against the TV's own
libSDL2 (LG's webOS port, 2.0.14) and libGLESv2, as Moonlight does. Only
SDL 2.0.14-era API may be used.

The package is a "native" app: appinfo.json "main" is bin/home, with assets/
(wallpapers) beside it. The ipk
layout and archive format are shared with settings/build.py.

Usage:
  ./build.py          build/com.lgtvtweaks.home_<version>_all.ipk
  ./build.py --host   desktop build for this machine (build/host/home)

Environment: WEBOS_TOOLCHAIN (default ~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot)
"""
import gzip
import importlib.util
import io
import json
import os
import subprocess
import sys
import tarfile

ROOT = os.path.dirname(os.path.abspath(__file__))
TV_DIR = os.path.join(ROOT, 'tv')
ASSETS_DIR = os.path.join(ROOT, 'assets')
BUILD_DIR = os.path.join(ROOT, 'build')
TOOLCHAIN = os.environ.get('WEBOS_TOOLCHAIN',
                           os.path.expanduser('~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot'))

# Reuse the ar writer from the Quick Settings build.
_spec = importlib.util.spec_from_file_location('settings_build', os.path.join(ROOT, '..', 'settings', 'build.py'))
ipk = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ipk)


def run(*cmd):
    print('+', ' '.join(cmd))
    subprocess.run(cmd, cwd=ROOT, check=True)


def compile_binary(host):
    out = os.path.join(BUILD_DIR, 'host' if host else 'tv')
    configure = ['cmake', '-S', ROOT, '-B', out, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release']
    if not host:
        configure.append('-DCMAKE_TOOLCHAIN_FILE=' + os.path.join(TOOLCHAIN, 'share', 'buildroot', 'toolchainfile.cmake'))
    run(*configure)
    run('cmake', '--build', out)
    binary = os.path.join(out, 'home')
    if not host:
        run(os.path.join(TOOLCHAIN, 'bin', 'arm-webos-linux-gnueabi-strip'), binary)
    return binary


def tar_gz(entries, mtime):
    """entries: list of (archive path, bytes or None for a directory, file mode)."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w', format=tarfile.GNU_FORMAT) as tar:
        for name, data, mode in entries:
            info = tarfile.TarInfo(name)
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            info.mode = mode
            if data is None:
                info.type = tarfile.DIRTYPE
                tar.addfile(info)
            else:
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode='wb', mtime=mtime, compresslevel=9) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def package(binary):
    contents = {}  # path in app dir -> (bytes, mode)
    for name in sorted(os.listdir(TV_DIR)):
        contents[name] = (open(os.path.join(TV_DIR, name), 'rb').read(), 0o644)
    contents['bin/home'] = (open(binary, 'rb').read(), 0o755)
    for base, dirs, files in os.walk(ASSETS_DIR):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            contents['assets/' + os.path.relpath(path, ASSETS_DIR)] = (open(path, 'rb').read(), 0o644)
    mtime = int(os.path.getmtime(binary))

    info = json.loads(contents['appinfo.json'][0])
    app_id, version = info['id'], info['version']
    app_root = 'usr/palm/applications/%s/' % app_id
    pkg_root = 'usr/palm/packages/%s/' % app_id
    packageinfo = (json.dumps({'id': app_id, 'version': version, 'app': app_id}, indent=2) + '\n').encode()

    dirs = {'usr/', 'usr/palm/', 'usr/palm/applications/', 'usr/palm/packages/', app_root, pkg_root}
    for rel in contents:
        parts = rel.split('/')[:-1]
        for i in range(1, len(parts) + 1):
            dirs.add(app_root + '/'.join(parts[:i]) + '/')
    entries = [(d, None, 0o755) for d in sorted(dirs)]
    entries += sorted((app_root + rel, data, mode) for rel, (data, mode) in contents.items())
    entries.append((pkg_root + 'packageinfo.json', packageinfo, 0o644))

    installed_size = sum(len(data) for _, data, _ in entries if data is not None)
    control = (
        'Package: %s\n'
        'Version: %s\n'
        'Section: misc\n'
        'Priority: optional\n'
        'Architecture: all\n'
        'Installed-Size: %d\n'
        'Maintainer: N/A <nobody@example.com>\n'
        'Description: This is a webOS application.\n'
        'webOS-Package-Format-Version: 2\n'
        'webOS-Packager-Version: x.y.x\n'
    ) % (app_id, version, installed_size)

    data = ipk.ar([
        ('debian-binary', b'2.0\n'),
        ('control.tar.gz', tar_gz([('control', control.encode(), 0o644)], mtime)),
        ('data.tar.gz', tar_gz(entries, mtime)),
    ], mtime)

    path = os.path.join(BUILD_DIR, '%s_%s_all.ipk' % (app_id, version))
    with open(path, 'wb') as f:
        f.write(data)
    print('%s  (%d bytes; bin/home %d bytes)' % (os.path.relpath(path, ROOT), len(data), len(contents['bin/home'][0])))


def main():
    host = '--host' in sys.argv[1:]
    binary = compile_binary(host)
    if host:
        print(os.path.relpath(binary, ROOT))
    else:
        package(binary)


if __name__ == '__main__':
    main()
