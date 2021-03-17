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
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include "patch.h"
#include "qdl.h"
#include "ufs.h"

#include "python_logging.h"

#define MAX_USBFS_BULK_SIZE (16 * 1024)

bool qdl_debug;

int detect_type(const char *xml_file) {
  xmlNode *root;
  xmlDoc *doc;
  xmlNode *node;
  int type = QDL_FILE_UNKNOWN;

  doc = xmlReadFile(xml_file, NULL, 0);
  if (!doc) {
    log_msg(log_error, "[PATCH] failed to parse %s\n", xml_file);
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
    log_msg(log_error, "Could not get device descriptor\n");
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
    log_msg(log_error, "Could not get device config descriptor\n");
    return err;
  }

  for (int i = 0; i < cdesc->bNumInterfaces; i++) {
    struct libusb_interface interface = cdesc->interface[i];

    for (int alt = 0; alt < interface.num_altsetting; alt++) {
      struct libusb_interface_descriptor idesc = interface.altsetting[alt];

      for (int e = 0; e < idesc.bNumEndpoints; e++) {
        struct libusb_endpoint_descriptor edesc = idesc.endpoint[e];

        if ((edesc.bmAttributes & 0x3) != LIBUSB_TRANSFER_TYPE_BULK)
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
    log_msg(log_error, "No USB device found\n");
    libusb_free_device_list(list, 1);
    return -ENOENT;
  }

  for (i = 0; i < cnt; i++) {
    libusb_device *device = list[i];

    bool is_an_sc20 = false;
    if ((err = parse_sc20_device(device, qdl, &intf, &is_an_sc20))) {
      log_msg(log_error, "Could not parse SC20 device\n");
      libusb_free_device_list(list, 1);
      return err;
    }

    if (is_an_sc20) {
      found = device;
      break;
    }
  }

  if (!found) {
    log_msg(log_error, "Device not found");
    return -ENOENT;
  }

  err = libusb_open(found, &qdl->device);
  if (err) {
    log_msg(log_error, "Could not open USB device\n");
    return err;
  }

  libusb_detach_kernel_driver(qdl->device, intf);
  if ((err = libusb_claim_interface(qdl->device, intf))) {
    log_msg(log_error, "Could not claim USB interface");
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
    // log_msg(log_info, "QDL read failed: %d\n", err);
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
      log_msg(log_error, "ERROR: n = %d, errno = %d (%s)\n", n, errno,
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
      log_msg(log_error, "ERROR: bulk write transfer failed: %d\n", err);
      return -1;
    }
    if (n != xfer) {
      log_msg(log_error, "ERROR: n = %d, errno = %d (%s)\n", n, errno,
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
      log_msg(log_error, "ERROR: last bulk write transfer failed\n");
      return -1;
    }
    if (n < 0)
      return n;
  }

  return count;
}
