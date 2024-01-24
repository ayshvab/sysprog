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

DECLARE_SLICE_HEADER(integers, int);
DECLARE_SLICE_HEADER(u8_buffer, u8);
DECLARE_SLICE_HEADER(buffer_of_u8_buffers, struct u8_buffer);
DECLARE_SLICE_HEADER(buffer_of_integer_buffers, struct integers);

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

int
main(int argc, char** argv) {
    if (argc == 1) {
        printf("Pass filenames\n");
        return 1;
    }

    /* TODO i want option to backing buffer with static array */
    struct buffer_of_u8_buffers u8_buffers = {0};
    for (isize i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        struct u8_buffer buffer = {0};
        struct read_result r = u8_buffer_read_fd_until_eof(&buffer, fd);
        assert(r.eof);
        *push((&u8_buffers)) = buffer;

        close(fd);
    }
    assert(u8_buffers.len == argc-1);

    struct buffer_of_integer_buffers integer_buffers = {0};
    for (isize i = 0; i < u8_buffers.len; i++) {
        struct integers ints = {0};
        struct parse_result r = parse(&u8_buffers.data[i], &ints);
        assert(!r.error);
        *push((&integer_buffers)) = ints;
    }
    assert(integer_buffers.len == u8_buffers.len);

    /* IMPLEMENT MERGING OF THE SORTED ARRAYS HERE. */

    for (isize i = 0; i < integer_buffers.len; i++) {
        char filename[512];
        int n = sprintf(filename, "clone_of_%s", argv[i+1]);
        if (n < 0) {
            handle_error();
        }
        int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
        integers_write_fd(&integer_buffers.data[i], fd);
        close(fd);
    }

    return 0;
}

/* INTERMIDIATE TEST FOR READING/PARSING/WRITING

python3 generator.py -f test1.txt -c 10000 -m 10000 && \
python3 generator.py -f test2.txt -c 10000 -m 10000 && \
python3 generator.py -f test3.txt -c 10000 -m 10000 && \
python3 generator.py -f test4.txt -c 10000 -m 10000 && \
python3 generator.py -f test5.txt -c 10000 -m 10000 && \
python3 generator.py -f test6.txt -c 100000 -m 10000

./run_sort test1.txt test2.txt test3.txt test4.txt test5.txt test6.txt

diff test1.txt clone_of_test1.txt && \
diff test2.txt clone_of_test2.txt && \
diff test3.txt clone_of_test3.txt && \
diff test4.txt clone_of_test4.txt && \
diff test5.txt clone_of_test5.txt && \
diff test6.txt clone_of_test6.txt

 */
