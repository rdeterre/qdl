#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <stdbool.h>
#include "qdl.h"

struct program {
	unsigned sector_size;
	unsigned file_offset;
	const char *filename;
	const char *label;
	unsigned num_sectors;
	unsigned partition;
	const char *start_sector;

	struct program *next;
};

int program_load(const char *program_file);
int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
                    const char *incdir, void* progress_callback_context);
int program_find_bootable_partition(void);
void progress_callback(void *context, int current, int total);
#endif
