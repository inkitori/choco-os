// Loads Limine modules: the ustar initrd becomes the root filesystem
// contents; *.bin model files are exposed read-only under /models without
// copying (they stay in bootloader-reclaimable memory, which the PMM never
// hands out).
#include "initrd.h"
#include "vfs.h"
#include "string.h"
#include "kprintf.h"

extern volatile struct limine_module_request module_request;

struct limine_file *module_find(const char *name)
{
	if (module_request.response == NULL)
		return NULL;

	for (uint64_t i = 0; i < module_request.response->module_count; i++)
	{
		struct limine_file *f = module_request.response->modules[i];
		size_t plen = strlen(f->path);
		size_t nlen = strlen(name);
		if (plen >= nlen && strcmp(f->path + plen - nlen, name) == 0)
			return f;
	}
	return NULL;
}

static uint64_t octal_to_int(const char *s, size_t len)
{
	uint64_t v = 0;
	for (size_t i = 0; i < len && s[i] >= '0' && s[i] <= '7'; i++)
		v = v * 8 + (s[i] - '0');
	return v;
}

static void load_tar(uint8_t *base, size_t size)
{
	size_t off = 0;
	int files = 0;

	while (off + 512 <= size)
	{
		uint8_t *hdr = base + off;
		if (hdr[0] == '\0')
			break;

		char name[100 + 1];
		memcpy(name, hdr, 100);
		name[100] = '\0';

		uint64_t fsize = octal_to_int((char *)hdr + 124, 12);
		char type = hdr[156];

		// Strip leading "./"
		char *clean = name;
		if (clean[0] == '.' && clean[1] == '/')
			clean += 2;

		if (clean[0] != '\0')
		{
			if (type == '5')
			{
				vfs_mkdirs(clean, vfs_root());
			}
			else if (type == '0' || type == '\0')
			{
				// Create parent dirs, then the file.
				char *slash = strrchr(clean, '/');
				VfsNode *dir = vfs_root();
				const char *leaf = clean;
				if (slash)
				{
					*slash = '\0';
					dir = vfs_mkdirs(clean, vfs_root());
					leaf = slash + 1;
				}
				if (dir && leaf[0])
				{
					VfsNode *f = vfs_create(dir, leaf, false);
					if (f && fsize > 0)
						vfs_write(f, base + off + 512, 0, fsize);
					files++;
				}
			}
		}

		off += 512 + ((fsize + 511) & ~511ull);
	}

	kprintf("initrd: loaded %d files\n", files);
}

static void register_module_file(const char *name, struct limine_file *mod)
{
	VfsNode *dir = vfs_mkdirs("/models", vfs_root());
	if (!dir)
		return;
	VfsNode *f = vfs_create(dir, name, false);
	if (!f)
		return;
	f->readonly = true;
	f->data = mod->address;
	f->size = mod->size;
	f->cap = 0;
	kprintf("initrd: module %s (%lu KiB) at %p\n", name,
			(uint64_t)(mod->size / 1024), mod->address);
}

void initrd_init(void)
{
	vfs_init();

	struct limine_file *tar = module_find("initrd.tar");
	if (tar)
		load_tar(tar->address, tar->size);
	else
		kprintf("initrd: no initrd.tar module\n");

	static const char *models[] = {
		"stories15M.bin", "stories260K.bin", "tokenizer.bin", "tok512.bin",
		"qwen3.bin", "qwen3.tokenizer"};
	for (unsigned i = 0; i < sizeof(models) / sizeof(models[0]); i++)
	{
		struct limine_file *m = module_find(models[i]);
		if (m)
			register_module_file(models[i], m);
	}
}
