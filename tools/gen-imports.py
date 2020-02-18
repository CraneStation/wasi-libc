# -*- encoding: utf-8 -*-
"""Generate import file for a target

This utility scans a static library to identify the undefined symbols which are
externally visible which it expects to have provided.  This is used to generate
the import file definitions for WASI.

Example:
    $ python gen-imports.py --nm llvm-nm --prefix __wasi_ libc.a
"""

import argparse
import os
import re
import subprocess
import sys

from subprocess import Popen

def main(argv):
    parser = argparse.ArgumentParser('gen-imports')
    parser.add_argument('--nm', default = 'nm')
    parser.add_argument('--prefix', default = '_?_wasi_')
    parser.add_argument('--output', required = True)

    args, unparsed = parser.parse_known_args()

    args.nm = os.path.normpath(args.nm)
    args.output = os.path.normpath(args.output)

    process = Popen([
                        args.nm,
                        '--undefined-only',
                        '--extern-only',
                        '--just-symbol-name',
                     ] + [ os.path.normpath(path) for path in unparsed ],
                    stdout = subprocess.PIPE)
    output, error = process.communicate()

    prefix = re.compile(args.prefix)
    lines = output.decode('utf-8').splitlines()
    symbols = [ line for line in lines if not line.endswith(':') ]

    with open(args.output, 'w') as imports:
        for symbol in sorted(set(symbols)):
            if prefix.match(symbol):
                imports.write('{}\n'.format(symbols))

if __name__ == '__main__':
    main(sys.argv)
