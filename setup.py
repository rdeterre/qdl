from distutils.core import setup, Extension

qdl = Extension('qdl', sources=['python_qdl.c'])

setup(name = 'qdl',
      version = '1.0',
      description = 'QDL C wrapper',
      ext_modules = [qdl])
