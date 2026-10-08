/*
    TI-NSPIRE Linux In-Place Bootloader
    Linux root filesystem image kept as a file of the TI-Nspire filesystem

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * "rootimg <config file>" makes sure the image file named in the config
 * file exists with the configured size, then has the kernel told where it
 * is (nspire_tifs.path=..., see drivers/mtd/nspire-tifs.c in the kernel).
 *
 * Config file, one "key = value" per line, '#' starts a comment:
 *   image   = /documents/linux/rootfs.img.tns
 *   size    = max       # or a size such as 64M, 512K; default: max
 *   reserve = 2M        # left free for the TI-Nspire OS with "max"
 *   min     = 6M        # what the root filesystem needs; default: 1M
 *   payload = /documents/linux/openwrt.tar.gz.tns   # optional
 *
 * The image is created tagged: every 4 KiB chunk starts with
 * "NSPLXIMG" + le32 index + le32 0, the last one with
 * "NSPLXEND" + le32 number of chunks + le32 1. The kernel checks the tags
 * to make sure it found the file's blocks, and Linux formats the image on
 * its first boot. Only an image Linux has not used yet is grown; to resize
 * a used one, delete it from the TI-Nspire file browser.
 *
 * An image is never smaller than "min": a smaller configured size is
 * raised to it, and without the space for it there is no image, and Linux
 * runs from RAM.
 *
 * A payload (a root filesystem archive too large to go through the RAM as
 * an initrd) is written into a new image, for Linux to unpack on its first
 * boot: after the tag of chunk 0, "NSPLXPAY" + le32 length + le32 0, then
 * the payload in the 4080 bytes after the tag of chunks 1, 2...
 */

#include <os.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "rootimg.h"

#define CHUNK		4096
#define CHUNKS_PER_WRITE 16
#define MIN_CHUNKS	256		/* 1 MiB */
#define TAG_MAGIC	"NSPLXIMG"
#define END_MAGIC	"NSPLXEND"
#define PAYLOAD_MAGIC	"NSPLXPAY"
#define TAG_SIZE	16
#define PAYLOAD_PER_CHUNK (CHUNK - TAG_SIZE)

struct rootimg_config {
    char image[128];
    int max;                    /* as large as the free space allows */
    unsigned long size;         /* bytes, when !max */
    unsigned long reserve;      /* bytes kept free with max */
    unsigned long min;          /* bytes the root filesystem needs */
    char payload[128];          /* written into a new image, or "" */
};

/* The payload: its file, and while create_image() writes it, the stream */
static const char *payload_path;
static FILE *payload_f;
static unsigned long payload_len;

static char rootimg_path[128];

const char *rootimg_cmdline(void) {
    static char arg[160];

    if (!rootimg_path[0])
        return "";
    snprintf(arg, sizeof(arg), " nspire_tifs.path=%s", rootimg_path);
    return arg;
}

static char *trim(char *s) {
    char *e;

    while (*s == ' ' || *s == '\t')
        s++;
    e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n'))
        *--e = '\0';
    return s;
}

/* "64M", "512K", "1G", "1048576" */
static int parse_size(const char *s, unsigned long *out) {
    char *end;
    unsigned long v = strtoul(s, &end, 10);

    if (end == s)
        return -1;
    switch (*end) {
    case 'k': case 'K': v <<= 10; end++; break;
    case 'm': case 'M': v <<= 20; end++; break;
    case 'g': case 'G': v <<= 30; end++; break;
    }
    if (*end)
        return -1;
    *out = v;
    return 0;
}

