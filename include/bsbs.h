/*
MIT License

Copyright (c) Derek "burlindw" Burlingame

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef BSBS_H
#define BSBS_H

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>
#include <stdnoreturn.h>

/// The name or path used to invoke the C compiler. This is set automatically
/// by `bsbs_init()` to either the value of the `CC` environment variable or
/// the string "cc".
extern char const *bsbs_cc;

/// A build step that runs a command.
typedef struct bsbs_run bsbs_run;

/// A callback for generating files. `fp` is a open for writing and truncated.
typedef void (*bsbs_genfn)(void *userdata, FILE *fp);

/// Perform the initial setup of the build system. This will also attempt to
/// recomile and reexecute the build script if the 
void bsbs_init(size_t total_memory, char const *arg0, char const *file);

/// Prints a formatted message to stderr and exits with `EXIT_FAILURE`.
noreturn void bsbs_die(char const *format, ...);

/// Allocate enough memory from the global arena allocator to hold `count`
/// elements of `size` bytes each. This function NEVER returns null; instead
/// the build process will immediately exit if it runs out of memory.
///
/// The macro `bsbs_alloc(Type, count)` provides a convenient wrapper for this.
void *bsbs_alloc_aligned_array(size_t size, size_t align, size_t count);

void *bsbs_alloc_save(void);

void bsbs_alloc_load(void *save);

bsbs_run *bsbs_run_create(void);

void bsbs_run_add_arg(bsbs_run *run, char const *arg);

void bsbs_run_add_input_file(bsbs_run *run, char const *filepath);

void bsbs_run_add_output_file(bsbs_run *run, char const *filepath);

void bsbs_run_add_input_file_arg(bsbs_run *run, char const *filepath);

void bsbs_run_add_output_file_arg(bsbs_run *run, char const *filepath);

void bsbs_run_execute(bsbs_run const *run);

void bsbs_generate(char const *filepath, void *userdata, bsbs_genfn callback);

#define bsbs_alloc(type, count) \
  (type*)bsbs_alloc_aligned_array(sizeof(type), alignof(type), (count))

#ifdef BSBS_IMPL

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char const *bsbs_cc = "cc";

static void *_bsbs_alloc_base;
static void *_bsbs_alloc_ptr;

typedef struct _bsbs_list_node {
  struct _bsbs_list_node *next;
  char content[];
} _bsbs_list_node;

typedef struct _bsbs_list {
  size_t count;
  _bsbs_list_node *head;
  _bsbs_list_node *tail;
} _bsbs_list;

struct bsbs_run {
  _bsbs_list args;
  _bsbs_list inputs;
  _bsbs_list outputs;
};

static void _bsbs_list_append(_bsbs_list *list, char const *content) {
  size_t length = strlen(content);
  bsbs_alloc(char, length + 1);

  _bsbs_list_node *node = bsbs_alloc(_bsbs_list_node, 1);
  node->next = NULL;
  strcpy(node->content, content);

  if (list->head == NULL) {
    list->head = node;
    list->tail = node;
    list->count = 1;
  } else {
    list->tail->next = node;
    list->tail = node;
    list->count += 1;
  }
}

// Convert a list to a null terminated array of strings.
static char **_bsbs_list_to_ntarray(_bsbs_list const *list) {
  char **array = bsbs_alloc(char*, list->count + 1);

  size_t i = 0;
  _bsbs_list_node *n = list->head;
  while (i < list->count && n != NULL) {
    array[i] = n->content;
    i++;
    n = n->next;
  }

  assert(i == list->count);
  assert(n == NULL);

  array[i] = NULL;
  return array;
}

/// Returns 0 if the file is not found.
static time_t _bsbs_mtime(char const *filepath) {
  struct stat attr;
  if (stat(filepath, &attr) == 0) {
    return attr.st_mtime;
  } else if (errno == ENOENT) {
    return 0;
  } else {
    bsbs_die("failed to get mtime for '%s': %s\n", filepath, strerror(errno));
  }
}

static bool _bsbs_requires_rebuild(_bsbs_list const *inputs, _bsbs_list const *outputs) {
  // If there is no output, then there is no reason to do anything.
  if (outputs->count == 0) return false;
  assert(outputs->head != NULL);

  // Find the earliest modification time of the outputs.
  time_t outtime = _bsbs_mtime(outputs->head->content);
  for (_bsbs_list_node *node = outputs->head->next; node; node = node->next) {
    time_t mtime = _bsbs_mtime(node->content);
    if (mtime < outtime) outtime = mtime;
  }

  // If `outtime` is zero here, then at least one of the outputs is entirely
  // missing and we need to rebuild regardless of the freshness of the inputs.
  if (outtime == 0) return true;

  // Check all the inputs to see if any of them have been modified since the
  // outputs were created. We also check to make sure that the inputs exist.
  for (_bsbs_list_node *node = inputs->head; node; node = node->next) {
    time_t mtime = _bsbs_mtime(node->content);
    if (mtime > outtime) {
      return true;
    } else if (mtime == 0) {
      bsbs_die("input file '%s' is missing\n", node->content);
    }
  }
  
  return false;
}

static void _bsbs_run_execute_unchecked(bsbs_run const *run) {
  char **args = _bsbs_list_to_ntarray(&run->args);

  int pid = fork();
  if (pid == 0) {
    // Child proces
    execvp(args[0], args); // noreturn if successful
    bsbs_die("exec failed: %s\n", strerror(errno));
  } else if (pid < 0) {
    bsbs_die("fork failed: %s\n", strerror(errno));
  }

  int status;
  if (waitpid(pid, &status, 0) < 0) {
    bsbs_die("wait on '%s' failed: %s\n", args[0], strerror(errno));
  } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    bsbs_die("'%s' exitied abnormally\n", args[0]);
  }
}

void bsbs_init(size_t total_memory, char const* arg0, char const* file) {
  // Set up the global arena allocator.
  _bsbs_alloc_base = malloc(total_memory);
  if (!_bsbs_alloc_base) bsbs_die("malloc failed\n");
  _bsbs_alloc_ptr = _bsbs_alloc_base + total_memory;

  // Get the C compiler name.
  char const *cc = getenv("CC");
  bsbs_cc = cc ? cc : "cc";

  // Recompile and rerun this build script if necessary.
  void *save = bsbs_alloc_save();

  bsbs_run *recompile = bsbs_run_create();
  bsbs_run_add_arg(recompile, bsbs_cc);
  bsbs_run_add_input_file_arg(recompile, file);
  bsbs_run_add_arg(recompile, "-o");
  bsbs_run_add_output_file_arg(recompile, arg0);

  if (_bsbs_requires_rebuild(&recompile->inputs, &recompile->outputs)) {
    _bsbs_run_execute_unchecked(recompile);
    execl(arg0, arg0); // noreturn if successful
    bsbs_die("failed to rerun recompiled builder\n");
  }

  bsbs_alloc_load(save);
}

noreturn void bsbs_die(char const *format, ...) {
  va_list ap;
  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
  exit(EXIT_FAILURE);
}

void *bsbs_alloc_aligned_array(size_t size, size_t align, size_t count) {
  size_t mask = align - 1;
  assert((align & mask) == 0 && align != 0);
  assert((size & mask) == 0 && size != 0);

  uintptr_t ptr = (uintptr_t)_bsbs_alloc_ptr;
  ptr &= ~((uintptr_t)mask);

  uintptr_t nbytes = size * count;
  if (nbytes / size != count) {
    bsbs_die("overflow (%zu * %zu)\n", size, count);
  } else if (nbytes > ptr) {
    bsbs_die("underflow (%zu - %zu)\n", ptr, nbytes);
  }

  _bsbs_alloc_ptr = (void*)(ptr - nbytes);
  if (_bsbs_alloc_ptr < _bsbs_alloc_base) {
    bsbs_die("out of memory\n");
  }

  return _bsbs_alloc_ptr;
}

void *bsbs_alloc_save(void) {
  return _bsbs_alloc_ptr;
}

void bsbs_alloc_load(void *save) {
  assert(save >= _bsbs_alloc_base);
  assert(save >= _bsbs_alloc_ptr);
  _bsbs_alloc_ptr = save;
}

bsbs_run *bsbs_run_create(void) {
  return bsbs_alloc(bsbs_run, 1);
}

void bsbs_run_add_arg(bsbs_run *run, char const *arg) {
  _bsbs_list_append(&run->args, arg);
}

void bsbs_run_add_input_file(bsbs_run *run, char const *filepath) {
  _bsbs_list_append(&run->inputs, filepath);
}

void bsbs_run_add_output_file(bsbs_run *run, char const *filepath) {
  _bsbs_list_append(&run->outputs, filepath);
}

void bsbs_run_add_input_file_arg(bsbs_run *run, char const *filepath) {
  _bsbs_list_append(&run->args, filepath);
  _bsbs_list_append(&run->inputs, filepath);
}

void bsbs_run_add_output_file_arg(bsbs_run *run, char const *filepath) {
  _bsbs_list_append(&run->args, filepath);
  _bsbs_list_append(&run->outputs, filepath);
}

void bsbs_run_execute(bsbs_run const *run) {
  if (_bsbs_requires_rebuild(&run->inputs, &run->outputs)) {
    _bsbs_run_execute_unchecked(run);
  }
}

void bsbs_generate(char const *filepath, void *userdata, bsbs_genfn callback) {
  int fd = openat(AT_FDCWD, filepath, O_EXCL | O_CREAT | O_WRONLY);
  if (fd < 0) {
    if (errno == EEXIST) return;
    bsbs_die("failed to create '%s': %s\n", filepath, strerror(errno));
  }

  FILE *fp = fdopen(fd, "w");
  if (fp == NULL) {
    bsbs_die("failed to create file '%s': %s\n", filepath, strerror(errno));
  } 

  callback(userdata, fp);
  fflush(fp);
  fclose(fp);
}

#endif // BSBS_IMPL

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // BSBS_H

