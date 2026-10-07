/* Run rootimg() against a directory with a simulated disk quota. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#undef fwrite
void rootimg(char *arg);
const char *rootimg_cmdline(void);

static const char *dir;

/* Free space = quota - size of the files in dir */
static long long used_bytes(void) {
    long long n = 0; struct dirent *e; DIR *d = opendir(dir); char p[512]; struct stat st;
    while ((e = readdir(d))) { snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        if (!stat(p, &st) && S_ISREG(st.st_mode)) n += st.st_size; }
    closedir(d); return n;
}
static long long quota;
size_t quota_fwrite(const void *p, size_t size, size_t n, FILE *f) {
    long long left;
    fflush(f);
    left = quota - used_bytes();
    if ((long long)(size * n) > left) {
        size_t fit = left > 0 ? left / size : 0;
        return fwrite(p, size, fit, f);
    }
    return fwrite(p, size, n, f);
}
int main(int argc, char **argv) {
    dir = argv[1]; quota = atoll(argv[2]);
    char cmd[256]; snprintf(cmd, sizeof cmd, "%s", argv[3]);
    rootimg(cmd);
    printf("cmdline:%s\n", rootimg_cmdline());
    return 0;
}
