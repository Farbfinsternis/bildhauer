#!/bin/sh
# Prints a function without the repetitive debug/assert/profiler boilerplate.
node "$(dirname "$0")/fn.js" "$(dirname "$0")/../_work/sculptris.ann.asm" "$@" | grep -v 'memset\|4ECC00h\|4D4410h\|4C2068h\|push        100h\|push        0$\|004FD2E3h\|004FD2ECh\|004FD2F0h\|004FD2EDh\|mov         eax,1F1h\|004FD384h\|004FD2F8h\|004FD359h\|004B086Ch\|004FD374h\|004D4010h\|004B0870h\|004FD1EDh\|MessageBoxA\|"Debug"\|push        31h\|00455CE0\|fs:\[00000000h\]\|004B0000h'
