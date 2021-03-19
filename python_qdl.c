#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "python_logging.h"
#include "qdl.h"

// "%s [--debug] [--storage <emmc|ufs>] [--finalize-provisioning] [--include
// <PATH>] <prog.mbn> [<program> <patch> ...]\n",
//
// >>> qdl_run('emmc', mbn, prog, patch)

static PyObject *qdl_run(PyObject *self, PyObject *args) {
  const char *storage;
  const char *mbn;
  const char *program;
  const char *patch;
  int type;
  int ret;
  struct qdl_device qdl;

  if (!PyArg_ParseTuple(args, "ssss", &storage, &mbn, &program, &patch))
    return NULL;

  begin_allow_threads();
  type = detect_type(program);
  end_allow_threads();
  if (type != QDL_FILE_PROGRAM) {
    PyErr_Format(PyExc_RuntimeError,
                 "Program passed is not a QDL program. Got type %d", type);
    return NULL;
  }

  begin_allow_threads();
  ret = program_load(program);
  end_allow_threads();
  if (ret < 0) {
    PyErr_Format(PyExc_RuntimeError, "Program load failed. Error %d", ret);
    return NULL;
  }

  begin_allow_threads();
  type = detect_type(patch);
  end_allow_threads();
  if (type != QDL_FILE_PATCH) {
    PyErr_Format(PyExc_RuntimeError,
                 "Patch passed is not a QDL patch. Got type %d", type);
    return NULL;
  }

  begin_allow_threads();
  ret = patch_load(patch);
  end_allow_threads();
  if (ret < 0) {
    PyErr_Format(PyExc_RuntimeError, "Patch load failed. Error %d", ret);
    return NULL;
  }

  begin_allow_threads();
  libusb_init(NULL);
  end_allow_threads();
  ret = find_device(&qdl);
  if (ret) {
    libusb_exit(NULL);
    PyErr_Format(PyExc_RuntimeError, "Could not load libusb. Error %d", ret);
    return NULL;
  }

  begin_allow_threads();
  ret = sahara_run(&qdl, mbn);
  end_allow_threads();
  if (ret < 0) {
    libusb_exit(NULL);
    PyErr_Format(PyExc_RuntimeError, "Could not run Sahara. Error %d\n", ret);
    return NULL;
  }

  begin_allow_threads();
  ret = firehose_run(&qdl, NULL, storage);
  end_allow_threads();
  if (ret < 0) {
    libusb_exit(NULL);
    PyErr_Format(PyExc_RuntimeError, "Could not run Firehose. Error %d\n", ret);
    return NULL;
  }

  return Py_None;
}

static PyMethodDef QdlMethods[] = {
    {"run", qdl_run, METH_VARARGS, "Runs QDL"},
    {NULL, NULL, 0, NULL} /* Sentinel */
};

static struct PyModuleDef QdlModule = {
    PyModuleDef_HEAD_INIT, "qdl", /* name of module */
    NULL,                         /* module documentation, may be NULL */
    -1, /* size of per-interpreter state of the module,
           or -1 if the module keeps state in global variables. */
    QdlMethods};

PyMODINIT_FUNC PyInit_qdl(void) { return PyModule_Create(&QdlModule); }
