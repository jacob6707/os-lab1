/*
 * unixcopy.c
 *
 * Simple file copy using UNIX system calls.
 * Usage: unixcopy [-b bufsize] [-h] source_file dest_file
 *
 * Options:
 *   -b N    set buffer size in bytes (positive integer)
 *   -h      display this help and exit
 *
 * The program performs checks for argument errors and reports exact problems.
 */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static void usage(const char *prog) {
  fprintf(stderr,
    "Usage: %s [-b BUF_SIZE] [-h] SOURCE_FILE DEST_FILE\n"
    "  -b BUF_SIZE   set buffer size in bytes (positive integer)\n"
    "  -h            show this help message and exit\n",
    prog);
}

static long parse_bufsize(const char *s, int *err) {
  char *end;
  errno = 0;
  long v = strtol(s, &end, 10);
  if (s[0] == '\0' || *end != '\0') {
    *err = 1;
    return 0;
  }
  if ((errno == ERANGE && (v == LONG_MAX || v == LONG_MIN)) || v <= 0) {
    *err = 1;
    return 0;
  }
  *err = 0;
  return v;
}

int main(int argc, char *argv[]) {
  const char *prog = argv[0];
  int opt;
  long bufsize = 4096; /* default 4KB */
  int parse_err = 0;

  while ((opt = getopt(argc, argv, "b:h")) != -1) {
    switch (opt) {
    case 'b':
      bufsize = parse_bufsize(optarg, &parse_err);
      if (parse_err) {
        fprintf(stderr, "Invalid buffer size: '%s' - must be a positive integer\n", optarg);
        return EXIT_FAILURE;
      }
      break;
    case 'h':
      usage(prog);
      return EXIT_SUCCESS;
    case '?':
    default:
      if (optopt == 'b')
        fprintf(stderr, "Option -%c requires an argument.\n", optopt);
      else
        fprintf(stderr, "Unknown option `-%c'.\n", optopt);
      usage(prog);
      return EXIT_FAILURE;
    }
  }

  if (argc - optind < 2) {
    fprintf(stderr, "Missing source and/or destination file. Expecting 2 arguments.\n");
    usage(prog);
    return EXIT_FAILURE;
  }
  if (argc - optind > 2) {
    fprintf(stderr, "Too many arguments. Expecting exactly 2 (source and destination).\n");
    usage(prog);
    return EXIT_FAILURE;
  }

  const char *src_path = argv[optind];
  const char *dst_path = argv[optind + 1];

  /* Basic checks on source and destination */
  struct stat st_src;
  if (stat(src_path, &st_src) < 0) {
    fprintf(stderr, "Cannot stat source '%s': %s\n", src_path, strerror(errno));
    return EXIT_FAILURE;
  }
  if (S_ISDIR(st_src.st_mode)) {
    fprintf(stderr, "Source '%s' is a directory. Expected a regular file.\n", src_path);
    return EXIT_FAILURE;
  }

  struct stat st_dst;
  int dst_exists = (stat(dst_path, &st_dst) == 0);
  if (dst_exists && S_ISDIR(st_dst.st_mode)) {
    fprintf(stderr, "Destination '%s' is a directory. Provide a file path.\n", dst_path);
    return EXIT_FAILURE;
  }

  /* If destination exists, check if it's the same file as source */
  if (dst_exists) {
    if (st_src.st_dev == st_dst.st_dev && st_src.st_ino == st_dst.st_ino) {
      fprintf(stderr, "Source and destination refer to the same file ('%s').\n", src_path);
      return EXIT_FAILURE;
    }
  }

  /* Open files */
  int fd_src = open(src_path, O_RDONLY);
  if (fd_src < 0) {
    fprintf(stderr, "Failed to open source '%s': %s\n", src_path, strerror(errno));
    return EXIT_FAILURE;
  }

  int fd_dst = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd_dst < 0) {
    fprintf(stderr, "Failed to open/create destination '%s': %s\n", dst_path, strerror(errno));
    close(fd_src);
    return EXIT_FAILURE;
  }

  /* Allocate buffer */
  if (bufsize <= 0) {
    fprintf(stderr, "Buffer size must be positive.\n");
    close(fd_src);
    close(fd_dst);
    return EXIT_FAILURE;
  }

  char *buf = malloc((size_t)bufsize);
  if (!buf) {
    fprintf(stderr, "Failed to allocate buffer of size %ld bytes.\n", bufsize);
    close(fd_src);
    close(fd_dst);
    return EXIT_FAILURE;
  }

  /* Copy loop using read/write */
  ssize_t nread;
  ssize_t nwritten;
  while ((nread = read(fd_src, buf, (size_t)bufsize)) > 0) {
    char *outp = buf;
    ssize_t remaining = nread;
    while (remaining > 0) {
      nwritten = write(fd_dst, outp, (size_t)remaining);
      if (nwritten < 0) {
        fprintf(stderr, "Write error to '%s': %s\n", dst_path, strerror(errno));
        free(buf);
        close(fd_src);
        close(fd_dst);
        return EXIT_FAILURE;
      }
      remaining -= nwritten;
      outp += nwritten;
    }
  }
  if (nread < 0) {
    fprintf(stderr, "Read error from '%s': %s\n", src_path, strerror(errno));
    free(buf);
    close(fd_src);
    close(fd_dst);
    return EXIT_FAILURE;
  }

  /* Optional: flush to disk */
  if (fsync(fd_dst) < 0) {
    fprintf(stderr, "Warning: fsync failed on '%s': %s\n", dst_path, strerror(errno));
  }

  free(buf);
  if (close(fd_src) < 0) {
    fprintf(stderr, "Warning: closing source '%s' failed: %s\n", src_path, strerror(errno));
  }
  if (close(fd_dst) < 0) {
    fprintf(stderr, "Warning: closing destination '%s' failed: %s\n", dst_path, strerror(errno));
  }

  return EXIT_SUCCESS;
}