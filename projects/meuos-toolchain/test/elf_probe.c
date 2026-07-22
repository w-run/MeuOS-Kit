#include "mt/elf.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned
count_exported_symbols(const unsigned char *bytes, size_t size,
                       const struct mt_elf64_view *view)
{
	struct mt_elf64_section table;
	struct mt_elf64_section strings;
	struct mt_elf64_symbol symbol;
	const char *name;
	unsigned count = 0;
	uint16_t section_index;
	uint64_t symbol_index;
	uint64_t symbol_count;
	unsigned binding;

	for (section_index = 0; section_index < view->section_count;
	     ++section_index) {
		if (mt_elf64_get_section(bytes, size, view, section_index, &table) !=
		    MT_ELF_OK)
			return 0;
		if (table.type != MT_SHT_SYMTAB || table.link >= view->section_count ||
		    mt_elf64_get_section(bytes, size, view, (uint16_t)table.link,
		                         &strings) != MT_ELF_OK)
			continue;
		if (table.entry_size < MT_ELF64_SYM_SIZE ||
		    table.size % table.entry_size != 0)
			return 0;
		symbol_count = table.size / table.entry_size;
		for (symbol_index = 0; symbol_index < symbol_count; ++symbol_index) {
			if (mt_elf64_get_symbol(bytes, size, &table, symbol_index,
			                        &symbol) != MT_ELF_OK)
				return 0;
			binding = (unsigned)(symbol.info >> MT_STB_SHIFT);
			if ((binding == MT_STB_GLOBAL || binding == MT_STB_WEAK) &&
			    symbol.section != MT_SHN_UNDEF && symbol.name != 0 &&
			    mt_elf64_get_string(bytes, size, &strings, symbol.name,
			                       &name) == MT_ELF_OK && *name != '\0')
				++count;
		}
		return count;
	}
	return 0;
}

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
	printf("type=%u machine=%s sections=%u symbols=%u\n",
	       (unsigned)view.type, mt_elf_machine_name(view.machine),
	       (unsigned)view.section_count,
	       count_exported_symbols(bytes, size, &view));
	free(bytes);
	return 0;
}