static int read_config(const char *path, struct rootimg_config *cfg) {
    char line[160];
    FILE *f = fopen(path, "r");

    strcpy(cfg->image, "/documents/linux/rootfs.img.tns");
    cfg->max = 1;
    cfg->size = 0;
    cfg->reserve = 2UL << 20;
    cfg->min = (unsigned long)MIN_CHUNKS * CHUNK;
    cfg->payload[0] = '\0';

    if (!f) {
        printl("Cannot open %s" NEWLINE, path);
        return -1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#'), *eq, *key, *val;

        if (hash)
            *hash = '\0';
        eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = '\0';
        key = trim(line);
        val = trim(eq + 1);
        if (!strcmp(key, "image")) {
            strncpy(cfg->image, val, sizeof(cfg->image) - 1);
            cfg->image[sizeof(cfg->image) - 1] = '\0';
        } else if (!strcmp(key, "size")) {
            if (!strcmp(val, "max")) {
                cfg->max = 1;
            } else if (!parse_size(val, &cfg->size)) {
                cfg->max = 0;
            } else {
                printl("%s: bad size \"%s\"" NEWLINE, path, val);
            }
        } else if (!strcmp(key, "reserve")) {
            if (parse_size(val, &cfg->reserve))
                printl("%s: bad reserve \"%s\"" NEWLINE, path, val);
        } else if (!strcmp(key, "payload")) {
            strncpy(cfg->payload, val, sizeof(cfg->payload) - 1);
            cfg->payload[sizeof(cfg->payload) - 1] = '\0';
        } else if (!strcmp(key, "min")) {
            if (parse_size(val, &cfg->min))
                printl("%s: bad min \"%s\"" NEWLINE, path, val);
        } else {
            printl("%s: unknown setting \"%s\"" NEWLINE, path, key);
        }
    }
    fclose(f);
    return 0;
}

