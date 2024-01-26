#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef uint8_t u8;
typedef int32_t b32;
typedef ptrdiff_t isize;

#define handle_error()                                                                                                 \
    ({                                                                                                                 \
        printf("Error %s\n", strerror(errno));                                                                         \
        exit(-1);                                                                                                      \
    })

#define countof(a)  (isize)(sizeof(a) / sizeof(*(a)))
#define lengthof(s) (countof(s) - 1)

#define string(s)                                                                                                      \
    (struct string) { (u8*)s, lengthof(s) }

struct string {
    u8* data;
    isize len;
};

/* Buffers */

#define DECLARE_SLICE_HEADER(name, T)                                                                                  \
    struct name {                                                                                                      \
        T* data;                                                                                                       \
        isize len;                                                                                                     \
        isize cap;                                                                                                     \
    };

static void
grow_(void* slice, isize size) {
    struct {
        void* data;
        isize len;
        isize cap;
    } replica;

    memcpy(&replica, slice, sizeof(replica));

    replica.cap = replica.cap ? replica.cap : 1;
    void* data = reallocarray(replica.data, replica.cap, 2 * size);
    if (!data) {
        handle_error();
    }
    replica.data = data;
    replica.cap *= 2;
    memcpy(slice, &replica, sizeof(replica));
}

#define grow(s) grow_(s, sizeof(*(s)->data))
#define push(s) ((s->len) >= (s)->cap ? grow(s), (s)->data + (s)->len++ : (s)->data + (s)->len++)

DECLARE_SLICE_HEADER(u8_buffer, u8);
DECLARE_SLICE_HEADER(integers, int);
DECLARE_SLICE_HEADER(many_u8_buffers, struct u8_buffer);
DECLARE_SLICE_HEADER(many_integer_buffers, struct integers);

/*  */

void
u8_buffer_append_string(struct u8_buffer* b, struct string s) {
    for (int i = 0; i < s.len; i++) {
        *push(b) = s.data[i];
    }
}

void
u8_buffer_append_int(struct u8_buffer* b, int value) {
    isize nleft = b->cap - b->len;
    while (nleft < 256) {
        grow(b);
        nleft = b->cap - b->len;
    }
    char* s = (char*)&b->data[b->len];
    int n = snprintf(s, nleft, "%d", value);
    if (n >= nleft) {
        handle_error();
    }
    b->len += n;
}

void
u8_buffer_clear(struct u8_buffer* b) {
    b->len = 0;
}

void
u8_buffer_write_fd(struct u8_buffer* b, int fd) {
    u8* start = b->data;
    u8* at = start;
    u8* end = &b->data[b->len];

    while (at < end) {
        isize n = write(fd, (char*)at, end - at);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                handle_error();
            }
        }
        at += n;
    }
    return;
}

void
integers_write_fd(struct integers* ints, int fd) {
    struct u8_buffer buffer = {0};
    char s[512] = {0};
    for (isize i = 0; i < ints->len; i++) {
        int n = sprintf(s, "%d", ints->data[i]);
        if (n < 0) {
            handle_error();
        }
        if (i > 0) {
            *push((&buffer)) = ' ';
        }
        for (isize j = 0; s[j]; j++) {
            *push((&buffer)) = s[j];
        }
        u8_buffer_write_fd(&buffer, fd);
        buffer.len = 0;
    }
    free(buffer.data);
}

struct read_result {
    isize nread;
    b32 eof;
    b32 error;
};

struct read_result
u8_buffer_read_fd(struct u8_buffer* b, int fd) {
    struct read_result r = {0};

    u8* start = &b->data[b->len];
    u8* at = start;
    u8* end = &b->data[b->cap];

    while (at < end) {
        isize n = read(fd, at, end - at);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            } else {
                r.error = errno;
                break;
            }
        }
        if (n == 0) {
            r.eof = 1;
            break;
        }
        at += n;
    }

    r.nread = at - start;
    b->len += r.nread;
    return r;
}

struct read_result
u8_buffer_read_fd_until_eof(struct u8_buffer* b, int fd) {
    struct read_result r = {0};
    isize len = b->len;

    for (;;) {
        if (b->len == b->cap) {
            grow(b);
        }
        r = u8_buffer_read_fd(b, fd);
        if (r.error || r.eof) {
            break;
        }
    }

    r.nread = b->len - len;
    return r;
}

struct parse_result {
    isize location;
    b32 error;
};

