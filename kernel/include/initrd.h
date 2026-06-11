#ifndef INITRD_H
#define INITRD_H

#include "limine.h"

// Mounts the initrd tar into the VFS and registers model modules under
// /models. Safe to call when no modules were loaded.
void initrd_init(void);

// Find a Limine module whose path ends with `name`. NULL if absent.
struct limine_file *module_find(const char *name);

#endif
