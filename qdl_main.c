
#include <err.h>
#include <getopt.h>
#include <termios.h>
#include <unistd.h>

#include "patch.h"
#include "qdl.h"
#include "ufs.h"

#include "python_logging.h"

static void print_usage(void) {
  extern const char *__progname;
  log_msg(log_error,
          "%s [--debug] [--storage <emmc|ufs>] [--finalize-provisioning] "
          "[--include <PATH>] <prog.mbn> [<program> <patch> ...]\n",
          __progname);
}

int main(int argc, char **argv) {
  char *prog_mbn, *storage = "ufs";
  char *incdir = NULL;
  int type;
  int ret;
  int opt;
  bool qdl_finalize_provisioning = false;
  struct qdl_device qdl;

  static struct option options[] = {
      {"debug", no_argument, 0, 'd'},
      {"include", required_argument, 0, 'i'},
      {"finalize-provisioning", no_argument, 0, 'l'},
      {"storage", required_argument, 0, 's'},
      {0, 0, 0, 0}};

  while ((opt = getopt_long(argc, argv, "di:", options, NULL)) != -1) {
    switch (opt) {
    case 'd':
      qdl_debug = true;
      break;
    case 'i':
      incdir = optarg;
      break;
    case 'l':
      qdl_finalize_provisioning = true;
      break;
    case 's':
      storage = optarg;
      break;
    default:
      print_usage();
      return 1;
    }
  }

  /* at least 2 non optional args required */
  if ((optind + 2) > argc) {
    print_usage();
    return 1;
  }

  prog_mbn = argv[optind++];

  do {
    type = detect_type(argv[optind]);
    if (type < 0 || type == QDL_FILE_UNKNOWN)
      errx(1, "failed to detect file type of %s\n", argv[optind]);

    switch (type) {
    case QDL_FILE_PATCH:
      ret = patch_load(argv[optind]);
      if (ret < 0)
        errx(1, "patch_load %s failed", argv[optind]);
      break;
    case QDL_FILE_PROGRAM:
      ret = program_load(argv[optind]);
      if (ret < 0)
        errx(1, "program_load %s failed", argv[optind]);
      break;
    case QDL_FILE_UFS:
      ret = ufs_load(argv[optind], qdl_finalize_provisioning);
      if (ret < 0)
        errx(1, "ufs_load %s failed", argv[optind]);
      break;
    default:
      errx(1, "%s type not yet supported", argv[optind]);
      break;
    }
  } while (++optind < argc);

  libusb_init(NULL);
  ret = find_device(&qdl);
  if (ret) {
    libusb_exit(NULL);
    return 1;
  }

  log_msg(log_info, "Found device\n");

  ret = sahara_run(&qdl, prog_mbn);
  if (ret < 0) {
    libusb_exit(NULL);
    return 1;
  }

  log_msg(log_info, "Ran Sahara, all good\n");

  ret = firehose_run(&qdl, incdir, storage);
  if (ret < 0) {
    libusb_exit(NULL);
    return 1;
  }
  log_msg(log_info, "Ran Firehose, we're done!\n");

  return 0;
}
