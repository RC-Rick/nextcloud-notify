#!/usr/bin/env python3
"""Определение Depends по ldd + dpkg -S (надёжнее dpkg-shlibdeps вне debian/)."""
import os
import subprocess
import sys

binary = sys.argv[1]
out = subprocess.run(["ldd", binary], capture_output=True, text=True).stdout
libs = set()
for line in out.splitlines():
    for tok in line.split():
        if tok.startswith("/"):
            p = os.path.realpath(tok)
            if os.path.exists(p):
                libs.add(p)

pkgs = set()
for p in sorted(libs):
    r = subprocess.run(["dpkg", "-S", p], capture_output=True, text=True)
    if r.returncode != 0 or not r.stdout.strip():
        continue
    name = r.stdout.splitlines()[0].split(":", 1)[0]
    name = name.split(",")[0].strip().split(":")[0].strip()
    if name:
        pkgs.add(name)
print(", ".join(sorted(pkgs)))
