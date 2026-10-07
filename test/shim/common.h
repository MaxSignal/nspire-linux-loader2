/* Host test shim: printing, and a disk quota for fwrite */
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#define NEWLINE "\n"
#define printl(...) printf(__VA_ARGS__)
#define nio_printf(...) do { if (getenv("SHOW_PROGRESS")) printf(__VA_ARGS__); } while (0)
size_t quota_fwrite(const void *p, size_t size, size_t n, FILE *f);
#define fwrite quota_fwrite
