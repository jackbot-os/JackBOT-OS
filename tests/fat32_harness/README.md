# FAT32 offline memory-disk test harness (task 332 / 333)

Local-only offline test for the real kernel `kernel/fat32.c`. A flat in-memory
array stands in for the ATA block device; the real `fat32.c`, `protect.c` and
`persist.c` are compiled unchanged. No real disk, no network, no exploit payload.

## Scope

- Subject under test: `kernel/fat32.c` (mount, read, write, overwrite, delete,
  mkdir, subdirectory, listing, move, LFN long names, unmount guards).
- Stubs: `stub.c` provides an in-memory disk with boundary-checked
  `ata_read` / `ata_write` / `ata_present` / `ata_flush_cache`, libc-backed
  kernel heap, stdout serial, `timer_get_ticks`.
- `harness.c`: 44 CHECK assertions covering 22 grouped boundary scenarios.
- `fuzz_random.c`: 200 mount rounds x 300 random operations (create / overwrite
  / read-back compare / delete / mkdir / subdirectory write), run under
  AddressSanitizer.

## Build and run (Debian build VM, userland gcc)

Run from the repository root so headers resolve via `-Iinclude`:

    # deterministic cases
    gcc -Iinclude -Wall -Wextra -g \
        kernel/fat32.c kernel/protect.c kernel/persist.c \
        tests/fat32_harness/stub.c tests/fat32_harness/harness.c \
        -o /tmp/fat_harness
    /tmp/fat_harness

    # ASAN random fuzz
    gcc -fsanitize=address -g -Iinclude -Wall -Wextra \
        kernel/fat32.c kernel/protect.c kernel/persist.c \
        tests/fat32_harness/stub.c tests/fat32_harness/fuzz_random.c \
        -o /tmp/fat_fuzz
    /tmp/fat_fuzz

## Deterministic cases (harness)

1.  mount a formatted disk; mounted flag set
2.  total / free byte counts are sane (0 < free <= total)
3.  small file write then read-back matches
4.  file exists after write
5.  overwrite round trip matches
6.  overwrite updates stored size
7.  multi-cluster file (> cluster size) round trip matches
8.  mkdir succeeds; directory recognized
9.  file written inside a subdirectory round trips
10. root listing contains the subdirectory and file
11. subdirectory listing contains its file
12. delete succeeds; entry gone; free space recovered
13. reading a missing file fails cleanly
14. zero-byte file writes; size is 0; read returns length 0
15. overlong path is rejected without crash
16. move keeps source gone and destination content intact
17. core file write blocked for USER, allowed for ADMIN
18. LFN long-name write in a subdirectory; listing restores the long name
19. USER blocked from LFN write of a core file
20. after unmount, exists / read / write / delete all return errors, no crash
21. tiny disk: accepted some files before full; free bytes stay sane
22. failed writes on a full disk do not leak clusters

## Result (2026-10-29, Debian VM gcc 14.2.0)

- Deterministic harness: 43/43 PASS (44 CHECK assertions, one bare status call).
- ASAN random fuzz: 200 rounds, 60 000 operations, 14 996 file writes,
  no crash, no out-of-bounds access.
- Real kernel rebuild on the same VM: `kernel/fat32.c` recompiled clean,
  NO_ERROR; only the pre-existing `ld: LOAD segment RWX permissions` warning
  remains (unchanged from earlier releases).

## Fixes applied in this pass (task 333)

1. `alloc_chain`: when a cluster allocation fails mid-chain, the already
   allocated chain is now freed back to the FAT instead of becoming orphaned
   space that leaks permanently.
2. Overwrite path: an existing file now reuses its own directory slot; the old
   data chain is released before the new chain is allocated, so a stale entry
   can no longer shadow the new file after overwrite.
3. `fat_read_file`: stops traversing remaining clusters as soon as the caller
   buffer limit is reached, instead of reading unused clusters.
4. `resolve_path`: paths at or above `MAX_PATH` are rejected instead of being
   silently truncated.
5. `mount_common`: added geometry guards - cluster size must be a power of two,
   number of FATs bounded, reserved/FAT/data layout checked with 64-bit
   arithmetic, cluster count capped by FAT entry capacity, root cluster
   validated; kernel buffers freed on allocation failure.
6. `fat_write_file_lfn`: now runs the core-file write permission check, and
   `dir_find_run` walks the whole directory cluster chain instead of scanning
   only the first cluster.

## Honest limitations (not covered here)

- Deleting a non-empty directory does not recursively free files inside it;
  the current public delete path is used for files. Recursive directory
  cleanup remains a future item.
- The LFN writer does not yet verify every per-cluster write result on a
  failing block device; the stub device never fails writes.
- This harness exercises the FAT32 layer only. ATA/IDE and AHCI/SATA command
  paths, the Python interpreter, JCA path resolution, network, TLS and audio
  are covered by their own modules and are not executed here under ASAN.
- No real-machine write-back regression with an attached disk was performed in
  this pass; the live desktop boot in the serial log has no disk mounted, so
  the FAT self-test branch does not run there.
