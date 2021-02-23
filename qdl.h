#ifndef __QDL_H__
#define __QDL_H__

#include <stdbool.h>
#include <stdint.h>

#include <libusb.h>

#include "patch.h"
#include "program.h"
#include <libxml/tree.h>

struct qdl_device {
  libusb_device_handle *device;

  uint8_t in_ep;
  uint8_t out_ep;

  size_t in_maxpktsize;
  size_t out_maxpktsize;
};

enum {
  QDL_FILE_UNKNOWN,
  QDL_FILE_PATCH,
  QDL_FILE_PROGRAM,
  QDL_FILE_UFS,
  QDL_FILE_CONTENTS,
};

int detect_type(const char *xml_file);

int find_device(struct qdl_device *qdl);

int qdl_read(struct qdl_device *qdl, void *buf, size_t len,
             unsigned int timeout);
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, bool eot);

int firehose_run(struct qdl_device *qdl, const char *incdir,
                 const char *storage);
int sahara_run(struct qdl_device *qdl, char *prog_mbn);
void print_hex_dump(const char *prefix, const void *buf, size_t len);
unsigned attr_as_unsigned(xmlNode *node, const char *attr, int *errors);
const char *attr_as_string(xmlNode *node, const char *attr, int *errors);

extern bool qdl_debug;

#endif
