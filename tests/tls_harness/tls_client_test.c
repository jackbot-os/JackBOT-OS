/* Host-side test driver for the real kernel TLS client (kernel/tls.c).
   The TCP/network/timer/serial kernel functions are stubbed over POSIX
   sockets, so the handshake and record layer code under test is exactly the
   code that ships in the kernel. Usage: tls_client_test <ip> <port> <sni> */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <kernel/tls.h>

int main(int argc, char **argv)
{
    if (argc < 3) {
        printf("usage: %s <ip> <port> [sni]\n", argv[0]);
        return 2;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    const char *sni = argc > 3 ? argv[3] : "localhost";

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &sa.sin_addr) != 1) {
        printf("bad ip\n");
        return 2;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) != 0) {
        printf("connect failed: %s\n", strerror(errno));
        return 2;
    }
    printf("[test] tcp connected to %s:%d, starting TLS handshake (sni=%s)\n",
           ip, port, sni);
    if (tls_connect(fd, sni) != 0) {
        printf("[test] TLS CONNECT FAILED (state=%s)\n",
               tls_state_name(tls_state()));
        return 1;
    }
    printf("[test] TLS established\n");
    const char *req = "GET / HTTP/1.0\r\nHost: localhost\r\n\r\n";
    if (tls_send(fd, req, (uint16_t)strlen(req)) < 0) {
        printf("[test] send failed\n");
        return 1;
    }
    int total = 0;
    for (int i = 0; i < 400; i++) {
        char buf[512];
        int n = tls_recv(fd, buf, sizeof buf);
        if (n > 0) {
            total += n;
            if (total <= 120)
                fwrite(buf, 1, (size_t)(n < 120 - (total - n) ? n : 120 - (total - n)), stdout);
        } else if (n < 0) {
            printf("\n[test] recv closed after %d bytes\n", total);
            break;
        }
    }
    printf("\n[test] received %d bytes over TLS\n", total);
    if (total <= 0) {
        printf("[test] TLS DATA FAILED\n");
        return 1;
    }
    printf("[test] TLS OK\n");
    return 0;
}
