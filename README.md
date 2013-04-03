About
=====

Brute-forcer for http://almamater.xkcd.com/

The problem is basically hashing large swaths of key space to find
near-collisions with a given bitstring. The better the 'near-ness', the higher
your rank. It is computed by taking the bitwise distance from the hash of the
input string to the given bitstring.

An ideal solver would be an implementation of the inverse skein function. Hah.

See main.c.

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

Locking overhead
----------------

As time goes on, threads should take the global lock less and less. To enable
some lock-debugging code, build with `-DLOCK_OVERHEAD_DEBUG`.
