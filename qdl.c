/*
 * Copyright (c) 2016-2017, Linaro Ltd.
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
/* #include <linux/usbdevice_fs.h> */
/* #include <linux/usb/ch9.h> */
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libusb.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#include "patch.h"
#include "qdl.h"
#include "ufs.h"

#define MAX_USBFS_BULK_SIZE (16 * 1024)

enum {
  QDL_FILE_UNKNOWN,
  QDL_FILE_PATCH,
  QDL_FILE_PROGRAM,
  QDL_FILE_UFS,
  QDL_FILE_CONTENTS,
};

struct qdl_device {
  libusb_device_handle *device;

  uint8_t in_ep;
  uint8_t out_ep;

  size_t in_maxpktsize;
  size_t out_maxpktsize;
};

bool qdl_debug;

static int detect_type(const char *xml_file) {
  xmlNode *root;
  xmlDoc *doc;
  xmlNode *node;
  int type = QDL_FILE_UNKNOWN;

  doc = xmlReadFile(xml_file, NULL, 0);
  if (!doc) {
    fprintf(stderr, "[PATCH] failed to parse %s\n", xml_file);
    return -EINVAL;
  }

  root = xmlDocGetRootElement(doc);
  if (!xmlStrcmp(root->name, (xmlChar *)"patches")) {
    type = QDL_FILE_PATCH;
  } else if (!xmlStrcmp(root->name, (xmlChar *)"data")) {
    for (node = root->children; node; node = node->next) {
      if (node->type != XML_ELEMENT_NODE)
        continue;
      if (!xmlStrcmp(node->name, (xmlChar *)"program")) {
        type = QDL_FILE_PROGRAM;
        break;
      }
      if (!xmlStrcmp(node->name, (xmlChar *)"ufs")) {
        type = QDL_FILE_UFS;
        break;
      }
    }
  } else if (!xmlStrcmp(root->name, (xmlChar *)"contents")) {
    type = QDL_FILE_CONTENTS;
  }

  xmlFreeDoc(doc);

  return type;
}

int parse_sc20_device(libusb_device *device, struct qdl_device *qdl, int *intf,
                      bool *is_an_sc20) {
  struct libusb_device_descriptor ddesc;
  struct libusb_config_descriptor *cdesc;
  int err;
  uint8_t in;
  uint8_t out;
  size_t in_size;
  size_t out_size;

  if ((err = libusb_get_device_descriptor(device, &ddesc))) {
    fprintf(stderr, "Could not get device descriptor\n");
    return err;
  }

  if (ddesc.idVendor != 0x05C6) {
    *is_an_sc20 = false;
    return 0;
  }

  if (ddesc.idProduct != 0x9008) {
    *is_an_sc20 = false;
    return 0;
  }

  if ((err = libusb_get_active_config_descriptor(device, &cdesc))) {
    fprintf(stderr, "Could not get device config descriptor\n");
    return err;
  }

  for (int i = 0; i < cdesc->bNumInterfaces; i++) {
    struct libusb_interface interface = cdesc->interface[i];

    for (int alt = 0; alt < interface.num_altsetting; alt++) {
      struct libusb_interface_descriptor idesc = interface.altsetting[alt];

      for (int e = 0; e < idesc.bNumEndpoints; e++) {
        struct libusb_endpoint_descriptor edesc = idesc.endpoint[e];

        if ((edesc.bmAttributes & 0x3) != LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK)
          continue;

        if (edesc.bEndpointAddress & LIBUSB_ENDPOINT_IN) {
          in = edesc.bEndpointAddress;
          in_size = edesc.wMaxPacketSize;
        } else {
          out = edesc.bEndpointAddress;
          out_size = edesc.wMaxPacketSize;
        }
      }

      if (idesc.bInterfaceClass != 0xff)
        continue;
      if (idesc.bInterfaceSubClass != 0xff)
        continue;
      if (idesc.bInterfaceProtocol != 0xff && idesc.bInterfaceProtocol != 0x10)
        continue;

      qdl->in_ep = in;
      qdl->out_ep = out;
      qdl->in_maxpktsize = in_size;
      qdl->out_maxpktsize = out_size;

      *intf = idesc.bInterfaceNumber;
      libusb_free_config_descriptor(cdesc);
      *is_an_sc20 = true;
      return 0;
    }
  }

  libusb_free_config_descriptor(cdesc);
  return -ENOENT;
}

