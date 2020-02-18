# -*- encoding: utf-8 -*-
"""Generate macro enumeration for WASI target

This utility generates the full list of compiler macros defined during the WASI
builds.  It writes out the `predefined-macros.txt` and `include-all.c` files
into the directory specificed.

Examples:
    $ python gen-predefined-macros.py --cc clang --sysroot sysroot --output-directory sysroot/share
"""

import argparse
import os
import re
import subprocess
import sys

from subprocess import Popen

def _enumerate_headers(directory):
    headers = set()

    for root, directories, files in os.walk(directory):
        # ignore the bits directories as they are internal implementation
        # details
        directories[:] = [ d for d in directories if not d == 'bits' ]

        # Compute the include relative path, however special case '.' to be
        # ignored as a prefix.
        path = os.path.relpath(root, directory)
        if path == '.':
            path = ''

        # ignore mman.h
        # XXX(compnerd) document why mman.h is being ignored
        for include in filter(lambda file: not file == 'mman.h', files):
            headers.add(os.path.join(path, include))

    return headers

def _enumerate_defines(cc, sysroot, flags, include_all_c):
    include = os.path.join(sysroot, 'include')

    # Collect all the predefined macros ...
    process = Popen([
                        cc,
                        '-D_ALL_SOURCE',

                        # ... except for compiler names to keep these
                        # compiler-idependent
                        '-U__llvm__',
                        '-U__clang__',

                        # ... except for the compiler version macros which we do
                        # not need to track here
                        '-U__clang_major__',
                        '-U__clang_minor__',
                        '-U__clang_patchlevel__',
                        '-U__clang_version__',

                        '-U__GNUC__',
                        '-U__GNUC_MINOR__',
                        '-U__GNUC_MAJOR__',
                        '-U__GNUC_PATCHLEVEL__',

                        '-U__VERSION__',

                        # ... marking the sysroot as a system include path as
                        # clang will include its resource directory include path
                        # earlier producing compiler-specific output
                        '-isystem', include,

                        '-E', include_all_c,
                        '-dM',

                        '-Wno-#warnings',
                    ] + flags,
                    stdout = subprocess.PIPE,
                    stderr = sys.stderr)
    output, error = process.communicate()

    if process.returncode:
        sys.exit(process.returncode)

    defines = output.decode('utf-8').splitlines()

    # filter out
    # - __FLT16_* for now as not all versions of clang have these
    # - __FLOAT128__ as it is not in clang 8.0
    __FLT16_ = re.compile(r'\b__FLT16_')
    __FLOAT128__ = re.compile(r'\b__FLOAT128__\b')

    defines = [
            define for define in defines if not (
                re.search(__FLT16_, define) or
                re.search(__FLOAT128__, define) or
                False
            )
    ]

    # For the __*_ATOMIC_*_LOCK_FREE macros, squash individual compiler names to
    # attempt to keep these files compiler-independent
    defines = [
        re.sub(r'__[A-Z0-9]*_ATOMIC_([A-Z0-9_]*)_LOCK_FREE',
               r'__compiler_ATOMIC_\1_LOCK_FREE', define)
        for define in defines
    ]

    return defines

def main(argv):
    parser = argparse.ArgumentParser('gen-predefined-macros')
    parser.add_argument('--cc', default = 'clang')
    parser.add_argument('--sysroot', required = True)
    parser.add_argument('--output-directory', required = True, dest = 'output')

    args, unparsed = parser.parse_known_args()

    args.output = os.path.normpath(args.output)
    args.sysroot = os.path.normpath(args.sysroot)

    include = os.path.join(args.sysroot, 'include')
    include_all_c = os.path.join(args.output, 'include-all.c')
    predefined_macros_txt = os.path.join(args.output, 'predefined-macros.txt')

    with open(include_all_c, 'w') as source:
        for header in sorted(_enumerate_headers(include)):
            source.write('#include <{}>\n'.format(header))

    with open(predefined_macros_txt, 'w') as macros:
        for define in _enumerate_defines(args.cc, args.sysroot, unparsed,
                                         include_all_c):
            macros.write('{}\n'.format(define))

if __name__ == '__main__':
    main(sys.argv)