struct parse_result
parse(struct u8_buffer* b, struct integers* ints) {
    struct parse_result r = {0};
    for (isize i = 0; i < b->len;) {
        if (isspace(b->data[i])) {
            i++;
            continue;
        }

        if (isdigit(b->data[i])) {
            char* start = (char*)&b->data[i];
            char* end = (char*)start;
            long value = strtol(start, &end, 10);
            assert(value >= (long)INT_MIN && value <= (long)INT_MAX);
            isize count = end - start;
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

struct int_span {
    isize off;
    isize len;
};

struct int_span
mergesort_integers_merge(int* sorted, int* tmp, struct int_span left, struct int_span right) {
    isize i = 0;
    isize j = 0;
    isize len = left.len + right.len;

    int* left_start = &sorted[left.off];
    int* right_start = &sorted[right.off];
    int* tmp_start = &tmp[left.off];
    for (; (i + j) < len;) {
        if (i < left.len && (j == right.len || left_start[i] <= right_start[j])) {
            tmp_start[i + j] = left_start[i];
            i++;
        } else {
            tmp_start[i + j] = right_start[j];
            j++;
        }
    }

    for (int i = 0; i < len; i++) {
        left_start[i] = tmp_start[i];
    }
    return (struct int_span){left.off, len};
}

struct int_span
mergesort_integers_(int* sorted, int* tmp, struct int_span span) {
    if (span.len <= 1) {
        return span;
    }

    isize half = span.len / 2;
    struct int_span left = {span.off, half};
    struct int_span right = {span.off + half, span.len - half};
    struct int_span left_sorted = mergesort_integers_(sorted, tmp, left);
    struct int_span right_sorted = mergesort_integers_(sorted, tmp, right);
    struct int_span merged = mergesort_integers_merge(sorted, tmp, left_sorted, right_sorted);
    return merged;
}

void
mergesort_integers(struct integers* ints) {
    void* tmp = reallocarray(NULL, ints->len, sizeof(*ints->data));
    if (NULL == tmp) {
        handle_error();
    }
    struct int_span span = {0, ints->len};
    mergesort_integers_(ints->data, tmp, span);
    free(tmp);
    return;
}

/* Merge sorted arrays */

/* Can simplify to just isize value */
struct cursor {
    isize index;
};

DECLARE_SLICE_HEADER(cursors, struct cursor);

isize
get_progress(struct cursors* cursors) {
    isize result = 0;
    for (int i = 0; i < cursors->len; i++) {
        result += cursors->data[i].index;
    }
    return result;
}

void
merge_sorted_and_write(struct many_integer_buffers* integer_buffers, int fd) {
    assert(integer_buffers->len > 0);

    struct u8_buffer b = {0};

    isize total_len = 0;
    struct cursors cursors = {0};
    for (int i = 0; i < integer_buffers->len; i++) {
        *push((&cursors)) = (struct cursor){0};
        total_len += integer_buffers->data[i].len;
    }

    isize buffer_index = 0;
    for (isize progress = 0; progress < total_len;) {
        int value = INT_MAX;
        for (int i = 0; i < integer_buffers->len; i++) {
            struct integers* ints = &integer_buffers->data[i];
            struct cursor* cursor = &cursors.data[i];
            if (cursor->index < ints->len) {
                if (ints->data[cursor->index] <= value) {
                    value = ints->data[cursor->index];
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

        cursors.data[buffer_index].index++;
        progress++;
    }

    return;
}

int
main(int argc, char** argv) {
    if (argc == 1) {
        printf("Pass filenames\n");
        return 1;
    }

    /* TODO i want option to backing buffer with static array */
    struct many_u8_buffers u8_buffers = {0};
    for (isize i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        struct u8_buffer buffer = {0};
        struct read_result r = u8_buffer_read_fd_until_eof(&buffer, fd);
        if (r.error) {
            handle_error();
        }
        assert(r.eof);
        *push((&u8_buffers)) = buffer;

        close(fd);
    }
    assert(u8_buffers.len == argc - 1);

    struct many_integer_buffers integer_buffers = {0};
    for (isize i = 0; i < u8_buffers.len; i++) {
        struct integers ints = {0};
        struct parse_result r = parse(&u8_buffers.data[i], &ints);
        assert(!r.error);
        *push((&integer_buffers)) = ints;
    }
    assert(integer_buffers.len == u8_buffers.len);

    /* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */
    for (isize i = 0; i < integer_buffers.len; i++) {
        mergesort_integers(&integer_buffers.data[i]);
    }

    int fd = open("sorted.txt", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
    merge_sorted_and_write(&integer_buffers, fd);
    close(fd);

    return 0;
}
