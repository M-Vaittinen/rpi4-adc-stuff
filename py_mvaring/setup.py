"""
Build the mvaring Python C extension.

Run from the py_mvaring/ directory on the Raspberry Pi:

    pip install --user .        # install into user site-packages
    # or:
    pip install --user -e .     # editable / in-place (for development)
    # or build without installing:
    python setup.py build_ext --inplace

Requirements:
    - Python 3 development headers  (sudo apt install python3-dev)
    - librt                         (usually part of libc on modern Linux)
"""

import os
from setuptools import setup, Extension

# Parent directory holds mvaring.c, rpi_shmem.c and the headers.
parent_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

mvaring_ext = Extension(
    name="mvaring",
    sources=[
        "mvaring_ext.c",
        os.path.join(parent_dir, "mvaring.c"),
        os.path.join(parent_dir, "rpi_shmem.c"),
    ],
    include_dirs=[parent_dir],
    extra_compile_args=["-std=gnu11", "-O2"],
    # librt provides shm_open / shm_unlink on Linux
    libraries=["rt"],
)

setup(
    name="mvaring",
    version="0.1.0",
    description="Python bindings for the mvaring lock-free ring buffer",
    ext_modules=[mvaring_ext],
)
