#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <pwd.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int err_code;
static int file_count = 0;
static bool count_only = false;
static bool human_readable = false;


void handle_error(char * fullname, char * action);
bool test_file(char * pathandname);
bool is_dir(char * pathandname);
const char * ftype_to_str(mode_t mode);
void list_file(char * pathandname, char * name, bool list_long);
void list_dir(char * dirname, bool list_long, bool list_all, bool recursive);

#define NOT_YET_IMPLEMENTED(msg)\
do {
  \
  printf("Not yet implemented: "
    msg "\n");\
  exit(255);\
} while (0)

/*
 * PRINT_ERROR: This can be used to print the cause of an error returned by a
 * system call. It can help with debugging and reporting error causes to
 * the user. Example usage:
 *     if ( error_condition ) {
 *        PRINT_ERROR();
 *     }
 */
#define PRINT_ERROR(progname, what_happened, pathandname)\
do {
  \
  printf("%s: %s %s: %s\n", progname, what_happened, pathandname, \
    strerror(errno));\
} while (0)

/* PRINT_PERM_CHAR:
 *
 * This will be useful for -l permission printing.  It prints the given
 * 'ch' if the permission exists, or "-" otherwise.
 * Example usage:
 *     PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
 */
#define PRINT_PERM_CHAR(mode, mask, ch) printf("%s", (mode & mask) ? ch : "-");

/*
 * Get username for uid. Return 1 on failure, 0 otherwise.
 */
static int uname_for_uid(uid_t uid, char * buf, size_t buflen) {
  struct passwd * p = getpwuid(uid);
  if (p == NULL) {
    return 1;
  }
  strncpy(buf, p -> pw_name, buflen);
  return 0;
}

/*
 * Get group name for gid. Return 1 on failure, 0 otherwise.
 */
static int group_for_gid(gid_t gid, char * buf, size_t buflen) {
  struct group * g = getgrgid(gid);
  if (g == NULL) {
    return 1;
  }
  strncpy(buf, g -> gr_name, buflen);
  return 0;
}

/*
 * Format the supplied `struct timespec` in `ts` (e.g., from `stat.st_mtime`) as a
 * string in `char *out`. Returns the length of the formatted string (see, `man
 * 3 strftime`).
 */
static size_t date_string(struct timespec * ts, char * out, size_t len) {
  struct timespec now;
  timespec_get( & now, TIME_UTC);
  struct tm * t = localtime( & ts -> tv_sec);
  if (now.tv_sec < ts -> tv_sec) {
    // Future time, treat with care.
    return strftime(out, len, "%b %e %Y", t);
  } else {
    time_t difference = now.tv_sec - ts -> tv_sec;
    if (difference < 31556952 ull) {
      return strftime(out, len, "%b %e %H:%M", t);
    } else {
      return strftime(out, len, "%b %e %Y", t);
    }
  }
}

/*
 * Print help message and exit.
 */
static void help() {
  printf("ls: List files\n");
  printf("usage: ./ls [option] [file]\n");
  printf("-a -> don't ignore hidden files\n");
  printf("-l -> print long listing format, will show symlinks\n");
  printf("-R -> list subdirectories recursively\n");
  printf("-n -> count files only, wont show files\n");
  printf("--help -> display this message and exit\n\n");
  printf("exit status:\n");
  printf("0 -> ok\n");
  printf("64 -> error occured\n");
  printf("72 -> file not found\n");
  printf("80 -> permission denied\n");
  printf("88 -> file not found and permission denied\n");
  printf("96 -> user/group lookup didn't work\n");
  exit(0);
}

static void format_size_human(long long size, char * buffer, size_t buf_size) {
  const char * units[] = {
    "B",
    "K",
    "M",
    "G",
    "T",
    "P",
    "E"
  };
  int unit_index = 0;
  double human_size = size;

  while (human_size >= 1024 && unit_index < 6) {
    human_size /= 1024;
    unit_index++;
  }

  if (unit_index == 0) {
    snprintf(buffer, buf_size, "%lld", size);
  } else {
    snprintf(buffer, buf_size, "%.1f%s", human_size, units[unit_index]);
  }
}

/*
 * call this when there's been an error.
 * The function should:
 * - print a suitable error message (this is already implemented)
 * - set appropriate bits in err_code
 */
