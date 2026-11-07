#include "jb.h"

int main(void)
{
    char buf[128];
    FILE *f;
    void *m;
    int pid = getpid();

    printf("[LIBC] hello from libc demo (pid=%d, time=%d)\n", pid, (int)time(0));

    m = malloc(64);
    if (m) {
        strcpy((char *)m, "malloc works");
        printf("[LIBC] malloc: %s\n", (char *)m);
        free(m);
    } else {
        printf("[LIBC] malloc FAILED\n");
    }

    {
        unsigned int st[2];
        if (stat("LC.TXT", st) == 0)
            printf("[LIBC] stat LC.TXT: size=%d dir=%d\n", (int)st[0], (int)st[1]);
        else
            printf("[LIBC] stat LC.TXT failed\n");
    }

    f = fopen("LC.TXT", "r");
    if (!f) {
        printf("[LIBC] fopen(LC.TXT) failed (no file on disk)\n");
        return 0;
    }
    {
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        printf("[LIBC] read %d bytes: %s\n", (int)n, buf);
    }
    fclose(f);
    return 0;
}

