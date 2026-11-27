/* Compare the native decision code with the original host table for every
 * Unicode codepoint, including table boundaries, interiors and all gaps. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmux-full.h"

void utf8_table_build(void);
u_int utf8_table_width(const struct utf8_data *);
u_int utf8_width(const struct utf8_data *);
uint32_t utf8_combine(const struct utf8_data *);

static void
encode(struct utf8_data *data, uint32_t value)
{
	memset(data, 0, sizeof *data);
	if (value < 0x80) {
		data->size = 1;
		data->data[0] = value;
	} else if (value < 0x800) {
		data->size = 2;
		data->data[0] = 0xc0 | (value >> 6);
		data->data[1] = 0x80 | (value & 0x3f);
	} else if (value < 0x10000L) {
		data->size = 3;
		data->data[0] = 0xe0 | (value >> 12);
		data->data[1] = 0x80 | ((value >> 6) & 0x3f);
		data->data[2] = 0x80 | (value & 0x3f);
	} else {
		data->size = 4;
		data->data[0] = 0xf0 | (value >> 18);
		data->data[1] = 0x80 | ((value >> 12) & 0x3f);
		data->data[2] = 0x80 | ((value >> 6) & 0x3f);
		data->data[3] = 0x80 | (value & 0x3f);
	}
	data->have = data->size;
}

int
main(void)
{
	struct utf8_data data;
	uint32_t value;
	u_int expected, actual;

	utf8_build();
	utf8_table_build();
	for (value = 0; value <= 0x10ffffL; value++) {
		encode(&data, value);
		assert(utf8_combine(&data) == value);
		expected = utf8_table_width(&data);
		actual = utf8_width(&data);
		if (actual != expected) {
			fprintf(stderr, "U+%06lx: code width %u, original table width %u\n",
			    (unsigned long) value, actual, expected);
			return (1);
		}
	}
	puts("native width code matches original table for all 1,114,112 codepoints");
	return (0);
}
