#!/usr/bin/env python3
"""Import captured GXM shader binaries into a native PSARC payload tree."""
import argparse
import json
from pathlib import Path
from native_assets.shader_cache import import_cache

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True,
                        help='Captured program_cache/gxm-cg-gxp-v1 directory')
    parser.add_argument('--payload', type=Path, required=True,
                        help='PSARC payload root containing disc/ and native/')
    args = parser.parse_args()
    print(json.dumps(import_cache(args.source, args.payload), indent=2))

if __name__ == '__main__':
    main()
