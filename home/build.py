#!/usr/bin/env python3
"""Builds the home screen and packages it as a webOS .ipk (see common/native_build.py).

The package is a "native" app: appinfo.json "main" is bin/home, with assets/
(wallpapers, icons) beside it.

Usage:
  ./build.py          build/com.lgtvtweaks.home_<version>_all.ipk
  ./build.py --host   desktop build for this machine (build/host/home)

Environment: WEBOS_TOOLCHAIN (default ~/webos-toolchain/arm-webos-linux-gnueabi_sdk-buildroot)
"""
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, '..', 'common'))
import native_build  # noqa: E402

if __name__ == '__main__':
    native_build.main(ROOT, 'home', sys.argv[1:])
