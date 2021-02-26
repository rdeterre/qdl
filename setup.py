from setuptools import setup, Extension

import sys
import subprocess

def pkg_config(libs):
    cflags = []
    lflags = []
    for lib in libs:
        cflags = cflags + subprocess.check_output(['pkg-config', '--cflags', lib], encoding='utf-8').replace('\n', '').split(' ')
        lflags = lflags + subprocess.check_output(['pkg-config', '--libs', lib], encoding='utf-8').replace('\n', '').split(' ')
    return (cflags, lflags)

cflags, lflags = pkg_config(['libusb-1.0', 'libxml-2.0'])

print ("C flags: {}".format(cflags))
print("L flags: {}".format(lflags) )

qdl = Extension('qdl', sources=[
    'python_qdl.c',
    'firehose.c',
    'qdl.c',
    'sahara.c',
    'util.c',
    'patch.c',
    'program.c',
    'ufs.c'],
    extra_compile_args=cflags,
    extra_link_args=lflags)

setup(name = 'qdl',
      version = '1.0',
      description = 'QDL C wrapper',
      ext_modules = [qdl])