void handle_error(char * what_happened, char * fullname) {
  int saved_errno = errno;

  PRINT_ERROR("ls", what_happened, fullname);

  err_code |= 64;

  if (saved_errno == ENOENT) {
    err_code |= 8; // file not found
  } else if (saved_errno == EACCES || saved_errno == EPERM) {
    err_code |= 16; // permission denied
  } else { // everything else
    err_code |= 32;
  }

  return;
}

/*
 * test_file():
 * test whether stat() returns successfully and if not, handle error.
 * Use this to test for whether a file or dir exists
 */
bool test_file(char * pathandname) {
  struct stat sb;
  if (lstat(pathandname, & sb)) {
    handle_error("cannot access", pathandname);
    return false;
  }
  return true;
}

/*
 * is_dir(): tests whether the argument refers to a directory.
 * precondition: test_file() returns true. that is, call this function
 * only if test_file(pathandname) returned true.
 */
bool is_dir(char * pathandname) {
  struct stat sb;
  if (lstat(pathandname, & sb)) {
    handle_error("No File Found", "is_dir");
    return false;
  }
  return S_ISDIR(sb.st_mode);
}

/* convert the mode field in a struct stat to a file type, for -l printing */
const char * ftype_to_str(mode_t mode) {
  if (S_ISDIR(mode)) {
    return "d";
  }
  if (S_ISREG(mode)) {
    return "-";
  }
  if (S_ISLNK(mode)) {
    return "l";
  }
  return "?";
}

/* list_file():
 * implement the logic for listing a single file.
 * This function takes:
 *   - pathandname: the directory name plus the file name.
 *   - name: just the name "component".
 *   - list_long: a flag indicated whether the printout should be in
 *   long mode.
 */
void list_file(char * pathandname, char * name, bool list_long) {
  if (count_only) {
    file_count++;
    return;
  }

  if (list_long) {
    struct stat sb;

    if (lstat(pathandname, & sb) == -1) {
      handle_error("cannot access", pathandname);
      return;
    }

    printf("%s", ftype_to_str(sb.st_mode));

    // for user permissions
    PRINT_PERM_CHAR(sb.st_mode, S_IRUSR, "r");
    PRINT_PERM_CHAR(sb.st_mode, S_IWUSR, "w");
    PRINT_PERM_CHAR(sb.st_mode, S_IXUSR, "x");

    // for group permissions
    PRINT_PERM_CHAR(sb.st_mode, S_IRGRP, "r");
    PRINT_PERM_CHAR(sb.st_mode, S_IWGRP, "w");
    PRINT_PERM_CHAR(sb.st_mode, S_IXGRP, "x");

    // for other permissions
    PRINT_PERM_CHAR(sb.st_mode, S_IROTH, "r");
    PRINT_PERM_CHAR(sb.st_mode, S_IWOTH, "w");
    PRINT_PERM_CHAR(sb.st_mode, S_IXOTH, "x");

    printf(" %ld", (long) sb.st_nlink);

    // printing the owner name
    char owner[32];
    if (uname_for_uid(sb.st_uid, owner, sizeof(owner)) == 0) {
      printf(" %-8s", owner);
    } else {
      printf(" %-8d", sb.st_uid);
      err_code |= (1 << 6) | (1 << 5);
    }

    // group name
    char group[32];
    if (group_for_gid(sb.st_gid, group, sizeof(group)) == 0) {
      printf(" %-8s", group);
    } else {
      printf(" %-8d", sb.st_gid);
      err_code |= (1 << 6) | (1 << 5);
    }

    // pringing file size
    if (human_readable) {
      char hr_size[16];
      format_size_human((long long) sb.st_size, hr_size, sizeof(hr_size));
      printf(" %5s", hr_size);
    } else {
      printf(" %8lld", (long long) sb.st_size);
    }

    // modification time
    char mod_time[64];
    date_string( & sb.st_mtim, mod_time, sizeof(mod_time));
    printf(" %s", mod_time);

    // the file name
    if (S_ISLNK(sb.st_mode)) {
      char target[1024];
      ssize_t target_len = readlink(pathandname, target, sizeof(target) - 1);
      if (target_len != -1) {
        target[target_len] = '\0';
        printf(" %s -> %s\n", name, target);
      } else {
        printf(" %s -> ?\n", name); // if we can't read the link
      }
    } else {
      printf(" %s", name);

      // adding / for the directories
      if (S_ISDIR(sb.st_mode) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
        printf("/");
      }

      printf("\n");
    }

  } else {
    if (!test_file(pathandname)) {
      return;
    }

    printf("%s", name);

    // making sure if it isn't "." or ".." case
    if (is_dir(pathandname) && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
      printf("/");
    }

    printf("\n");
  }
}

