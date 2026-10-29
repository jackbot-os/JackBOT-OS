# Honeypot offline harness (task 337)

Local-only offline test for `kernel/honeypot.c`. Compiles the real module with
stubbed config, timer, serial and net helpers; no real network, no listening
socket, no outbound packet.

## Build and run (Debian build VM, userland gcc)

    gcc -Iinclude -Wall -Wextra -g kernel/honeypot.c \
        tests/honeypot_harness/stub.c tests/honeypot_harness/harness.c \
        -o /tmp/hp_harness
    /tmp/hp_harness

## Cases

1. Module starts disabled; nothing recorded while off.
2. Enabling works; identical unclaimed UDP source+port merges and increments hits.
3. Different destination port recorded as a separate entry.
4. Claimed UDP (handled by a local service) is not recorded.
5. Plain TCP ACK ignored; TCP SYN probe recorded.
6. ICMP echo reply ignored; echo request recorded.
7. Entry table capped at HONEYPOT_MAX_ENTRIES without overflow.
8. Clear empties the table; built-in self-test runs.

## Honest boundary

This module is observation-only: it never opens a listening port, never answers
a probe, never scans or initiates a connection. The kernel network stack is
client-only, so it cannot emulate a service banner or bind a server port.
