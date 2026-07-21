#include "mt/elf.h"

#include <stdio.h>
#include <stdlib.h>

int
main(int argc, char **argv)
{
	FILE *file;
	unsigned char *bytes;
	long length;
	size_t size;
	struct mt_elf64_view view;
	enum mt_elf_status status;

	if (argc != 2)
		return 2;
	file = fopen(argv[1], "rb");
	if (!file)
		return 3;
	if (fseek(file, 0, SEEK_END) != 0) {
		fclose(file);
		return 4;
	}
	length = ftell(file);
	if (length <= 0 || fseek(file, 0, SEEK_SET) != 0) {
		fclose(file);
		return 5;
	}
	size = (size_t)length;
	bytes = (unsigned char *)malloc(size);
	if (!bytes || fread(bytes, 1, size, file) != size) {
		free(bytes);
		fclose(file);
		return 6;
	}
	fclose(file);
	status = mt_elf64_parse(bytes, size, &view);
	if (status != MT_ELF_OK) {
		fprintf(stderr, "elf_probe: %s\n", mt_elf_status_string(status));
		free(bytes);
		return 7;
	}
	printf("type=%u machine=%s sections=%u\n",
	       (unsigned)view.type, mt_elf_machine_name(view.machine),
	       (unsigned)view.section_count);
	free(bytes);
	return 0;
}