/* list_dir():
 * implement the logic for listing a directory.
 * This function takes:
    printf("");
 *    - dirname: the name of the directory
 *    - list_long: should the directory be listed in long mode?
 *    - list_all: are we in "-a" mode?
 *    - recursive: are we supposed to list sub-directories?
 */
void list_dir(char * dirname, bool list_long, bool list_all, bool recursive) {
  // checking if recursive flag is set
  if (recursive) {
    printf("%s:\n", dirname);
  }

  // open dir
  DIR * dir = opendir(dirname);
  if (dir == NULL) {
    handle_error("Error opening directory", dirname);
    return;
  }

  struct dirent * entry;
  char subdir_list[1024][256]; // will be storing subdir names
  int subdir_count = 0; // count of how many subdirs stored

  while ((entry = readdir(dir)) != NULL) {
    // skip hidden files
    if (!list_all && entry -> d_name[0] == '.') {
      continue; // go to next file
    }

    // building the path here
    char fullpath[1024];
    snprintf(fullpath, sizeof(fullpath), "%s/%s", dirname, entry -> d_name);

    // list the file
    list_file(fullpath, entry -> d_name, list_long);

    if (recursive) {
      // skipping "." and ".."
      if (strcmp(entry -> d_name, ".") == 0 || strcmp(entry -> d_name, "..") == 0) {
        continue;
      }

      struct stat sb;
      if (lstat(fullpath, & sb) == 0 && S_ISDIR(sb.st_mode)) {
        // store directory
        if (subdir_count < 1024) {
          strncpy(subdir_list[subdir_count], fullpath, sizeof(subdir_list[0]) - 1);
          subdir_list[subdir_count][sizeof(subdir_list[0]) - 1] = '\0';
          subdir_count++;
        }
      }
    }
  }

  closedir(dir);

  if (recursive && subdir_count > 0) {
    for (int index = 0; index < subdir_count; index++) {
      printf("\n");
      list_dir(subdir_list[index], list_long, list_all, recursive);
    }
  }
}

int main(int argc, char * argv[]) {
  // This needs to be int since C does not specify whether char is signed or
  // unsigned.
  int opt;
  err_code = 0;
  bool list_long = false, list_all = false, recursive = false;
  count_only = false;
  human_readable = false;

  // We make use of getopt_long for argument parsing, and this
  // (single-element) array is used as input to that function. The `struct
  // option` helps us parse arguments of the form `--FOO`. Refer to `man 3
  // getopt_long` for more information.
  struct option opts[] = {
    {
      .name = "help", .has_arg = 0, .flag = NULL, .val = '\a'
    }
  };

  // This loop is used for argument parsing. Refer to `man 3 getopt_long` to
  // better understand what is going on here.
  while ((opt = getopt_long(argc, argv, "1alRnh", opts, NULL)) != -1) {
    switch (opt) {
    case '\a':
      // Handle the case that the user passed in `--help`. (In the
      // long argument array above, we used '\a' to indicate this
      // case.)
      help();
      break;
    case '1':
      // Safe to ignore since this is default behavior for our version
      // of ls.
      break;
    case 'a':
      list_all = true;
      break;
      // cases that the user enters "-l" or "-R"
    case 'l':
      list_long = true;
      break;
    case 'R':
      recursive = true;
      break;
    case 'n':
      count_only = true;
      break;
    case 'h':
      human_readable = true;
      break;
    default:
      printf("Unimplemented flag %d\n", opt);
      break;
    }
  }

  file_count = 0;

  if (optind == argc) {
    if (recursive) {
      printf(".:\n");
    }
    list_dir(".", list_long, list_all, recursive);
  } else {
    for (int index = optind; index < argc; index++) {
      char * arg = argv[index];

      // check to see if the file exists, continue if not
      if (!test_file(arg)) {
        continue;
      }
      // if it's a dir case
      if (is_dir(arg)) {
        // for multiple arguments
        if (argc - optind > 1) {
          printf("%s:\n", arg);
        }

        list_dir(arg, list_long, list_all, recursive);

        if (index + 1 < argc) {
          printf("\n");
        }
        // if it's a normal file
      } else {
        list_file(arg, arg, list_long);
      }
    }
  }

  if (count_only) {
    printf("%d\n", file_count);
  }

  exit(err_code);
}
