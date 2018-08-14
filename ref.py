#!/usr/bin/env python2

import sys
import subprocess

"""
Usage:
    ./ref.py ./main -B 1000000 -t 3 -T 31
"""

system = subprocess.check_output

githash = system("git rev-parse HEAD", shell=True).strip()
date = system("date -Ihours", shell=True).strip()

filename = "reference.%s.%s" % (githash, date)

benchargs = sys.argv[1:]

with open(filename, "wb") as fh:
    fh.write(" ".join(benchargs) + "\n")

    system(benchargs) # warm up
    results = system(benchargs)
    fh.write(results)

print "Wrote", filename
