#ifndef INITRD_H
#define INITRD_H

int initrd_load(const char *filename);
int initrd_in_kernel_block(void);
void initrd_free(void);
int initrd_place(void);
void initrd_gather(void);

#endif
