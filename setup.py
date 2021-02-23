from distutils.core import setup, Extension

qdl = Extension('qdl', sources=[
    'python_qdl.c',
    'firehose.c',
    'qdl.c',
    'sahara.c',
    'util.c',
    'patch.c',
    'program.c',
    'ufs.c'])

setup(name = 'qdl',
      version = '1.0',
      description = 'QDL C wrapper',
      ext_modules = [qdl])
