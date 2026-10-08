/*
    TI-NSPIRE Linux In-Place Bootloader

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
 * The initrd goes at the end of the kernel's block when it fits there, as
 * it always did. Otherwise, as it may be larger than any block the OS heap
 * gives, it is loaded into as many pieces as needed, and at boot, once
 * nothing of the OS runs any more, gathered into the highest free part of
 * the RAM, out of the way of the kernel decompressing itself there.
 */

#include <os.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "common.h"
#include "initrd.h"
#include "memory.h"

#define MAX_PIECES	128
#define MIN_PIECE	0x10000

struct range {
    unsigned start, end;
};

static struct {
    void *addr;
    size_t size;
} pieces[MAX_PIECES];
static int npieces;
static int in_place;            /* at the end of the kernel's block */

void initrd_free(void) {
    if (in_place)
        npieces = 0;
    in_place = 0;
    while (npieces)
        free(pieces[--npieces].addr);
    settings.initrd.addr = NULL;
    settings.initrd.size = 0;
    settings.initrd_loaded = 0;
}

/* The largest block the heap gives, up to want bytes */
static void *alloc_piece(size_t want, size_t *got) {
    size_t size = want;
    void *p;

    for (;;) {
        p = malloc(size);
        if (p) {
            *got = size;
            return p;
        }
        if (size <= MIN_PIECE)
            return NULL;
        size = (size / 2 + MIN_PIECE - 1) & ~(MIN_PIECE - 1);
    }
}

int initrd_in_kernel_block(void) {
    return in_place;
}

int initrd_load(const char *filename) {
    struct stat st;
    size_t left;
    FILE *f;

    initrd_free();
    if (stat(filename, &st) || !st.st_size) {
        printl("Initrd doesn't exist or empty" NEWLINE);
        return -1;
    }
    f = fopen(filename, "rb");
    if (!f) {
        printl("Failed to open initrd image %s" NEWLINE, filename);
        return -1;
    }

    /* At the end of the kernel's block, when it fits */
    if (settings.mem_block.size - settings.kernel.size >= st.st_size + PAGE_SIZE) {
        char *end = (char *)settings.mem_block.start + settings.mem_block.size;
        char *addr = ROUND_PAGE_BOUND(end - st.st_size);

        if (fread(addr, 1, st.st_size, f) != (size_t)st.st_size) {
            printl("Failed to read %s" NEWLINE, filename);
            fclose(f);
            return -1;
        }
        fclose(f);
        pieces[0].addr = addr;
        pieces[0].size = st.st_size;
        npieces = 1;
        in_place = 1;
        settings.initrd.addr = addr;
        settings.initrd.size = st.st_size;
        settings.initrd_loaded = 1;
        printl("Initrd successfully loaded" NEWLINE);
        return 0;
    }

    /* Leave the rest of the kernel's block to the pieces */
    if (settings.kernel_loaded)
        mem_block_shrink(settings.kernel.size);
    for (left = st.st_size; left; ) {
        size_t got, n;
        void *p;

        if (npieces == MAX_PIECES || !(p = alloc_piece(left, &got))) {
            printl("Not enough memory for the initrd: %u of %u bytes loaded" NEWLINE,
                   (unsigned)(st.st_size - left), (unsigned)st.st_size);
            goto fail;
        }
        if (got > left)
            got = left;
        pieces[npieces].addr = p;
        pieces[npieces++].size = got;
        n = fread(p, 1, got, f);
        if (n != got) {
            printl("Failed to read %s" NEWLINE, filename);
            goto fail;
        }
        left -= got;
    }
    fclose(f);

    settings.initrd.size = st.st_size;
    settings.initrd_loaded = 1;
    printl("Initrd successfully loaded (%u bytes in %d pieces)" NEWLINE,
           (unsigned)st.st_size, npieces);
    return 0;

fail:
    fclose(f);
    initrd_free();
    return -1;
}

static unsigned le32(const unsigned char *p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (unsigned)p[3] << 24;
}

