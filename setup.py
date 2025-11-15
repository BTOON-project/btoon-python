from setuptools import setup, Extension
from pybind11.setup_helpers import Pybind11Extension, build_ext
import os
import sys

__version__ = "0.0.1"

# Find btoon-core installation
btoon_core_path = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "btoon-core"))
btoon_include = os.environ.get("BTOON_INCLUDE", os.path.join(btoon_core_path, "include"))
btoon_lib = os.environ.get("BTOON_LIB", os.path.join(btoon_core_path, "build"))

# Set up runtime library search path
extra_link_args = []
if sys.platform == 'darwin':
    # macOS: embed rpath so dylib can be found at runtime
    extra_link_args.extend([
        f'-Wl,-rpath,{btoon_lib}',
        f'-Wl,-rpath,@loader_path/../btoon-core/build',
    ])
elif sys.platform.startswith('linux'):
    # Linux: use $ORIGIN for relative rpath
    extra_link_args.extend([
        f'-Wl,-rpath,{btoon_lib}',
        '-Wl,-rpath,$ORIGIN/../btoon-core/build',
    ])

ext_modules = [
    Pybind11Extension(
        "btoon._btoon",
        ["btoon_python.cpp"],
        include_dirs=[btoon_include],
        library_dirs=[btoon_lib],
        libraries=["btoon_core", "z"],
        extra_link_args=extra_link_args,
        cxx_std=20,
        define_macros=[("VERSION_INFO", __version__)],
    ),
]

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="btoon",
    version=__version__,
    author="BTOON Contributors",
    author_email="hello@btoon.net",
    url="https://github.com/BTOON-project/btoon-python",
    description="Python bindings for BTOON: Binary TOON serialization format",
    long_description=long_description,
    long_description_content_type="text/markdown",
    packages=["btoon"],
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
    zip_safe=False,
    python_requires=">=3.7",
    install_requires=[
        "pybind11>=2.10.0",
    ],
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: MIT License",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.7",
        "Programming Language :: Python :: 3.8",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Software Development :: Libraries",
        "Topic :: System :: Archiving :: Compression",
    ],
)
