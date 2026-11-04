"""Builds a native app (C++, SDL2, OpenGL ES 2) and packages it as a webOS .ipk.

Shared by home/build.py and control/build.py. An app folder holds
CMakeLists.txt, tv/ (appinfo.json and icons, packaged as they are) and
optionally assets/ (packaged under assets/). The binary goes where
appinfo.json's "main" says.

The TV's userspace is 32-bit ARM (armhf). The binary is cross-compiled with
the homebrew webOS toolchain, buildroot-nc4 (github.com/openlgtv/buildroot-nc4,
arm-webos-linux-gnueabi_sdk-buildroot), and links against the TV's own
libSDL2 (LG's webOS port, 2.0.14) and libGLESv2, as Moonlight does. Only
SDL 2.0.14-era API may be used.

Environment: WEBOS_TOOLCHAIN (default ~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot)
"""
import gzip
import io
import json
import os
import subprocess
import tarfile

TOOLCHAIN = os.environ.get('WEBOS_TOOLCHAIN',
                           os.path.expanduser('~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot'))


def run(root, *cmd):
    print('+', ' '.join(cmd))
    subprocess.run(cmd, cwd=root, check=True)


def compile_binary(root, target, host):
    out = os.path.join(root, 'build', 'host' if host else 'tv')
    configure = ['cmake', '-S', root, '-B', out, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release']
    if not host:
        configure.append('-DCMAKE_TOOLCHAIN_FILE=' + os.path.join(TOOLCHAIN, 'share', 'buildroot', 'toolchainfile.cmake'))
    run(root, *configure)
    run(root, 'cmake', '--build', out)
    binary = os.path.join(out, target)
    if not host:
        run(root, os.path.join(TOOLCHAIN, 'bin', 'arm-webos-linux-gnueabi-strip'), binary)
    return binary


def ar(members, mtime):
    out = bytearray(b'!<arch>\n')
    for name, data in members:
        header = '%-16s%-12d%-6d%-6d%-8s%-10d`\n' % (name, mtime, 0, 0, '100644', len(data))
        out += header.encode('ascii') + data
        if len(data) % 2:
            out += b'\n'
    return bytes(out)


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


def package(root, binary):
    tv_dir, assets_dir = os.path.join(root, 'tv'), os.path.join(root, 'assets')
    contents = {}  # path in app dir -> (bytes, mode)
    for name in sorted(os.listdir(tv_dir)):
        contents[name] = (open(os.path.join(tv_dir, name), 'rb').read(), 0o644)
    info = json.loads(contents['appinfo.json'][0])
    main = info['main']
    contents[main] = (open(binary, 'rb').read(), 0o755)
    for base, dirs, files in os.walk(assets_dir):
        dirs.sort()
        for name in sorted(files):
            path = os.path.join(base, name)
            contents['assets/' + os.path.relpath(path, assets_dir)] = (open(path, 'rb').read(), 0o644)
    mtime = int(os.path.getmtime(binary))

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

    data = ar([
        ('debian-binary', b'2.0\n'),
        ('control.tar.gz', tar_gz([('control', control.encode(), 0o644)], mtime)),
        ('data.tar.gz', tar_gz(entries, mtime)),
    ], mtime)

    path = os.path.join(root, 'build', '%s_%s_all.ipk' % (app_id, version))
    with open(path, 'wb') as f:
        f.write(data)
    print('%s  (%d bytes; %s %d bytes)' % (os.path.relpath(path, root), len(data), main, len(contents[main][0])))


def main(root, target, argv):
    host = '--host' in argv
    binary = compile_binary(root, target, host)
    if host:
        print(os.path.relpath(binary, root))
    else:
        package(root, binary)