/*
 * Bytes the kernel takes at the start of the RAM: its decompressed image and
 * BSS, from the zImage's KLSZ table, then the zImage itself when it has to
 * move out of the way first. Without the table, 4 times the zImage.
 */
static unsigned kernel_extent(void) {
    const unsigned char *z = settings.kernel.addr;
    unsigned zsize = settings.kernel.size, table, i;

    if (zsize > 0x40 && le32(z + 0x24) == 0x016f2818 && le32(z + 0x34) == 0x45454545) {
        table = le32(z + 0x38);
        for (i = table; i + 16 <= zsize && le32(z + i); i += 4 * (le32(z + i) & 0xff)) {
            if (le32(z + i + 4) == 0x5a534c4b) {        /* "KLSZ" */
                unsigned size_at = le32(z + i + 8), bss = le32(z + i + 12);

                if (size_at + 4 <= zsize)
                    return 0x8000 + le32(z + size_at) + bss + zsize + 0x10000;
            }
        }
    }
    return 0x8000 + 4 * zsize + zsize;
}

static int overlaps(unsigned start, unsigned end, const struct range *r) {
    return start < r->end && r->start < end;
}

/*
 * Choose where the initrd goes: the highest page aligned range of the RAM
 * that is above what the kernel uses to decompress itself and overlaps
 * nothing still needed until the kernel runs.
 */
int initrd_place(void) {
    struct range busy[MAX_PIECES + 4];
    unsigned ram = (unsigned)settings.phys.start;
    unsigned ram_end = ram + settings.phys.size;
    unsigned size = (settings.initrd.size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    unsigned floor, dst, sp;
    int nbusy = 0, i, moved;

    if (!settings.initrd_loaded || in_place)
        return 0;

    /* The kernel decompresses itself at the start of the RAM */
    floor = ram + kernel_extent() + 0x100000;

    for (i = 0; i < npieces; i++) {
        busy[nbusy].start = (unsigned)pieces[i].addr;
        busy[nbusy++].end = (unsigned)pieces[i].addr + pieces[i].size;
    }
    /* The kernel runs where it was loaded */
    busy[nbusy].start = (unsigned)settings.kernel.addr;
    busy[nbusy++].end = (unsigned)settings.kernel.addr + settings.kernel.size;
    /* Boot parameters, copied at the very end */
    busy[nbusy].start = (unsigned)settings.boot_param.start;
    busy[nbusy++].end = (unsigned)settings.boot_param.start + settings.boot_param.size;
    /* This program, and its stack */
    busy[nbusy].start = ((unsigned)&initrd_place & ~0xfffff) - 0x100000;
    busy[nbusy++].end = ((unsigned)&initrd_place | 0xfffff) + 0x100001;
    asm volatile("mov %0, sp" : "=r"(sp));
    busy[nbusy].start = (sp & ~0xffff) - 0x10000;
    busy[nbusy++].end = (sp | 0xffff) + 0x10001;

    dst = (ram_end - size) & ~(PAGE_SIZE - 1);
    do {
        moved = 0;
        for (i = 0; i < nbusy; i++) {
            if (dst >= floor && overlaps(dst, dst + size, &busy[i])) {
                dst = (busy[i].start - size) & ~(PAGE_SIZE - 1);
                moved = 1;
            }
        }
    } while (moved && dst >= floor && dst < ram_end);

    if (dst < floor || dst >= ram_end) {
        printl("No room in the RAM for the initrd (%u bytes)" NEWLINE, size);
        return -1;
    }
    settings.initrd.addr = (void *)dst;
    return 0;
}

/* Interrupts off: nothing of the OS may run any more */
void initrd_gather(void) {
    char *dst = settings.initrd.addr;
    int i;

    if (in_place)
        return;

    for (i = 0; i < npieces; i++) {
        builtin_memcpy(dst, pieces[i].addr, pieces[i].size);
        dst += pieces[i].size;
    }
}
