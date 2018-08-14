[![Build Status](https://travis-ci.org/cemeyer/xkcd-skein-brute.png?branch=master)](https://travis-ci.org/cemeyer/xkcd-skein-brute)

About
=====

One of the better CPU brute-forcers for https://beatthehash.com/ (née
http://almamater.xkcd.com/, now defunct).

The problem is basically hashing large swaths of key space to find
near-collisions with a given bitstring. The better the 'near-ness', the higher
your rank. It is computed by taking the bitwise distance from the hash of the
input string to the given bitstring.

An ideal solver would be an implementation of the inverse skein function.

Interesting program sources are in `main.c`; everything else is reference
Skein implementation.  One exception is that the Skein1024 Update
function has been modified to allow callers to flush a full block (callers
assume responsibility for guaranteeing there will be more data later).

Compile-time options
--------------------

The build may be configured by employing `EXTRAFLAGS`, `OPTFLAGS`, and
`LIBFLAGS`. For example, to build without CURL:

    make EXTRAFLAGS="-DHAVE_CURL=0" LIBFLAGS=""

Or, to build at a different optimization level:

    make OPTFLAGS=-O2

To use bithacks to calculate XORs instead of x86 intrinsics, use
`-DUSE_BITHACKS=1`. I find that the intrinsics give me approximately two
percent better performance, so the default is off.

Run-time options
----------------

    Usage: ./main [OPTIONS]
    
      -h, --help                This help
      -B, --benchmark=LIMIT     Benchmark LIMIT hashes
      -H, --hash=HASH           Brute-force HASH (1024-bit hex string)
                                (HASH defaults to XKCD 1193)
      -S                        Run self-test (Skein1024 correctness)
      -t, --trials=TRIALS       Run TRIALS in benchmark mode
      -T, --threads=THREADS     Use THREADS concurrent workers

The long form options can only be used on Linux (by default).  If your
libc supports `getopt_long`, you can compile with `-DHAVE_GETOPT_LONG=1`
to override that default.

Brute-force performance
-----------------------

The best performanace profile (hashes per second) I've found on my 2011 4-core
Core i7-2600k is `EXTRAFLAGS="-DUNROLL_FACTOR=10"` and `OPTFLAGS="-O3
-march=native -mtune=native"`, with 4-8 threads running. Both of these
compile-time options were previously the defaults, and remain so.  The
default thread count is based on the number of cores your OS reports.  Defaults
aim to be good. Ex:

    ./main --benchmark 1000000 --trials 3 --threads 4
    TRIAL TIME_FLOAT TIME_INT HASHES HASHES_PER_THREAD HASHES_PER_SECOND
    0 0.782700 1 4000000 1000000 5110512.88
    1 0.774080 1 4000000 1000000 5167424.97
    2 0.758298 1 4000000 1000000 5274968.08

The same benchmark (and build options) on a 2013 Xeon E3-1240 v3:

    ./main --benchmark 1000000 --trials 3 --threads 4
    TRIAL TIME_FLOAT TIME_INT HASHES HASHES_PER_THREAD HASHES_PER_SECOND
    0 0.632235 1 4000000 1000000 6326756.84
    1 0.614292 1 4000000 1000000 6511562.01
    2 0.630443 1 4000000 1000000 6344740.34

Or about 23% faster just going from a 3.4-3.8 GHz 2011 Sandybridge i7 to
a (similarly clocked) 3.4-3.8 GHz 2013 Haswell Xeon.

Compilers and CPUs just keep getting faster and faster!  (To enable web and
application bloat, for the most part.)  Here's my 2017 AMD TR 1950X running
the same benchmark, restricted to the same number of threads:

    ./main -B 1000000 -t 3 -T 4                                                                             ⏎
    TRIAL TIME_FLOAT TIME_INT HASHES HASHES_PER_THREAD HASHES_PER_SECOND
    0 0.511538 1 4000000 1000000 7819548.67
    1 0.469354 0 4000000 1000000 8522352.60
    2 0.469311 0 4000000 1000000 8523127.45

Or another 30% faster than the Haswell Xeon with the (similarly clocked) 3.4-4.0
Ghz 2017 AMD part.

The Skein 1024 assembly block implementation gives another ~30% (AMD TR) to
~50% (Haswell Xeon) additional throughput, and is now used by default.

Future work
-----------

We should poke into some of the non-default GCC optimizations and see if they
help.
