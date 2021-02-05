#define PY_SSIZE_T_CLEAN
#include <Python.h>

// "%s [--debug] [--storage <emmc|ufs>] [--finalize-provisioning] [--include
// <PATH>] <prog.mbn> [<program> <patch> ...]\n",
//
// >>> qdl_run(mbn, prog, patch, debug=False, storage='emmc',
//             finalize_provisioning=True)

static PyObject *qdl_run(PyObject *self, PyObject *args) {
  const char *command;
  int sts;

  if (!PyArg_ParseTuple(args, "s", &command))
    return NULL;
  sts = system(command);
  return PyLong_FromLong(sts);
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
