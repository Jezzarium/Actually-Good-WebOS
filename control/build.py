#!/usr/bin/env python3
"""Builds the Control Center and packages it as a webOS .ipk (see common/native_build.py).

The package is a "native" app: appinfo.json "main" is bin/control, with
assets/icons beside it (rendered from icons/*.svg by tools/make_icons.sh).

Usage:
  ./build.py          build/com.lgtvtweaks.control_<version>_all.ipk

Environment: WEBOS_TOOLCHAIN (default ~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot)
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, '..', 'common'))
import native_build  # noqa: E402

if __name__ == '__main__':
    native_build.main(ROOT, 'control', sys.argv[1:])
