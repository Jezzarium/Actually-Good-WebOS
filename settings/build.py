#!/usr/bin/env python3
"""Packages app/ as a webOS .ipk without LG's SDK.

An ipk is an `ar` archive of, in this order:
  debian-binary   "2.0\\n"
  control.tar.gz  ./control
  data.tar.gz     usr/palm/applications/<id>/...  usr/palm/packages/<id>/packageinfo.json
The member headers mirror what ares-package writes (no trailing "/" on names,
mode 100644, owner 0), and the tarballs are reproducible: sorted entries,
root ownership, one fixed mtime taken from the newest source file.

Usage:
  ./build.py          build/com.lgtvtweaks.settings_<version>_all.ipk
  ./build.py --card   same, but as an ordinary opaque full-screen app (no
                      transparent overlay window), as a fallback if the TV
                      refuses the overlay window type for a developer app
"""
import gzip
import io
import json
import os
import sys
import tarfile

ROOT = os.path.dirname(os.path.abspath(__file__))
APP_DIR = os.path.join(ROOT, 'app')
BUILD_DIR = os.path.join(ROOT, 'build')


def app_files():
    out = []
    for base, dirs, files in os.walk(APP_DIR):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            out.append((os.path.relpath(path, APP_DIR), path))
    return out


def tar_gz(entries, mtime):
    """entries: list of (archive path, bytes or None for a directory)."""
    raw = io.BytesIO()
    with tarfile.open(fileobj=raw, mode='w', format=tarfile.GNU_FORMAT) as tar:
        for name, data in entries:
            info = tarfile.TarInfo(name)
            info.mtime = mtime
            info.uid = info.gid = 0
            info.uname = info.gname = 'root'
            if data is None:
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                tar.addfile(info)
            else:
                info.mode = 0o644
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode='wb', mtime=mtime, compresslevel=9) as gz:
        gz.write(raw.getvalue())
    return out.getvalue()


def ar(members, mtime):
    out = bytearray(b'!<arch>\n')
    for name, data in members:
        header = '%-16s%-12d%-6d%-6d%-8s%-10d`\n' % (name, mtime, 0, 0, '100644', len(data))
        out += header.encode('ascii') + data
        if len(data) % 2:
            out += b'\n'
    return bytes(out)


def main():
    card = '--card' in sys.argv[1:]
    files = app_files()
    contents = {rel: open(path, 'rb').read() for rel, path in files}
    mtime = int(max(os.path.getmtime(path) for _, path in files))

    info = json.loads(contents['appinfo.json'])
    if card:
        info['transparent'] = False
        info.pop('defaultWindowType', None)
        contents['appinfo.json'] = (json.dumps(info, indent=2) + '\n').encode()
    app_id, version = info['id'], info['version']

    app_root = 'usr/palm/applications/%s/' % app_id
    pkg_root = 'usr/palm/packages/%s/' % app_id
    packageinfo = (json.dumps({'id': app_id, 'version': version, 'app': app_id}, indent=2) + '\n').encode()

    dirs = {'usr/', 'usr/palm/', 'usr/palm/applications/', 'usr/palm/packages/', app_root, pkg_root}
    for rel in contents:
        parts = rel.split('/')[:-1]
        for i in range(1, len(parts) + 1):
            dirs.add(app_root + '/'.join(parts[:i]) + '/')
    entries = [(d, None) for d in sorted(dirs)]
    entries += sorted((app_root + rel, data) for rel, data in contents.items())
    entries.append((pkg_root + 'packageinfo.json', packageinfo))

    installed_size = sum(len(data) for _, data in entries if data is not None)
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

    ipk = ar([
        ('debian-binary', b'2.0\n'),
        ('control.tar.gz', tar_gz([('control', control.encode())], mtime)),
        ('data.tar.gz', tar_gz(entries, mtime)),
    ], mtime)

    os.makedirs(BUILD_DIR, exist_ok=True)
    path = os.path.join(BUILD_DIR, '%s_%s_all.ipk' % (app_id, version))
    with open(path, 'wb') as f:
        f.write(ipk)

    code = sum(len(contents[n]) for n in ('index.html', 'style.css', 'app.js'))
    print('%s  (%d bytes, %s window)' % (os.path.relpath(path, ROOT), len(ipk), 'card' if card else 'overlay'))
    print('page code: %d bytes (index.html + style.css + app.js), %d gzipped'
          % (code, len(gzip.compress(contents['index.html'] + contents['style.css'] + contents['app.js'], 9))))


if __name__ == '__main__':
    main()
