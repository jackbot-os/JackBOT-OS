/* Deterministic offline harness for the real kernel http.c parser functions.
   Includes http.c to reach the static URL/redirect/response parsers.
   Local VM only: stub TCP/DNS, no real network. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../../kernel/http.c"

static int failures = 0;

#define CHECK(cond, name) do { \
    if (cond) { printf("PASS: %s\n", name); } \
    else { printf("FAIL: %s\n", name); failures++; } \
} while (0)

static void resp_fill(struct http_client *c, const char *text)
{
    size_t n = strlen(text);
    c->resp_len = (uint16_t)n;
    memcpy(c->resp, text, n);
}

int main(void)
{
    char host[80], path[140];
    uint16_t port;
    uint32_t ip;
    int r;

    /* ---- parse_abs_url ---- */
    memset(host, 0, sizeof host); memset(path, 0, sizeof path);
    r = parse_abs_url("http://example.com/path", host, sizeof host, path, sizeof path, &port);
    CHECK(r == 1, "http:// scheme parsed as plain http");
    CHECK(strcmp(host, "example.com") == 0, "hostname extracted");
    CHECK(strcmp(path, "/path") == 0, "path extracted");
    CHECK(port == 80, "default http port 80");

    memset(host, 0, sizeof host); memset(path, 0, sizeof path);
    r = parse_abs_url("https://secure.com:8443/x", host, sizeof host, path, sizeof path, &port);
    CHECK(r == 2, "https:// scheme marked secure");
    CHECK(port == 8443, "explicit port 8443 parsed");

    memset(host, 0, sizeof host); memset(path, 0, sizeof path);
    r = parse_abs_url("http://10.0.2.2/", host, sizeof host, path, sizeof path, &port);
    CHECK(r == 1 && strcmp(host, "10.0.2.2") == 0, "literal IP host accepted");

    /* very long host must be truncated inside the host buffer, no overflow */
    char longurl[400];
    memset(longurl, 'x', sizeof longurl - 1);
    longurl[0] = 'h'; longurl[1] = 't'; longurl[2] = 't'; longurl[3] = 'p';
    longurl[4] = ':'; longurl[5] = '/'; longurl[6] = '/';
    longurl[399] = 0;
    memset(host, 0, sizeof host);
    r = parse_abs_url(longurl, host, sizeof host, path, sizeof path, &port);
    CHECK(r != 0 && strlen(host) < sizeof host, "oversized host truncated, no overflow");

    /* ---- literal_ip ---- */
    CHECK(literal_ip("10.0.2.15", &ip) == 1, "literal IPv4 recognized");
    CHECK(literal_ip("256.1.1.1", &ip) == 0, "octet >255 rejected");
    CHECK(literal_ip("abc", &ip) == 0, "alpha host rejected");
    CHECK(literal_ip("1.2.3", &ip) == 0, "three parts rejected");
    CHECK(literal_ip("1.2.3.4.5", &ip) == 0, "five parts rejected");

    /* ---- normalize_path ---- */
    char np[140];
    strcpy(np, "/a/b/../c"); normalize_path(np, sizeof np);
    CHECK(strcmp(np, "/a/c") == 0, "normalize collapses ..");
    strcpy(np, "//a//b//"); normalize_path(np, sizeof np);
    CHECK(strcmp(np, "/a/b") == 0, "normalize collapses duplicate slashes");
    strcpy(np, "/x/./y"); normalize_path(np, sizeof np);
    CHECK(strcmp(np, "/x/y") == 0, "normalize drops current-dir dot");
    strcpy(np, "/../../escape"); normalize_path(np, sizeof np);
    CHECK(np[0] == '/' && strcmp(np, "/escape") == 0, "normalize clamps leading ..");

    /* ---- resolve_location ---- */
    struct http_client *base = &cl[0];
    memset(base, 0, sizeof *base);
    strcpy(base->host, "old.com");
    strcpy(base->path, "/dir/old.html");
    base->port = 80;

    char rh[80], rp[140]; uint16_t rport;
    r = resolve_location("/abs/path", base, rh, sizeof rh, rp, sizeof rp, &rport);
    CHECK(r == 1 && strcmp(rh, "old.com") == 0 && strcmp(rp, "/abs/path") == 0, "absolute path keeps host");

    r = resolve_location("page2.html", base, rh, sizeof rh, rp, sizeof rp, &rport);
    CHECK(r == 1 && strcmp(rp, "/dir/page2.html") == 0, "relative path joins directory");

    r = resolve_location("http://other.com/here", base, rh, sizeof rh, rp, sizeof rp, &rport);
    CHECK(r == 1 && strcmp(rh, "other.com") == 0, "absolute http Location followed");

    r = resolve_location("https://secure.com/x", base, rh, sizeof rh, rp, sizeof rp, &rport);
    CHECK(r == 0, "https Location refused (TLS engine off)");

    /* ---- parse_response ---- */
    struct http_client *c = &cl[1];
    memset(c, 0, sizeof *c);
    resp_fill(c, "HTTP/1.1 200 OK\r\nContent-Length: 11\r\n\r\nhello world");
    parse_response(c);
    CHECK(c->status == 200, "status 200 parsed");
    CHECK(c->content_len == 11, "Content-Length parsed");
    CHECK(c->cl_overflow == 0, "small Content-Length not overflow");

    memset(c, 0, sizeof *c);
    resp_fill(c, "HTTP/1.1 404 Not Found\r\nContent-Length: 999999\r\n\r\nx");
    parse_response(c);
    CHECK(c->status == 404, "status 404 parsed");
    CHECK(c->content_len == -1, "digit > RESPCAP rejected as illegal, length unset");
    CHECK(c->cl_overflow == 0, "illegal oversized length does not flag overflow");

    memset(c, 0, sizeof *c);
    resp_fill(c, "HTTP/1.1 200 OK\r\nContent-Length: 59990\r\n\r\nx");
    parse_response(c);
    CHECK(c->content_len == 59990, "large but parsable length kept");
    CHECK(c->cl_overflow == 1, "header+body beyond response buffer flagged overflow");

    memset(c, 0, sizeof *c);
    resp_fill(c, "HTTP/1.1 200 OK\r\npartial headers no blank line");
    parse_response(c);
    CHECK(c->hdr_len == (int)strlen("HTTP/1.1 200 OK\r\npartial headers no blank line"),
          "missing blank line uses whole buffer as header");

    /* ---- find_header case-insensitive ---- */
    memset(c, 0, sizeof *c);
    resp_fill(c, "HTTP/1.1 200 OK\r\ncontent-type: text/html\r\nContent-Length: 7\r\n\r\nbody");
    parse_response(c);
    char hv[24];
    CHECK(find_header(c, "Content-Type:", hv, sizeof hv) > 0 && strcmp(hv, "text/html") == 0,
          "case-insensitive header value read");

    /* ---- build_request bounds ---- */
    struct http_client *b2 = &cl[0];
    memset(b2, 0, sizeof *b2);
    strcpy(b2->host, "example.com"); strcpy(b2->path, "/index.html");
    char req[600];
    int n = build_request(b2, req);
    CHECK(n > 0 && strncmp(req, "GET /index.html", 15) == 0, "valid request built");

    strcpy(b2->host, "");
    n = build_request(b2, req);
    CHECK(n < 0, "empty host rejected by build_request");

    printf(failures ? "\n=== %d FAILURE(S) ===\n" : "\n=== ALL PASS ===\n", failures);
    return failures ? 1 : 0;
}
