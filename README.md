# TI-NSPIRE Linux In-Place Bootloader V2

In-place boot a Linux kernel from inside the Nspire operating system.

## Improvements over old one

This version of the bootloader has been rewritten to be more stable, portable and flexible than the previous version.

Some big improvements include:

 * Being able to load ramdisks and kernels larger than 4MB (up to ~40MB on the CX)
 * This version does not need to relocate anything by default which means much higher stability
 * Has a shell-like interface to set options for loading
 * Scripting support
 * Cleaner and more portable code
 * Easy to port to other models

## Usage

Copy ```linuxloader.tns``` to your calculator and run it.

Valid commands are:

 * ```kernel <filename>```: Loads a kernel image into memory
 * ```initrd <filename>```: Loads a ramdisk into memory
 * ```dtb <filename>```: Loads a DTB image into memory
 * ```script <filename>```: Loads a script file
 * ```dump```: Prints out the current internal state of the bootloader. Useful for debugging.
 * ```free```: Prints out the total amount of memory provided to the bootloader by the Nspire OS and amounts used by the kernel and ramdisks.
 * ```cmdline [str]```: Get/set the kernel command line parameters.
 * ```mach [id]```: Get/set the machine ID that will be provided to Linux upon booting. Useful for overriding the builtin default value without having to recompile.
 * ```phys [<start> <size>]```: Get/set the address and size of physical memory. Useful for overriding the builtin default value without having to recompile.
 * ```rdsize [size]```: Get/set the size of the ramdisk that Linux should create on boot. Leave at ```0x0``` for the kernel default.
 * ```probemem```: If this is run on an calculator model that isn't directly supported by the bootloader, you can use this to try and guess how much memory the system has.
 * ```poke <addr> <value>```: Write a word to an arbitrary location in the memory address space.
 * ```peek <addr>```: Read a word from an arbitrary location in the memory address space.
 * ```rootimg <config file>```: Sets up a file in the calculator's filesystem for Linux to keep its root filesystem in (see below), and passes its name to the kernel (```nspire_tifs.path=```).
 * ```boot```: Boot kernel.

The bootloader is also scriptable. Create a text file containing a list of commands to be executed and change the extension to ```.ll2.tns```, and add the following line to your ```/documents/ndless/ndless.cfg.tns``` file. Any line starting with ```#``` will be considered a comment and ignored by the bootloader.

    ext.ll2=linuxloader2

Then simply open your script file and the loader will execute all the commands in it. A sample one could look like this:

    kernel linux/zImage.tns
    initrd linux/initrd.tns
    cmdline root=/dev/ram
    boot

This should save a lot of typing everytime you need to boot Linux.

### A root filesystem in the calculator's filesystem

There is no room on the NAND for a Linux partition, but Linux (with the
```nspire-tifs``` driver) can use the contents of a file of the TI-Nspire
filesystem as a block device. ```rootimg``` creates that file the first time:

    # linux.cfg.tns
    image = /documents/linux/rootfs.img.tns
    size = max      # or a size such as 64M (max: all the free space ...)
    reserve = 2M    # ... minus this much, left to the OS

With ```size = max```, or when there is not enough space for the size asked
for, the image takes all the free space but ```reserve```. A progress bar is
shown while the file is written. Every 4 KiB chunk of a new image carries a
tag (```NSPLXIMG``` and its index, ```NSPLXEND``` for the last one), which
lets the kernel check that it found the file's blocks in the right order
before it writes to them. A new image that is still unused is grown when
the configured size gets larger; an image Linux has used is left alone
(delete it from the TI-Nspire file browser to start over).

The host test in ```test/``` runs ```rootimg.c``` against a simulated file
system with limited free space: ```test/build.sh && cd test && ./run.sh```.

## Developers

I've done my best to write nice, clean code but it is always open to improvements. Here are some notes for extending the bootloader to support more commands and models.

If you're having trouble compiling, try removing ```-Werror``` from the CFLAGS.

### Adding a new command

Commands are defined in ```cmd.c```. Scroll down until you see a list of ```DEFINE_COMMAND```s. Just follow the example to add your own.

### Adding a new bootloader variable

The bootloader's internal state is stored in a struct called ```settings```. It is defined in ```common.h```. Feel free to add your own bootloader variables but if you do, make sure you also update the ```dump_settings()``` function in ```common.c``` so it is dumped correctly.

### Porting to another calculator model

Most of the code is already portable to other calculators. The only parameters that need adjusting are the ones passed to Linux such as the amount of physical memory and machine ID.

At the moment, they are hardcoded values for the CX. Code to detect and automatically set the correct values for different models needs to be written. They can be manually overrided using ```mach``` and ```phys``` commands until proper detection is implemented.

If you're interested in writing an implementation for it, look in ```mach.c``` and implement the ```detect_memory()``` and ```detect_machine()``` functions.
