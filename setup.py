#!/usr/bin/env python
# encoding: utf-8
from distutils.core import setup, Extension
import commands
import os
import sys

libraries = []
output = commands.getoutput('GraphicsMagick-config --libs')
for library in output.split(' '):
    if library.startswith('-l'):
        libraries.append(library[2:])

library_dirs = []
output = commands.getoutput('GraphicsMagick-config --ldflags')
for directory in output.split(' '):
    if directory.startswith('-L'):
        library_dirs.append(directory[2:])


include_dirs = []
output = commands.getoutput('GraphicsMagick-config --cppflags')
for directory in output.split(' '):
    if directory.startswith('-I'):
        include_dirs.append(directory[2:])

setup(name = "magick",
      version = "0.7",
      ext_modules = [Extension("magick", ["imageobject.c"],
                               libraries=libraries,
                               library_dirs=library_dirs,
                               include_dirs=include_dirs)],
      description = "C-based Python Interface to GraphicsMagick",
      long_description = """Long description will be added soon""",
      author = "Christian Klein",
      author_email = "c.klein@hudora.de",
      url = "http://bitbucket.org/cklein/magick/",
      license = "BSD",
      )
