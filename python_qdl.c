#define PY_SSIZE_T_CLEAN
#include <Python.h>

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

  Py_BEGIN_ALLOW_THREADS
  type = detect_type(program);
  Py_END_ALLOW_THREADS
  if (type != QDL_FILE_PROGRAM) {
    PyErr_Format(PyExc_RuntimeError,
                 "Program passed is not a QDL program. Got type %d", type);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  ret = program_load(program);
  Py_END_ALLOW_THREADS
  if (ret < 0) {
    PyErr_Format(PyExc_RuntimeError, "Program load failed. Error %d", ret);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  type = detect_type(patch);
  Py_END_ALLOW_THREADS
  if (type != QDL_FILE_PATCH) {
    PyErr_Format(PyExc_RuntimeError,
                 "Patch passed is not a QDL patch. Got type %d", type);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  ret = patch_load(patch);
  Py_END_ALLOW_THREADS
  if (ret < 0) {
    PyErr_Format(PyExc_RuntimeError, "Patch load failed. Error %d", ret);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  libusb_init(NULL);
  Py_END_ALLOW_THREADS
  ret = find_device(&qdl);
  if (ret) {
    libusb_exit(NULL);
    PyErr_Format(PyExc_RuntimeError, "Could not load libusb. Error %d", ret);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  ret = sahara_run(&qdl, mbn);
  Py_END_ALLOW_THREADS
  if (ret < 0) {
    libusb_exit(NULL);
    PyErr_Format(PyExc_RuntimeError, "Could not run Sahara. Error %d\n", ret);
    return NULL;
  }

  Py_BEGIN_ALLOW_THREADS
  ret = firehose_run(&qdl, NULL, storage);
  Py_END_ALLOW_THREADS
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