int find_device(struct qdl_device *qdl) {
  libusb_device **list;
  libusb_device *found = NULL;
  ssize_t cnt = libusb_get_device_list(NULL, &list);
  ssize_t i = 0;
  int err = 0;
  int intf;

  if (cnt < 0) {
    fprintf(stderr, "No USB device found\n");
    libusb_free_device_list(list, 1);
    return -ENOENT;
  }

  for (i = 0; i < cnt; i++) {
    libusb_device *device = list[i];

    bool is_an_sc20 = false;
    if ((err = parse_sc20_device(device, qdl, &intf, &is_an_sc20))) {
      fprintf(stderr, "Could not parse SC20 device\n");
      libusb_free_device_list(list, 1);
      return err;
    }

    if (is_an_sc20) {
      found = device;
      break;
    }
  }

  if (!found) {
    fprintf(stderr, "Device not found");
    return -ENOENT;
  }

  err = libusb_open(found, &qdl->device);
  if (err) {
    fprintf(stderr, "Could not open USB device\n");
    return err;
  }

  libusb_detach_kernel_driver(qdl->device, intf);
  if ((err = libusb_claim_interface(qdl->device, intf))) {
    fprintf(stderr, "Could not claim USB interface");
    return err;
  }

  libusb_free_device_list(list, 1);
  return 0;
}

int qdl_read(struct qdl_device *qdl, void *buf, size_t len,
             unsigned int timeout) {
  int n;
  int err =
      libusb_bulk_transfer(qdl->device, qdl->in_ep, buf, len, &n, timeout);
  if (err) {
    // printf("QDL read failed: %d\n", err);
    return -1;
  }
  return n;
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, bool eot) {
  unsigned char *data = (unsigned char *)buf;
  unsigned count = 0;
  size_t len_orig = len;
  int n;
  int err;

  if (len == 0) {
    n = libusb_bulk_transfer(qdl->device, qdl->out_ep, data, 0, NULL, 1000);
    if (n != 0) {
      fprintf(stderr, "ERROR: n = %d, errno = %d (%s)\n", n, errno,
              strerror(errno));
      return -1;
    }
    return 0;
  }

  while (len > 0) {
    int xfer;
    xfer = (len > qdl->out_maxpktsize) ? qdl->out_maxpktsize : len;

    err = libusb_bulk_transfer(qdl->device, qdl->out_ep, data, xfer, &n, 1000);
    if (err != 0) {
      fprintf(stderr, "ERROR: bulk write transfer failed: %d\n", err);
      return -1;
    }
    if (n != xfer) {
      fprintf(stderr, "ERROR: n = %d, errno = %d (%s)\n", n, errno,
              strerror(errno));
      return -1;
    }
    count += xfer;
    len -= xfer;
    data += xfer;
  }

  if (eot && (len_orig % qdl->out_maxpktsize) == 0) {
    err = libusb_bulk_transfer(qdl->device, qdl->out_ep, NULL, 0, &n, 1000);
    if (err != 0) {
      fprintf(stderr, "ERROR: last bulk write transfer failed\n");
      return -1;
    }
    if (n < 0)
      return n;
  }

  return count;
}

static void print_usage(void) {
  extern const char *__progname;
  fprintf(stderr,
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

  printf("Found device\n");

  ret = sahara_run(&qdl, prog_mbn);
  if (ret < 0) {
    libusb_exit(NULL);
    return 1;
  }

  printf("Ran Sahara, all good\n");

  ret = firehose_run(&qdl, incdir, storage);
  if (ret < 0) {
    libusb_exit(NULL);
    return 1;
  }
  printf("Ran Firehose, we're done!\n");

  return 0;
}
