#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>

typedef uint8_t u8;
typedef int32_t b32;
typedef ptrdiff_t isize;

#define assert(c)  while (!(c)) __builtin_trap()

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

/* ------------ */
DECLARE_SLICE_HEADER(u8_buffer, u8);

static inline void
u8_buffer_append_string(struct u8_buffer* b, struct string s) {
    for (int i = 0; i < s.len; i++) {
        *push(b) = s.data[i];
    }
}

static inline void
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

static inline void
u8_buffer_clear(struct u8_buffer* b) {
    b->len = 0;
}

static inline void
u8_buffer_write_fd(struct u8_buffer* b, int fd) {
    u8* beg = b->data;
    u8* at = beg;
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

struct read_result {
    isize nread;
    b32 eof;
    b32 error;
};

static inline struct read_result
u8_buffer_read_fd(struct u8_buffer* b, int fd) {
    struct read_result r = {0};

    u8* begin = &b->data[b->len];
    u8* at = begin;
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

    r.nread = at - begin;
    b->len += r.nread;
    return r;
}

static inline struct read_result
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


