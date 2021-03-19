from setuptools import setup, Extension

import platform
import sys
import subprocess

def _pkgc(args):
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

def main():
    cflags = pkg_get_cflags('libusb-1.0') + pkg_get_cflags('libxml-2.0')
    lflags = pkg_get_lib_flags('libxml-2.0')

    extra_objects = []
    if platform.system() == 'Darwin':
        try:
            extra_objects = [pkg_get_lib_path('libusb-1.0')]
        except ValueError as e:
            print("Could not get lib path: {}".format(e))
    else:
        lflags = lflags + pkg_get_lib_flags('libusb-1.0')


    print("C flags: {}".format(cflags))
    print("L flags: {}".format(lflags))
    print("Extra objects: {}".format(extra_objects))

    qdl = Extension('qdl', sources=[
        'python_qdl.c',
        'python_logging.c',
        'firehose.c',
        'qdl.c',
        'sahara.c',
        'util.c',
        'patch.c',
        'program.c',
        'ufs.c'],
        extra_compile_args=cflags,
        extra_link_args=lflags,
        extra_objects=extra_objects)

    setup(name = 'qdl',
          version = '1.1.4',
          description = 'QDL C wrapper',
          ext_modules = [qdl])

if __name__ == "__main__":
    main()
