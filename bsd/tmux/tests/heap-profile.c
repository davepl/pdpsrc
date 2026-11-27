/* Actual option and CSI units: global names, copied local names and small args. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tmux-full.h"

void input_new_argument(struct input_ctx *);
int input_add_argument(struct input_ctx *, u_char);
int input_get_argument(struct input_ctx *, u_int, uint16_t *, uint16_t);

#if defined(TMUX_MILESTONE) && defined(TMUX_NATIVE_HEAP_TEST)
#define EXPECT_BORROWED 1
#else
#define EXPECT_BORROWED 0
#endif

static unsigned int data_freed;

static void
release_data(void *data)
{
	data_freed++;
	xfree(data);
}

static void
check_name(const struct options_entry *entry, const char *name, int borrowed)
{
	assert(strcmp(entry->name, name) == 0);
	assert((entry->name == name) == borrowed);
}

static void
check_options(void)
{
	static const char number_name[] = "heap-profile-number";
	static const char string_name[] = "heap-profile-string";
	static const char data_name[] = "heap-profile-data";
	struct options local;
	struct options_entry *entry;
	char mutable_name[40], value[16] = "original";
	void *data;

	options_init(&global_s_options, NULL);
	options_init(&global_w_options, NULL);
	options_init(&local, &global_s_options);
	entry = options_set_number(&global_s_options, number_name, 37);
	check_name(entry, number_name, EXPECT_BORROWED);
	assert(options_get_number(&local, number_name) == 37);
	entry = options_set_string(&global_w_options, string_name, "%s", value);
	check_name(entry, string_name, EXPECT_BORROWED);
	value[0] = 'X';
	assert(strcmp(options_get_string(&global_w_options, string_name), "original") == 0);
	options_set_number(&global_w_options, string_name, 42);
	assert(options_get_number(&global_w_options, string_name) == 42);
	check_name(options_find1(&global_w_options, string_name), string_name, EXPECT_BORROWED);

	data = xmalloc(8);
	entry = options_set_data(&global_s_options, data_name, data, release_data);
	check_name(entry, data_name, EXPECT_BORROWED);
	options_set_string(&global_s_options, data_name, "%s", "replacement");
	assert(data_freed == 1);
	options_remove(&global_s_options, data_name);
	assert(options_find1(&global_s_options, data_name) == NULL);
	options_remove(&global_s_options, number_name);
	assert(options_find1(&global_s_options, number_name) == NULL);

	/* Subordinate names retain independent lifetime, including dynamic names. */
	strcpy(mutable_name, "subordinate-number");
	entry = options_set_number(&local, mutable_name, 99);
	check_name(entry, mutable_name, 0);
	mutable_name[0] = 'X';
	assert(options_get_number(&local, "subordinate-number") == 99);
	options_remove(&local, "subordinate-number");
	strcpy(mutable_name, "subordinate-string");
	entry = options_set_string(&local, mutable_name, "%s", "kept");
	check_name(entry, mutable_name, 0);
	mutable_name[0] = 'X';
	assert(strcmp(options_get_string(&local, "subordinate-string"), "kept") == 0);
	strcpy(mutable_name, "subordinate-data");
	entry = options_set_data(&local, mutable_name, xmalloc(8), release_data);
	check_name(entry, mutable_name, 0);
	mutable_name[0] = 'X';
	options_free(&local);
	assert(data_freed == 2);
	entry = options_set_data(&global_s_options, data_name, xmalloc(8), release_data);
	check_name(entry, data_name, EXPECT_BORROWED);
	options_set_string(&global_w_options, string_name, "%s", "final value");
	options_free(&global_s_options);
	options_free(&global_w_options);
	assert(data_freed == 3);
	assert(SPLAY_EMPTY(&global_s_options.tree) && SPLAY_EMPTY(&global_w_options.tree));
}

static void
check_csi_arguments(void)
{
	struct input_ctx input;
	struct input_arg *previous;
	size_t capacity;
	unsigned int count, i;
	uint16_t number;

	memset(&input, 0, sizeof input);
	assert(sizeof input.args.list[0].data == 64);
	for (count = 1; count <= 100; count++) {
		previous = input.args.list;
		capacity = input.args.space;
		input_new_argument(&input);
		assert(ARRAY_LENGTH(&input.args) == count);
		assert(input.args.space >= count * sizeof *input.args.list);
		if (capacity > count * sizeof *input.args.list ||
		    (EXPECT_BORROWED && capacity == count * sizeof *input.args.list))
			assert(input.args.list == previous);
#if EXPECT_BORROWED
		if (count <= 4)
			assert(input.args.space == 4 * sizeof *input.args.list);
		if (count == 5)
			assert(input.args.space == 8 * sizeof *input.args.list);
#endif
		for (i = 0; i < count - 1; i++)
			assert(input.args.list[i].used == 1 && input.args.list[i].data[0] == '7');
		assert(ARRAY_LAST(&input.args).used == 0);
		assert(input_add_argument(&input, '7') == 0);
	}
	previous = input.args.list;
	capacity = input.args.space;
	ARRAY_CLEAR(&input.args);
	input_new_argument(&input);
	assert(input.args.list == previous && input.args.space == capacity);
	for (i = 0; i < 62; i++)
		assert(input_add_argument(&input, '0') == 0);
	assert(input_add_argument(&input, '7') == 0);
	assert(ARRAY_LAST(&input.args).used == 63);
	/* The parser terminates an argument itself; storage is not zero-filled. */
	assert(input_add_argument(&input, '\0') == 0);
	assert(ARRAY_LAST(&input.args).used == 64);
	assert(input_add_argument(&input, '1') == -1);
	assert(input_get_argument(&input, 0, &number, 0) == 0 && number == 7);
	ARRAY_FREE(&input.args);
}

int
main(void)
{
	check_options();
	check_csi_arguments();
	puts("option-name ownership and CSI growth/reuse/64-byte argument semantics passed");
	return 0;
}
