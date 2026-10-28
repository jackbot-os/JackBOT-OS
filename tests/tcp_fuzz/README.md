# TCP inbound malformed-segment test harness (task 324)

Local-only offline test for the real kernel `kernel/tcp.c`. It compiles the
actual tcp.c with stubbed network/timer symbols and feeds crafted and random
malformed TCP segments through `tcp_in()`. No socket, no real network, no
external target, no exploit payload.

## Scope

- Subject under test: `kernel/tcp.c` (TCP inbound path `tcp_in`).
- Stubs: `stub.c` provides `net_ready`, `net_local_ip`, `net_ipv4`,
  `net_fmt_ip`, `net_pseudo_checksum`, `net_send_ip`, `timer_get_ticks`,
  byte-order helpers and no-op serial routines.
- `harness.c`: 14 deterministic boundary cases.
- `fuzz_random.c`: 5000 connections x 8 random segments (40 000 total) built
  with random length (0..1599), random data-offset words (0..15), random
  flag bits and random sequence/ack numbers, run under AddressSanitizer.

## Build and run (Debian build VM, userland gcc)

Run from the repository root so the kernel headers resolve via `-Iinclude`:

    # deterministic cases
    gcc -Iinclude -Wall -Wextra kernel/tcp.c tests/tcp_fuzz/stub.c \
        tests/tcp_fuzz/harness.c -o /tmp/tcp_harness
    /tmp/tcp_harness

    # ASAN random fuzz
    gcc -fsanitize=address -g -Iinclude -Wall -Wextra kernel/tcp.c \
        tests/tcp_fuzz/stub.c tests/tcp_fuzz/fuzz_random.c -o /tmp/tcp_fuzz
    /tmp/tcp_fuzz

## Deterministic cases (harness)

1.  connect allocates a socket in SYN_SENT
2.  socket not connected yet
3.  truncated segment (len < 20) dropped
4.  oversized segment (len > 1500) dropped
5.  data offset beyond segment length dropped
6.  zero data offset dropped
7.  bare SYN (no ACK) rejected; handshake requires SYN|ACK
8.  legitimate SYN|ACK completes the handshake
9.  RST after established marks the socket failed
10. second connection handshake
11. out-of-order segment is not buffered
12. in-order data delivered to the receive queue
13. wrong-sequence FIN does not close the peer
14. correct FIN marks the peer closed

## Result (2026-10-28, Debian VM gcc 14.2.0)

- Deterministic harness: 14/14 PASS.
- ASAN random fuzz: 40 000 segments, no crash, no out-of-bounds access.
- Real-machine regression on jb10live: DHCP bound, SYN|ACK handshake and
  HTTP GET version.json still succeed; desktop, playback and I18N self-tests
  pass; normal path unaffected after the ARP and DHCP guards were added.

## Inbound surfaces reviewed statically (not covered by this harness)

- `net.c` `net_poll` ARP dispatch: frames shorter than `eth_hdr + arp_pkt`
  are dropped before the ARP body is parsed.
- `net.c` `dhcp_handler` / `dhcp_opt`: option walk is bounded by the actual
  received options length; each option's declared length is checked before a
  4-byte mask/gateway/DNS/server-id copy.
- `net.c` `ip_handle`: IP header/ihl/total length bounds checked before
  dispatch; `plen = total - ihl` cannot underflow.
- `net.c` `udp_handle`: only uses IP payload length minus 8; the UDP header
  length field is not trusted; `dlen >= 0` guaranteed.
- `net.c` `icmp_handle`: echoes only ICMP echo request with len >= 8;
  payload lives inside the received frame buffer.
- `http.c` `dns_handler` / `dns_skip`: header length >= 12, question/answer
  counts capped at 100, label and compression-pointer advancement bounded by
  packet length, record rdlen checked before reading.
- `http.c` `parse_response`: requires `HTTP/`, validates a three-digit 1xx-5xx
  status code, `Content-Length` parsed digit-by-digit and capped at RESPCAP,
  `hdr_len + content_len` overflow flagged.

## Honest limitations

- This harness exercises the TCP inbound state machine only. The HTTP
  client state machine, TLS engine and FAT32 paths are covered by their own
  review, code guards and real-machine boot regression; they are not executed
  here under ASAN.
- The kernel has no inbound listening TCP server; external hosts cannot open
  a TCP connection to it, so the reachable inbound attack surface is the reply
  packets of outgoing connections plus ICMP/UDP/DNS replies.
- TLS/HTTPS remains unimplemented (`tls_engine_ready()` returns 0); https
  requests fail by design at the TLS gate.
