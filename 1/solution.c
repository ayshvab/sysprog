#include <ctype.h>
#include <limits.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include "common.h"

/**
 * You can compile and run this code using the commands:
 *
 * $> gcc solution.c libcoro.c
 * $> ./a.out
 */

/* ========================================== */
struct my_context {
    /** ADD HERE YOUR OWN MEMBERS, SUCH AS FILE NAME, WORK TIME, ... */
    isize id;
	struct integers* ints;
    char* name;
    char* filename;
};

DECLARE_SLICE_HEADER(integers, int);
DECLARE_SLICE_HEADER(integer_buffers, struct integers);

/* Parsing */
struct parse_result {
    isize location;
    b32 error;
};

struct parse_result
parse(struct u8_buffer* b, struct integers* ints) {
    struct parse_result r = {0};
    for (isize i = 0; i < b->len;) {
		coro_yield();

        if (isspace(b->data[i])) {
            i++;
            continue;
        }

        if (isdigit(b->data[i])) {
            char* begin = (char*)&b->data[i];
            char* end = (char*)begin;
            long value = strtol(begin, &end, 10);
            assert(value >= (long)INT_MIN && value <= (long)INT_MAX);
            isize count = end - begin;
            if (!count) {
                r.error = 1;
                r.location = i;
                break;
            }
            i += count;
            *push(ints) = value;
        }
    }
    return r;
}

/* Sorting */

struct span {
    isize begin;
    isize len;
};

static struct span
mergesort_integers_merge_(int* sorted, int* tmp, struct span left_span, struct span right_span) {
    isize i = 0;
    isize j = 0;
    isize len = left_span.len + right_span.len;

    int* left = &sorted[left_span.begin];
    int* right = &sorted[right_span.begin];
    int* tmp_begin = &tmp[left_span.begin];
    for (; (i + j) < len;) {
        if (i < left_span.len && (j == right_span.len || left[i] <= right[j])) {
            tmp_begin[i + j] = left[i];
            i++;
        } else {
            tmp_begin[i + j] = right[j];
            j++;
        }
		coro_yield();
    }

    for (int i = 0; i < len; i++) {
        left[i] = tmp_begin[i];
    }
    return (struct span){left_span.begin, len};
}

static struct span
mergesort_integers_(int* sorted, int* tmp, struct span span) {
    if (span.len <= 1) {
        return span;
    }
	coro_yield();
    isize half = span.len / 2;
    struct span left = {span.begin, half};
    struct span right = {span.begin + half, span.len - half};
    struct span left_sorted = mergesort_integers_(sorted, tmp, left);
    struct span right_sorted = mergesort_integers_(sorted, tmp, right);
    struct span merged = mergesort_integers_merge_(sorted, tmp, left_sorted, right_sorted);
    return merged;
}

static void
sort_integers_using_mergesort(struct integers* ints) {
    void* tmp = reallocarray(NULL, ints->len, sizeof(*ints->data));
    if (NULL == tmp) {
        handle_error();
    }
    struct span span = {0, ints->len};
    mergesort_integers_(ints->data, tmp, span);
    free(tmp);
    return;
}

/* Merge sorted and write */

DECLARE_SLICE_HEADER(merging_indexes, isize);

static void
merge_sorted_and_write(struct integer_buffers* integer_buffers, int fd) {
	assert(integer_buffers->len > 0);
	struct u8_buffer b = {0};
	isize total_len = 0;
	struct merging_indexes merging_indexes = {0};
    for (int i = 0; i < integer_buffers->len; i++) {
        *push((&merging_indexes)) = 0;
        total_len += integer_buffers->data[i].len;
    }

    isize buffer_index = 0;
	for (isize progress = 0; progress < total_len;) {
        int value = INT_MAX;
        for (int i = 0; i < integer_buffers->len; i++) {
            struct integers* ints = &integer_buffers->data[i];
			isize index = merging_indexes.data[i];
            if (index < ints->len) {
                if (ints->data[index] <= value) {
                    value = ints->data[index];
                    buffer_index = i;
                }
            }
        }

        u8_buffer_clear(&b);
        if (progress > 0) {
            u8_buffer_append_string(&b, string(" "));
        }
        u8_buffer_append_int(&b, value);
        u8_buffer_write_fd(&b, fd);

        merging_indexes.data[buffer_index]++;
        progress++;
    }
}

/* ========================================== */



static struct my_context*
my_context_new(isize id, struct integers* ints, const char* name, char* filename) {
    struct my_context* ctx = malloc(sizeof(*ctx));
	ctx->id = id;
	ctx->ints = ints;
    ctx->name = strdup(name);
	ctx->filename = filename;
    return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
	free(ctx);
}

/**
 * A function, called from inside of coroutines recursively. Just to demonstrate
 * the example. You can split your code into multiple functions, that usually
 * helps to keep the individual code blocks simple.
 */
static void
other_function(const char *name, int depth)
{
	printf("%s: entered function, depth = %d\n", name, depth);
	coro_yield();
	if (depth < 3)
		other_function(name, depth + 1);
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */
static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF INDIVIDUAL FILES HERE. */
	struct coro *this = coro_this();
	struct my_context *ctx = context;
	printf("Started coroutine %s, %s\n", ctx->name, ctx->filename);
	coro_yield();

	char* name = ctx->name;	
	char* filename = ctx->filename;
	struct u8_buffer content = {0};
	{
		int fd = open(filename, O_RDONLY);
		struct read_result r = u8_buffer_read_fd_until_eof(&content, fd);
		if (r.error) {
			handle_error();
		}
		assert(r.eof);
		close(fd);
	}

	struct parse_result r = parse(&content, ctx->ints);
	printf("%s: switch count %lld\n", name, coro_switch_count(this));
	assert(!r.error);

	sort_integers_using_mergesort(ctx->ints);
	printf("%s: switch count %lld\n", name, coro_switch_count(this));

	free(content.data);
	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}

int
main(int argc, char **argv)
{
    if (argc == 1) {
        printf("Pass filenames\n");
        return 1;
    }

	// TODO: Preallocate integer_buffers
    struct integer_buffers integer_buffers = {0};
	for (int i = 1; i < argc; ++i) {
		*push((&integer_buffers)) = (struct integers){ 0 };
	}

	/* Initialize our coroutine global cooperative scheduler. */
	coro_sched_init();
	/* Start several coroutines. */
	for (int i = 1; i < argc; ++i) {
		/*
		 * The coroutines can take any 'void *' interpretation of which
		 * depends on what you want. Here as an example I give them
		 * some names.
		 */
		int id = i - 1;
		char name[16];
		sprintf(name, "coro_%d", id);
		/*
		 * I have to copy the name. Otherwise all the coroutines would
		 * have the same name when they finally start.
		 */
		coro_new(coroutine_func_f, my_context_new(id, &integer_buffers.data[id], name, argv[i]));
	}
	/* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		/*
		 * Each 'wait' returns a finished coroutine with which you can
		 * do anything you want. Like check its exit status, for
		 * example. Don't forget to free the coroutine afterwards.
		 */
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

	/* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */
    int fd = open("sorted_by_solution.txt", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    merge_sorted_and_write(&integer_buffers, fd);
    close(fd);

	return 0;
}