static void put_le32(unsigned char *p, unsigned v) {
    p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static unsigned get_le32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

static void progress(const char *what, unsigned done, unsigned total) {
    unsigned pct = total ? (unsigned long long)done * 100 / total : 100;
    char bar[21];
    unsigned i;

    for (i = 0; i < 20; i++)
        bar[i] = i < pct / 5 ? '#' : '.';
    bar[20] = '\0';
    /* nio only: the UART would get a line per update */
    nio_printf("\r%s [%s] %3u%% %u/%u KiB ", what, bar, pct, done * (CHUNK / 1024),
               total * (CHUNK / 1024));
}

/* Write tagged chunks [from, to) of an image of `total` chunks */
static int write_chunks(FILE *f, unsigned from, unsigned to, unsigned total, const char *what) {
    static unsigned char buf[CHUNK * CHUNKS_PER_WRITE];
    unsigned c = from, last_shown = ~0u;

    memset(buf, 0, sizeof(buf));
    while (c < to) {
        unsigned n = to - c < CHUNKS_PER_WRITE ? to - c : CHUNKS_PER_WRITE, i;

        if (payload_f)
            memset(buf, 0, sizeof(buf));
        for (i = 0; i < n; i++) {
            unsigned char *tag = buf + i * CHUNK;

            if (payload_f && c + i == 0) {
                memcpy(tag + TAG_SIZE, PAYLOAD_MAGIC, 8);
                put_le32(tag + TAG_SIZE + 8, payload_len);
            } else if (payload_f && c + i < total - 1 &&
                       fread(tag + TAG_SIZE, 1, PAYLOAD_PER_CHUNK, payload_f) == 0 &&
                       ferror(payload_f)) {
                return -1;
            }
            if (c + i == total - 1) {
                memcpy(tag, END_MAGIC, 8);
                put_le32(tag + 8, total);
                put_le32(tag + 12, 1);
            } else {
                memcpy(tag, TAG_MAGIC, 8);
                put_le32(tag + 8, c + i);
                put_le32(tag + 12, 0);
            }
        }
        if (fwrite(buf, CHUNK, n, f) != n)
            return -1;
        c += n;
        if ((c - from) * 100ULL / (to - from) != last_shown) {
            last_shown = (c - from) * 100ULL / (to - from);
            progress(what, c, total);
        }
    }
    return 0;
}

static unsigned long file_bytes(const char *path) {
    struct stat st;

    return stat(path, &st) ? 0 : st.st_size;
}

/* Chunks holding the payload, with chunk 0 and the end chunk */
static unsigned payload_chunks(void) {
    return 2 + (payload_len + PAYLOAD_PER_CHUNK - 1) / PAYLOAD_PER_CHUNK;
}

/* Create an image of `chunks` chunks; -1 when the space runs out */
static int create_image(const char *path, unsigned chunks) {
    FILE *f;
    int ret;

    if (payload_path) {
        if (chunks < payload_chunks()) {
            printl("An image of %u KiB cannot hold %s" NEWLINE, chunks * 4, payload_path);
            return -1;
        }
        payload_f = fopen(payload_path, "rb");
        if (!payload_f) {
            printl("Cannot open %s" NEWLINE, payload_path);
            return -1;
        }
    }
    f = fopen(path, "wb");
    if (!f) {
        printl("Cannot create %s" NEWLINE, path);
        ret = -1;
    } else {
        ret = write_chunks(f, 0, chunks, chunks, "Creating");
        if (fclose(f))
            ret = -1;
        nio_printf(NEWLINE);
        if (ret)
            remove(path);
    }
    if (payload_f) {
        fclose(payload_f);
        payload_f = NULL;
    }
    return ret;
}

/* Does the (fresh) image hold the configured payload? */
static int has_payload(const char *path) {
    unsigned char hdr[TAG_SIZE + 16];
    FILE *f = fopen(path, "rb");
    int ok;

    if (!f)
        return 0;
    ok = fread(hdr, sizeof(hdr), 1, f) == 1 &&
         !memcmp(hdr + TAG_SIZE, PAYLOAD_MAGIC, 8) &&
         get_le32(hdr + TAG_SIZE + 8) == payload_len;
    fclose(f);
    return ok;
}

/*
 * How many bytes can still be written. There is no system call telling
 * the free space, so fill a temporary file until it fails, then delete it.
 */
static unsigned long probe_free_space(const char *dir) {
    static unsigned char buf[CHUNK * CHUNKS_PER_WRITE];
    char path[160];
    unsigned long done = 0;
    unsigned step = 0;
    FILE *f;

    snprintf(path, sizeof(path), "%s/.linux-probe.tns", dir);
    f = fopen(path, "wb");
    if (!f)
        return 0;
    memset(buf, 0, sizeof(buf));
    for (;;) {
        size_t n = fwrite(buf, 1, sizeof(buf), f);

        done += n;
        if (n != sizeof(buf))
            break;
        if (!(++step % 16))
            nio_printf("\rMeasuring free space: %lu KiB ", done >> 10);
    }
    fclose(f);
    remove(path);
    nio_printf("\rFree space: %lu KiB             " NEWLINE, done >> 10);
    return done;
}

enum image_state { IMAGE_NONE, IMAGE_FRESH, IMAGE_USED, IMAGE_INVALID };

static enum image_state image_state(const char *path, unsigned *chunks) {
    unsigned char tag[16];
    struct stat st;
    enum image_state state = IMAGE_INVALID;
    FILE *f;

    if (stat(path, &st))
        return IMAGE_NONE;
    *chunks = st.st_size / CHUNK;
    if (st.st_size % CHUNK || *chunks < 2)
        return IMAGE_INVALID;

    f = fopen(path, "rb");
    if (!f)
        return IMAGE_INVALID;
    if (fseek(f, (long)(*chunks - 1) * CHUNK, SEEK_SET) == 0 &&
        fread(tag, sizeof(tag), 1, f) == 1 && !memcmp(tag, END_MAGIC, 8) &&
        get_le32(tag + 8) == *chunks) {
        state = IMAGE_USED;
        if (fseek(f, 0, SEEK_SET) == 0 && fread(tag, sizeof(tag), 1, f) == 1 &&
            !memcmp(tag, TAG_MAGIC, 8) && get_le32(tag + 8) == 0 &&
            fseek(f, CHUNK, SEEK_SET) == 0 && fread(tag, sizeof(tag), 1, f) == 1 &&
            !memcmp(tag, TAG_MAGIC, 8) && get_le32(tag + 8) == 1)
            state = IMAGE_FRESH;
    }
    fclose(f);
    return state;
}

/* Grow a fresh image: rewrite its end chunk as a normal one and go on */
static int grow_image(const char *path, unsigned from, unsigned to) {
    FILE *f = fopen(path, "rb+");
    int ret = -1;

    if (!f)
        return -1;
    if (!fseek(f, (long)(from - 1) * CHUNK, SEEK_SET))
        ret = write_chunks(f, from - 1, to, to, "Growing");
    if (fclose(f))
        ret = -1;
    nio_printf(NEWLINE);
    return ret;
}

static int make_max(const char *path, unsigned long reserve, unsigned min_chunks) {
    char dir[128];
    char *slash;
    unsigned long free_bytes;
    unsigned chunks;

    strncpy(dir, path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    slash = strrchr(dir, '/');
    if (slash)
        *slash = '\0';

    free_bytes = probe_free_space(dir);
    /* a little slack for the file's own filesystem metadata */
    chunks = free_bytes > reserve ? (free_bytes - reserve) / CHUNK : 0;
    chunks -= chunks ? chunks / 64 + 1 : 0;
    if (chunks < min_chunks) {
        printl("Not enough free space for the Linux image: %lu KiB free,"
               " %u KiB needed, %lu KiB kept for the TI-Nspire OS" NEWLINE,
               free_bytes >> 10, (min_chunks - 1) * 4, reserve >> 10);
        return -1;
    }
    if (create_image(path, chunks)) {
        printl("Creating the image failed" NEWLINE);
        return -1;
    }
    return 0;
}

void rootimg(char *arg) {
    struct rootimg_config cfg;
    unsigned chunks = 0, want, min_chunks;
    enum image_state state;
    char *cfgpath = trim(arg);

    if (!*cfgpath) {
        printl("Usage: rootimg <config file>" NEWLINE);
        return;
    }
    if (read_config(cfgpath, &cfg))
        return;
    payload_path = NULL;
    if (cfg.payload[0]) {
        payload_len = file_bytes(cfg.payload);
        if (!payload_len) {
            printl("Cannot find %s" NEWLINE, cfg.payload);
            goto none;
        }
        payload_path = cfg.payload;
    }
    /* Linux uses all the chunks but the last one */
    min_chunks = (cfg.min + CHUNK - 1) / CHUNK + 1;
    if (min_chunks < MIN_CHUNKS)
        min_chunks = MIN_CHUNKS;
    want = cfg.max ? 0 : cfg.size / CHUNK;
    if (!cfg.max && cfg.size < cfg.min)
        printl("The configured size is less than the %lu KiB Linux needs, using that" NEWLINE,
               cfg.min >> 10);
    if (!cfg.max && want < min_chunks)
        want = min_chunks;

    state = image_state(cfg.image, &chunks);
    if (state == IMAGE_FRESH && payload_path && !has_payload(cfg.image)) {
        printl("%s is unused but does not hold %s, recreating it" NEWLINE,
               cfg.image, payload_path);
        remove(cfg.image);
        state = IMAGE_NONE;
    }
    if (state == IMAGE_INVALID) {
        printl("%s is not a Linux image (an interrupted creation?), recreating it" NEWLINE,
               cfg.image);
        remove(cfg.image);
        state = IMAGE_NONE;
    }

    if (state == IMAGE_NONE) {
        printl("Creating the Linux image %s (%s)" NEWLINE, cfg.image,
               cfg.max ? "as large as possible" : "configured size");
        if (cfg.max || create_image(cfg.image, want)) {
            if (!cfg.max)
                printl("Not enough space for %u KiB, using the free space instead" NEWLINE,
                       (want - 1) * 4);
            if (make_max(cfg.image, cfg.reserve, min_chunks))
                goto none;
        }
    } else if (cfg.max && chunks < min_chunks) {
        /* An image made for a smaller root filesystem */
        if (state == IMAGE_FRESH) {
            printl("Growing %s to the %u KiB Linux needs" NEWLINE, cfg.image,
                   (min_chunks - 1) * 4);
            if (grow_image(cfg.image, chunks, min_chunks)) {
                printl("Not enough space, recreating it as large as possible" NEWLINE);
                remove(cfg.image);
                if (make_max(cfg.image, cfg.reserve, min_chunks))
                    goto none;
            }
        } else {
            printl("%s is too small for this system (%u KiB, %u KiB needed); delete it"
                   " from the TI-Nspire file browser and boot again" NEWLINE,
                   cfg.image, (chunks - 1) * 4, (min_chunks - 1) * 4);
        }
    } else if (!cfg.max && want > chunks) {
        if (state == IMAGE_FRESH) {
            printl("Growing %s to %u KiB" NEWLINE, cfg.image, (want - 1) * 4);
            if (grow_image(cfg.image, chunks, want)) {
                printl("Not enough space, recreating it as large as possible" NEWLINE);
                remove(cfg.image);
                if (make_max(cfg.image, cfg.reserve, min_chunks))
                    goto none;
            }
        } else {
            printl("%s is in use with %u KiB; to resize it, delete it from the"
                   " TI-Nspire file browser and boot again" NEWLINE, cfg.image, chunks * 4);
        }
    }

    state = image_state(cfg.image, &chunks);
    if (state != IMAGE_FRESH && state != IMAGE_USED)
        goto none;
    strncpy(rootimg_path, cfg.image, sizeof(rootimg_path) - 1);
    rootimg_path[sizeof(rootimg_path) - 1] = '\0';
    printl("Linux image: %s, %u KiB%s" NEWLINE, cfg.image, (chunks - 1) * 4,
           state == IMAGE_FRESH ? " (formatted on first boot)" : "");
    return;

none:
    printl("No Linux image: Linux runs from RAM, and forgets what it writes" NEWLINE);
}
