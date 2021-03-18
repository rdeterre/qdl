#include <stdarg.h>
#include <stdio.h>

#include "Python.h"

#include "python_logging.h"

PyThreadState *_save;

void begin_allow_threads() {
  _save = PyEval_SaveThread();
}

void end_allow_threads() {
  PyEval_RestoreThread(_save);
}

void log_msg(int type, char *format, ...) {
  static PyObject *logging = NULL;
  static PyObject *string = NULL;
#define BUFFER_SIZE 256
  static char buffer[BUFFER_SIZE];
  va_list args;
  va_start(args, format);

  // import logging module on demand
  if (logging == NULL) {
    logging = PyImport_ImportModuleNoBlock("logging");
    if (logging == NULL)
      PyErr_SetString(PyExc_ImportError, "Could not import module 'logging'");
  }

  vsnprintf(&buffer[0], BUFFER_SIZE, format, args);

  end_allow_threads();
  // build msg-string
  string = Py_BuildValue("s", &buffer[0]);

  // call function depending on loglevel
  switch (type) {
  case log_info:
    PyObject_CallMethod(logging, "info", "O", string);
    break;

  case log_warning:
    PyObject_CallMethod(logging, "warn", "O", string);
    break;

  case log_error:
    PyObject_CallMethod(logging, "error", "O", string);
    break;

  case log_debug:
    PyObject_CallMethod(logging, "debug", "O", string);
    break;
  }
  Py_DECREF(string);
  begin_allow_threads();
  va_end(args);
}
