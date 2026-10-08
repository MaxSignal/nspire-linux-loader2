/*
    TI-NSPIRE Linux In-Place Bootloader
    Copyright (C) 2012  Daniel Tang

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

#include <os.h>
#include <libfdt.h>

#include "common.h"
#include "initrd.h"
#include "memory.h"

static size_t file_size(const char *filename) {
    struct stat stats;
    if (stat(filename, &stats)) return 0;
    return stats.st_size;
}

void load_kernel(const char *filename) {
    size_t kernel_size = file_size(filename);
    FILE *f;

    if (!kernel_size) {
        printl("Kernel doesn't exist or empty" NEWLINE);
        return;
    }

    if (settings.initrd_loaded && initrd_in_kernel_block()) {
        printl("Load the kernel before the initrd" NEWLINE);
        return;
    }
    if (mem_block_fit(kernel_size)) {
        printl( "Kernel too large!" NEWLINE
                "Tried to load kernel of %u bytes into %u bytes of free space" NEWLINE,
                kernel_size, settings.mem_block.size);
        return;
    }

    f = fopen(filename, "rb");
    if (!f) {
        printl("Failed to open kernel image %s" NEWLINE, filename);
        return;
    }

    settings.kernel.addr = settings.mem_block.start;
    settings.kernel.size = fread(settings.kernel.addr, 1, kernel_size, f);
    settings.kernel_loaded = !!(settings.kernel.size);

    if (settings.kernel.size != kernel_size)
        printl("Warning: read less data from file than expected" NEWLINE);

    fclose(f);
    printl("Kernel successfully loaded" NEWLINE);
    return;
}

void load_initrd(const char *filename) {
    if (!strlen(filename) && settings.initrd_loaded) {
        initrd_free();
        settings.initrd_failed = 0;
        printl("Unloaded initrd" NEWLINE);
        return;
    }
    /* Do not boot without the initrd the script asked for */
    settings.initrd_failed = !!initrd_load(filename);
}

void load_dtb(const char *filename) {
    FILE *f;
    size_t dtb_size = file_size(filename);

    if (dtb_size > settings.boot_param.size) {
        printl("DTB image too large!" NEWLINE);
        return;
    }

    f = fopen(filename, "rb");
    if (!f) {
        printl("Failed to open dtb image %s" NEWLINE, filename);
        return;
    }

    if (fread(settings.boot_param.start, 1, dtb_size, f) != dtb_size)
        printl("Warning: read less data from file than expected" NEWLINE);

    fclose(f);

    if (fdt_check_header(settings.boot_param.start)) {
        printl("Not a valid DTB!" NEWLINE);
        return;
    }

    settings.dtb_loaded = 1;
    settings.machine_id = DTB_MACH_ID;

    printl("DTB successfully loaded" NEWLINE);
    return;
}
