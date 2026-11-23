#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check that the SDK matches the core it documents.

    tools/check-header.py --core PATH/TO/core
    tools/check-header.py PATH/TO/core/azmod-mod.h [PATH/TO/core/MOD-MENU-KEYS.md]

--core compares the public header, timestamp helper and menu table in the supplied
core checkout. The positional form takes the header, an optional menu table and
an adjacent timestamp helper. Use --timestamps to override the helper's location.

include/azmod-mod.h must be byte-identical to the core's header: a mod compiled against it
sees exactly the struct layouts the core was built with. With a second argument, the key
table in docs/MOD-MENU-KEYS.md (between the keys:begin and keys:end markers) must also equal
the core's generated table. Exit status: 0 identical, 1 different (the first differing line
is printed), 2 a file cannot be read or the markers are missing.
"""
import argparse
import os
import re
import sys

SDK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SDK_HEADER = os.path.join(SDK, "include", "azmod-mod.h")
SDK_TIMESTAMP = os.path.join(SDK, "include", "azmod-timestamps.h")
SDK_KEYS = os.path.join(SDK, "docs", "MOD-MENU-KEYS.md")
KEYS_BLOCK = re.compile(rb"<!-- keys:begin.*?<!-- keys:end -->", re.S)


def keys_block(path):
    """The generated key table in a Markdown file, or None if the markers are missing."""
    with open(path, "rb") as f:
        m = KEYS_BLOCK.search(f.read())
    return m.group(0) if m else None


def compare(sdk_path, core_path):
    """Return None when the files are identical, else a one-line description."""
    with open(sdk_path, "rb") as f:
        sdk = f.read()
    with open(core_path, "rb") as f:
        core = f.read()
    return compare_bytes(sdk, core)


def compare_bytes(sdk, core):
    if sdk == core:
        return None
    a, b = sdk.split(b"\n"), core.split(b"\n")
    for i, (x, y) in enumerate(zip(a, b), 1):
        if x != y:
            return f"line {i} differs:\n  sdk:  {x.decode(errors='replace')}\n  core: {y.decode(errors='replace')}"
    return f"one file is longer: sdk {len(a)} lines, core {len(b)} lines"


def check_inputs(header, timestamp, keys, label):
    status = 0
    for path, theirs in ((SDK_HEADER, header), (SDK_TIMESTAMP, timestamp)):
        with open(path, "rb") as source:
            diff = compare_bytes(source.read(), theirs)
        name = os.path.relpath(path, SDK)
        if diff:
            print(f"{name} differs from {label}: {diff}")
            status = 1
        else:
            print(f"{name} matches {label}")
    if keys is not None:
        mine, match = keys_block(SDK_KEYS), KEYS_BLOCK.search(keys)
        if mine is None or match is None:
            raise ValueError("keys:begin / keys:end markers missing")
        diff = compare_bytes(mine, match.group(0))
        if diff:
            print(f"docs/MOD-MENU-KEYS.md differs from {label}: {diff}")
            status = 1
        else:
            print(f"docs/MOD-MENU-KEYS.md matches {label}")
    return status


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("header", nargs="?")
    parser.add_argument("keys", nargs="?")
    parser.add_argument("--timestamps", help="timestamp helper for a working-tree comparison")
    parser.add_argument("--core", help="compare every mirror with this core checkout")
    try:
        args = parser.parse_args(argv[1:])
        if args.core:
            if args.header or args.keys or args.timestamps:
                raise ValueError("--core cannot be combined with explicit file paths")
            args.header = os.path.join(args.core, "src", "azmod", "azmod-mod.h")
            args.keys = os.path.join(args.core, "docs", "MOD-MENU-KEYS.md")
        if not args.header:
            raise ValueError("pass --core CHECKOUT or a core header")
        timestamps = args.timestamps or os.path.join(os.path.dirname(args.header), "..", "common", "aztimestamp.h")
        with open(args.header, "rb") as source:
            header = source.read()
        with open(timestamps, "rb") as source:
            timestamp = source.read()
        keys = None
        if args.keys:
            with open(args.keys, "rb") as source:
                keys = source.read()
        return check_inputs(header, timestamp, keys, "core working-tree inputs")
    except (OSError, ValueError) as error:
        print(f"cannot verify core contract: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
