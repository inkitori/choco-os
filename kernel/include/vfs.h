#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VFS_NAME_MAX 64

typedef struct VfsNode
{
	char name[VFS_NAME_MAX];
	bool is_dir;
	bool readonly; // e.g. files backed by bootloader module memory
	uint8_t *data;
	size_t size;
	size_t cap;
	struct VfsNode *parent;
	struct VfsNode *children; // first child (if dir)
	struct VfsNode *next;	  // next sibling
} VfsNode;

void vfs_init(void);
VfsNode *vfs_root(void);

// Resolve a path (absolute or relative to cwd). Returns NULL if not found.
VfsNode *vfs_resolve(const char *path, VfsNode *cwd);
// Resolve the directory part of a path and return the final component name.
VfsNode *vfs_resolve_parent(const char *path, VfsNode *cwd, const char **leaf);

VfsNode *vfs_create(VfsNode *dir, const char *name, bool is_dir);
// mkdir -p style: create all missing directories along the path.
VfsNode *vfs_mkdirs(const char *path, VfsNode *cwd);

int vfs_write(VfsNode *file, const void *buf, size_t off, size_t len);
int vfs_truncate(VfsNode *file, size_t size);
int vfs_unlink(VfsNode *node);

// Build the absolute path of a node into buf.
void vfs_node_path(VfsNode *node, char *buf, size_t size);

#endif
