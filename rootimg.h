/*
    TI-NSPIRE Linux In-Place Bootloader
    Linux root filesystem image kept as a file of the TI-Nspire filesystem

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef ROOTIMG_H
#define ROOTIMG_H

void rootimg(char *arg);
/* " nspire_tifs.path=..." once an image is set up, else "" */
const char *rootimg_cmdline(void);

#endif
