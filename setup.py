from setuptools import setup, Extension

from glob import glob
from shutil import copy
import platform
import os
import sys
import subprocess

def _pkgc(args):
    '''Calls the pkg-config command with the args as arguments'''
    args = ['pkg-config'] + args
    try:
        elements = subprocess.check_output(args, encoding='ascii').replace('\n', '').split(' ')
    except subprocess.CalledProcessError as e:
        print("Calling {} failed with error {}".format(args, e.returncode))
        return []
    return [x for x in elements if x != '']

def check_and_remove_prefix(prefix, data):
    if data.startswith(prefix):
        return data[len(prefix):]
    raise ValueError("String {} does not start with {}".format(data, prefix))

def remove_prefix_if_exists(prefix, data):
    if data.startswith(prefix):
        return data[len(prefix):]
    return data

def pkgc_get_lib_paths(lib):
    elements = _pkgc(['--libs-only-L', lib])
    return [check_and_remove_prefix('-L', x) for x in elements]

def pkgc_get_libs(lib):
    elements = _pkgc(['--libs-only-l', lib])
    return [check_and_remove_prefix('-l', x) for x in elements]

def pkg_get_cflags(lib):
    return _pkgc(['--cflags', lib])

def pkg_get_lib_flags(lib):
    return _pkgc(['--libs', lib])

def pkg_get_lib_path(lib):
    directories = pkgc_get_lib_paths(lib)
    if len(directories) != 1:
        raise ValueError("Got {} -L for library {}".format(len(directories), lib))
    directory = directories[0]
    names = pkgc_get_libs(lib)
    if len(names) != 1:
        raise ValueError("Got {} -l for library {}".format(len(names), lib))
    return directory + '/lib' + names[0] + '.a'

def pkg_get_dylib_paths(lib):
    dirs = pkgc_get_lib_paths(lib)
    names = pkgc_get_libs(lib)
    result = []
    for directory in dirs:
        for name in names:
            result = result + glob('{}/*{}*.dylib'.format(directory, name))
    return result

def main():
    cflags = pkg_get_cflags('libusb-1.0') + pkg_get_cflags('libxml-2.0')
    lflags = pkg_get_lib_flags('libxml-2.0') + pkg_get_lib_flags('libusb-1.0')
    files_to_package = []

    if platform.system() == 'Darwin':
        files_to_package = pkg_get_dylib_paths('libusb-1.0')
        if not files_to_package:
            raise ValueError('Could not identify any library to copy')



    print("C flags: {}".format(cflags))
    print("L flags: {}".format(lflags))
    print("Files to package: {}".format(files_to_package))

    qdl = Extension('qdl', sources=[
        'firehose.c',
        'patch.c',
        'program.c',
        'python_logging.c',
        'python_qdl.c',
        'qdl.c',
        'sahara.c',
        'ufs.c',
        'util.c'],
        extra_compile_args=cflags,
        extra_link_args=lflags,
        extra_objects=files_to_package)

    setup(name = 'qdl',
          version = '1.2.0',
          description = 'QDL C wrapper',
          ext_modules = [qdl])

if __name__ == "__main__":
    main()
