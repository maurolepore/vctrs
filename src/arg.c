#include "vctrs.h"
#include "utils.h"


// Materialising argument tags ------------------------------------------

#define DEFAULT_ARG_BUF_SIZE 100

static int fill_arg_buffer(struct vctrs_arg* arg,
                           char* buf,
                           r_ssize_t cur_size,
                           r_ssize_t tot_size);

/**
 * This takes a `struct vctrs_arg{}` linked list and calls the
 * recursive function `fill_arg_buffer()`. It allocates a buffer in a
 * RAWSXP of size 100, which is grown by a factor of 1.5 each time the
 * `fill()` methods return a negative value. Returns a character
 * vector of size 1 containing the materialised argument tag.
 */
SEXP vctrs_arg(struct vctrs_arg* arg) {
  r_ssize_t next_size = DEFAULT_ARG_BUF_SIZE;
  r_ssize_t size;

  SEXP buf_holder = PROTECT(R_NilValue);
  char* buf;

  do {
    size = next_size;

    UNPROTECT(1);
    buf_holder = PROTECT(Rf_allocVector(RAWSXP, size));
    buf = (char*) RAW(buf_holder);

    // Reallocate a larger buffer at the next iteration if the current
    // buffer turns out too small
    next_size *= 1.5;
  } while (fill_arg_buffer(arg, buf, 0, size) < 0);

  SEXP out = Rf_mkString(buf);

  UNPROTECT(1);
  return out;
}

/**
 * Takes a `struct vctrs_arg{}` linked list and a buffer and calls the
 * `fill()` method on each of those, recursively. Unless an error
 * occurred, it returns the current size written to the buffer so we
 * can track the remaining memory available in the buffer after
 * recursion.
 */
static int fill_arg_buffer(struct vctrs_arg* arg,
                           char* buf,
                           r_ssize_t cur_size,
                           r_ssize_t tot_size) {
  if (arg->parent) {
    cur_size = fill_arg_buffer(arg->parent, buf, cur_size, tot_size);

    if (cur_size < 0) {
      return cur_size;
    }
  }

  r_ssize_t written = arg->fill(arg, buf + cur_size, tot_size - cur_size);

  if (written < 0) {
    return written;
  } else {
    return cur_size + written;
  }
}


// Objects -------------------------------------------------------------

// Simple wrapper around a `const char*` argument tag

static r_ssize_t wrapper_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining);

struct vctrs_arg_wrapper new_wrapper_arg(struct vctrs_arg* parent, const char* arg) {
  struct vctrs_arg_wrapper wrapper = {
    .iface = {
      .parent = parent,
      .fill = &wrapper_arg_fill
    },
    .arg = arg
  };

  return wrapper;
}

static r_ssize_t wrapper_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining) {
  const char* src = ((struct vctrs_arg_wrapper*) self)->arg;

  size_t len = strlen(src);

  if (len >= remaining) {
    return -1;
  }

  memcpy(buf, src, len);
  buf[len] = '\0';

  return len;
}


// Wrapper around a counter representing the current position of the
// argument

static r_ssize_t counter_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining);

struct vctrs_arg_counter new_counter_arg(struct vctrs_arg* parent,
                                         R_len_t* i,
                                         SEXP* names,
                                         R_len_t* names_i) {
  struct vctrs_arg_counter counter = {
    .iface = {
      .parent = parent,
      .fill = &counter_arg_fill
    },
    .i = i,
    .names = names,
    .names_i = names_i
  };

  return counter;
}

static r_ssize_t counter_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining) {
  struct vctrs_arg_counter* counter = (struct vctrs_arg_counter*) self;
  R_len_t i = *counter->i;

  SEXP names = *counter->names;
  R_len_t names_i = *counter->names_i;

  int len;
  if (r_has_name_at(names, names_i)) {
    // FIXME: Check for syntactic names
    len = snprintf(buf, remaining, "%s", r_chr_get_c_string(names, names_i));
  } else {
    len = snprintf(buf, remaining, "list(...)[[%d]]", i + 1);
  }

  if (len >= remaining) {
    return -1;
  } else {
    return len;
  }
}


// Indexing tag that materialises as `$rhs`. The `$` is only written when
// the arg has a parent.

static r_ssize_t index_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining);
static bool is_empty_arg(struct vctrs_arg* arg);

struct vctrs_arg_wrapper new_index_arg(struct vctrs_arg* parent, const char* arg) {
  struct vctrs_arg_wrapper wrapper = {
    .iface = {
      .parent = parent,
      .fill = &index_arg_fill
    },
    .arg = arg
  };

  return wrapper;
}

static r_ssize_t index_arg_fill(struct vctrs_arg* self, char* buf, r_ssize_t remaining) {
  struct vctrs_arg_wrapper* orig = (struct vctrs_arg_wrapper*) self;

  const char* src = orig->arg;
  size_t len = strlen(src);

  bool child = is_empty_arg(self->parent);

  if (child) {
    ++len;
  }

  if (len >= remaining) {
    return -1;
  }

  if (child) {
    *buf++ = '$';
  }
  memcpy(buf, src, len);
  buf[len] = '\0';

  return len;
}

static bool is_empty_arg(struct vctrs_arg* arg) {
  char tmp[1];
  return arg->fill(arg, tmp, 1) != 0;;
}