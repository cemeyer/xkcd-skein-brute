[![Build Status](https://travis-ci.org/cemeyer/xkcd-skein-brute.png?branch=master)](https://travis-ci.org/cemeyer/xkcd-skein-brute)

About
=====

Brute-forcer for http://almamater.xkcd.com/

The problem is basically hashing large swaths of key space to find
near-collisions with a given bitstring. The better the 'near-ness', the higher
your rank. It is computed by taking the bitwise distance from the hash of the
input string to the given bitstring.

An ideal solver would be an implementation of the inverse skein function. Hah.

See main.c.

Compile-time options
--------------------

The build may be configured by employing `EXTRAFLAGS`, `OPTFLAGS`, and
`LIBFLAGS`. For example, to build without CURL:

    make EXTRAFLAGS="-DHAVE_CURL=0" LIBFLAGS=""

Or, to build at a different optimization level:

    make OPTFLAGS=-O2

To use bithacks to calculate XORs instead of GCC intrinsics, use
`-DUSE_BITHACKS=1`. I find that the GCC intrinsics give me approximately two
percent better performance, so the default is off.

Run-time options
----------------

    Usage: ./main [OPTIONS]
    
      -h, --help                This help
      -B, --benchmark=LIMIT     Benchmark LIMIT hashes
      -H, --hash=HASH           Brute-force HASH (1024-bit hex string)
                                (HASH defaults to XKCD 1193)
      -t, --trials=TRIALS       Run TRIALS in benchmark mode
      -T, --threads=THREADS     Use THREADS concurrent workers


Skein block performance
-----------------------

With GCC -O3 on this Core i7 machine, single thread (TurboBoost), the C
implementation yields:

    Trial 0 Skeins/sec: 1425606.47
    Trial 1 Skeins/sec: 1437325.28
    Trial 2 Skeins/sec: 1401826.96
    Trial 3 Skeins/sec: 1411270.93
    Trial 4 Skeins/sec: 1434945.67

With the assembly implementation:

    Trial 0 Skeins/sec: 1784922.82
    Trial 1 Skeins/sec: 1817764.97
    Trial 2 Skeins/sec: 1801454.82
    Trial 3 Skeins/sec: 1813100.52
    Trial 4 Skeins/sec: 1767078.88

Unfortunately, the hashes produced by the assembly implementation seem
incorrect. =(

Brute-force performance
-----------------------

The best performanace profile (hashes per second) I've found on my 4-core Core
i7-2600k is `EXTRAFLAGS="-DUNROLL_FACTOR=10"` and `OPTFLAGS="-O3 -march=native
-mtune=native"`, with 4-8 threads running. Both of these compile-time options
were previously the defaults, and remain so. And the default thread count is
based on the number of cores your OS reports, which for me is 8. Defaults
should be good. Ex:

    ./main --benchmark 1000000 --trials 3 --threads 4
    TRIAL TIME_FLOAT TIME_INT HASHES HASHES_PER_THREAD HASHES_PER_SECOND
    0 0.782700 1 4000000 1000000 5110512.88
    1 0.774080 1 4000000 1000000 5167424.97
    2 0.758298 1 4000000 1000000 5274968.08

The same benchmark (and build options) on a Xeon E3-1240 v3:

    ./main --benchmark 1000000 --trials 3 --threads 4
    TRIAL TIME_FLOAT TIME_INT HASHES HASHES_PER_THREAD HASHES_PER_SECOND
    0 0.632235 1 4000000 1000000 6326756.84
    1 0.614292 1 4000000 1000000 6511562.01
    2 0.630443 1 4000000 1000000 6344740.34

Or about 23% faster just going from a Sandybridge i7 to a similarly clocked
Haswell Xeon.

Future work
-----------

We should poke into some of the non-default GCC optimizations and see if they
help.
