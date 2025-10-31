/*
 * unixcopy-stdlib.c
 *
 * Simple file copy using the C standard library.
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

  /* Open files using C standard library */
  FILE *fsrc = fopen(src_path, "rb");
  if (!fsrc) {
    fprintf(stderr, "Failed to open source '%s': %s\n", src_path, strerror(errno));
    return EXIT_FAILURE;
  }

  FILE *fdst = fopen(dst_path, "wb");
  if (!fdst) {
    fprintf(stderr, "Failed to open/create destination '%s': %s\n", dst_path, strerror(errno));
    fclose(fsrc);
    return EXIT_FAILURE;
  }

  /* If destination did not exist before, try to set permissions to 0644 */
  if (!dst_exists) {
    if (chmod(dst_path, 0644) < 0) {
      /* non-fatal; warn and continue */
      fprintf(stderr, "Warning: chmod %s failed: %s\n", dst_path, strerror(errno));
    }
  }

  /* Validate buffer size */
  if (bufsize <= 0) {
    fprintf(stderr, "Buffer size must be positive.\n");
    fclose(fsrc);
    fclose(fdst);
    return EXIT_FAILURE;
  }

  char *buf = malloc((size_t)bufsize);
  if (!buf) {
    fprintf(stderr, "Failed to allocate buffer of size %ld bytes.\n", bufsize);
    fclose(fsrc);
    fclose(fdst);
    return EXIT_FAILURE;
  }

  /* Copy loop using fread/fwrite, handling short writes */
  size_t nread;
  while ((nread = fread(buf, (size_t)bufsize, 1, fsrc)) > 0) {
    size_t written_total = 0;
    while (written_total < nread) {
      size_t nw = fwrite(buf + written_total, nread - written_total, 1, fdst);
      if (nw == 0) {
        if (ferror(fdst)) {
          fprintf(stderr, "Write error to '%s': %s\n", dst_path, strerror(errno));
          free(buf);
          fclose(fsrc);
          fclose(fdst);
          return EXIT_FAILURE;
        }
        /* If no error but zero written, treat as error to avoid infinite loop */
        fprintf(stderr, "Unexpected short write to '%s'.\n", dst_path);
        free(buf);
        fclose(fsrc);
        fclose(fdst);
        return EXIT_FAILURE;
      }
      written_total += nw;
    }
  }

  if (ferror(fsrc)) {
    fprintf(stderr, "Read error from '%s': %s\n", src_path, strerror(errno));
    free(buf);
    fclose(fsrc);
    fclose(fdst);
    return EXIT_FAILURE;
  }

  /* Flush and optionally fsync to ensure data is on disk */
  if (fflush(fdst) != 0) {
    fprintf(stderr, "Warning: fflush failed on '%s': %s\n", dst_path, strerror(errno));
  } else {
    if (fsync(fileno(fdst)) < 0) {
      fprintf(stderr, "Warning: fsync failed on '%s': %s\n", dst_path, strerror(errno));
    }
  }

  free(buf);
  if (fclose(fsrc) == EOF) {
    fprintf(stderr, "Warning: closing source '%s' failed: %s\n", src_path, strerror(errno));
  }
  if (fclose(fdst) == EOF) {
    fprintf(stderr, "Warning: closing destination '%s' failed: %s\n", dst_path, strerror(errno));
  }

  return EXIT_SUCCESS;
}