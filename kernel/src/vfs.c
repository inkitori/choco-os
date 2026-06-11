// Minimal in-RAM filesystem: a tree of nodes with heap-backed file data.
#include "vfs.h"
#include "malloc.h"
#include "string.h"

static VfsNode root_node;

void vfs_init(void)
{
	strcpy(root_node.name, "/");
	root_node.is_dir = true;
	root_node.parent = NULL;
	root_node.children = NULL;
	root_node.next = NULL;
}

VfsNode *vfs_root(void)
{
	return &root_node;
}

static VfsNode *dir_find(VfsNode *dir, const char *name, size_t len)
{
	for (VfsNode *c = dir->children; c; c = c->next)
	{
		if (strlen(c->name) == len && strncmp(c->name, name, len) == 0)
			return c;
	}
	return NULL;
}

// Walk one path component at a time. `stop_at_last` returns the parent of
// the final component instead of the component itself.
static VfsNode *walk(const char *path, VfsNode *cwd, bool stop_at_last,
					 const char **leaf)
{
	VfsNode *cur = (path[0] == '/') ? &root_node : (cwd ? cwd : &root_node);
	const char *p = path;

	while (*p == '/')
		p++;

	if (*p == '\0')
	{
		if (leaf)
			*leaf = NULL;
		return stop_at_last ? NULL : cur;
	}

	for (;;)
	{
		const char *start = p;
		while (*p && *p != '/')
			p++;
		size_t len = p - start;

		const char *rest = p;
		while (*rest == '/')
			rest++;
		bool is_last = (*rest == '\0');

		if (is_last && stop_at_last)
		{
			if (leaf)
				*leaf = start;
			return cur;
		}

		VfsNode *nxt;
		if (len == 1 && start[0] == '.')
			nxt = cur;
		else if (len == 2 && start[0] == '.' && start[1] == '.')
			nxt = cur->parent ? cur->parent : cur;
		else
			nxt = dir_find(cur, start, len);

		if (!nxt || (!is_last && !nxt->is_dir))
			return NULL;
		if (is_last)
			return nxt;

		cur = nxt;
		p = rest;
	}
}

VfsNode *vfs_resolve(const char *path, VfsNode *cwd)
{
	return walk(path, cwd, false, NULL);
}

VfsNode *vfs_resolve_parent(const char *path, VfsNode *cwd, const char **leaf)
{
	return walk(path, cwd, true, leaf);
}

VfsNode *vfs_create(VfsNode *dir, const char *name, bool is_dir)
{
	if (!dir || !dir->is_dir)
		return NULL;
	size_t len = strlen(name);
	if (len == 0 || len >= VFS_NAME_MAX)
		return NULL;
	if (dir_find(dir, name, len))
		return NULL;

	VfsNode *n = calloc(1, sizeof(VfsNode));
	if (!n)
		return NULL;
	strcpy(n->name, name);
	n->is_dir = is_dir;
	n->parent = dir;
	n->next = dir->children;
	dir->children = n;
	return n;
}

VfsNode *vfs_mkdirs(const char *path, VfsNode *cwd)
{
	VfsNode *cur = (path[0] == '/') ? &root_node : (cwd ? cwd : &root_node);
	const char *p = path;

	while (*p == '/')
		p++;

	while (*p)
	{
		const char *start = p;
		while (*p && *p != '/')
			p++;
		size_t len = p - start;
		while (*p == '/')
			p++;

		if (len == 1 && start[0] == '.')
			continue;
		if (len == 2 && start[0] == '.' && start[1] == '.')
		{
			cur = cur->parent ? cur->parent : cur;
			continue;
		}

		VfsNode *nxt = dir_find(cur, start, len);
		if (!nxt)
		{
			char name[VFS_NAME_MAX];
			if (len >= VFS_NAME_MAX)
				return NULL;
			memcpy(name, start, len);
			name[len] = '\0';
			nxt = vfs_create(cur, name, true);
			if (!nxt)
				return NULL;
		}
		if (!nxt->is_dir)
			return NULL;
		cur = nxt;
	}
	return cur;
}

int vfs_write(VfsNode *file, const void *buf, size_t off, size_t len)
{
	if (!file || file->is_dir || file->readonly)
		return -1;

	size_t end = off + len;
	if (end > file->cap)
	{
		size_t newcap = file->cap ? file->cap : 64;
		while (newcap < end)
			newcap *= 2;
		uint8_t *nd = malloc(newcap);
		if (!nd)
			return -1;
		if (file->data)
		{
			memcpy(nd, file->data, file->size);
			free(file->data);
		}
		file->data = nd;
		file->cap = newcap;
	}

	if (off > file->size)
		memset(file->data + file->size, 0, off - file->size);
	memcpy(file->data + off, buf, len);
	if (end > file->size)
		file->size = end;
	return (int)len;
}

int vfs_truncate(VfsNode *file, size_t size)
{
	if (!file || file->is_dir || file->readonly)
		return -1;
	if (size <= file->size)
	{
		file->size = size;
		return 0;
	}
	uint8_t zero = 0;
	return vfs_write(file, &zero, size - 1, 1) < 0 ? -1 : 0;
}

int vfs_unlink(VfsNode *node)
{
	if (!node || node == &root_node)
		return -1;
	if (node->is_dir && node->children)
		return -1; // not empty

	VfsNode **link = &node->parent->children;
	while (*link && *link != node)
		link = &(*link)->next;
	if (!*link)
		return -1;
	*link = node->next;

	if (!node->readonly && node->data)
		free(node->data);
	free(node);
	return 0;
}

void vfs_node_path(VfsNode *node, char *buf, size_t size)
{
	if (!node->parent)
	{
		strlcpy(buf, "/", size);
		return;
	}

	// Build by walking up; collect components then reverse-print.
	VfsNode *stack[32];
	int depth = 0;
	for (VfsNode *n = node; n->parent && depth < 32; n = n->parent)
		stack[depth++] = n;

	size_t pos = 0;
	for (int i = depth - 1; i >= 0; i--)
	{
		if (pos + 1 < size)
			buf[pos++] = '/';
		const char *nm = stack[i]->name;
		while (*nm && pos + 1 < size)
			buf[pos++] = *nm++;
	}
	buf[pos < size ? pos : size - 1] = '\0';
}
