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
-mtune=native"`, with only a single thread running. Both of these compile-time
options were previously the defaults, and remain so.

Locking overhead
----------------

As time goes on, threads should take the global lock less and less. To enable
some lock-debugging code, build with `EXTRAFLAGS=-DLOCK_OVERHEAD_DEBUG`.

For example, after a few minutes (after finding a 421-bit wrong string), we
see:

    Found 'KOXEI9wcSxS' with distance 421
    lock taken 109 times (0.535 locks/sec)
    lock taken 110 times (0.417 locks/sec)
    lock taken 111 times (0.412 locks/sec)
    lock taken 112 times (0.385 locks/sec)
    lock taken 113 times (0.261 locks/sec)
    Found 'Q1kDrQuEOf6' with distance 414
    lock taken 113 times (0.261 locks/sec)

Future work
-----------

Single-threaded overall performance is currently better than 2 or 4 threads.
This is somewhat suspect on a 4-core machine, so it bears future investigation.

Additionally, we should poke into some of the non-default GCC optimizations and
see if they help.
