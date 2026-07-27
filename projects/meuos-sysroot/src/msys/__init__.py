# msys.py — Python bindings for .msys single-file sysroot archives
#
# Usage:
#   import msys
#   m = msys.open("archive.msys")
#   data = m.read("path/to/file")
#   print(f"version: {m.version}, entries: {m.count}")
#
# Requires: libmsys.so (build via: make so)
#           in the same directory as this module or in LD_LIBRARY_PATH

import ctypes
import ctypes.util
import os
import struct
from collections import namedtuple

# ── locate shared library ──
_lib_path = os.path.join(os.path.dirname(__file__), "..", "..", "build", "libmsys.so")
_lib = None

for _p in [_lib_path,
           os.path.join(os.path.dirname(__file__), "libmsys.so"),
           ctypes.util.find_library("msys")]:
    if _p and os.path.exists(_p):
        _lib = ctypes.cdll.LoadLibrary(_p)
        break

if _lib is None:
    raise ImportError("libmsys.so not found. Build it with: make -C projects/meuos-sysroot so")

# ── constants from msys.h ──
MSYS_FORMAT_V1 = 1
MSYS_FORMAT_V2 = 2

MSYS_FILE_REG = 0
MSYS_FILE_DIR = 1
MSYS_FILE_SYMLINK = 2
MSYS_FILE_CHR = 3
MSYS_FILE_BLK = 4
MSYS_FILE_FIFO = 5
MSYS_FILE_SOCK = 6

# ── ctypes setup ──
class _Msys(ctypes.Structure):
    _fields_ = [
        ("base", ctypes.c_void_p),
        ("size", ctypes.c_size_t),
        ("format_version", ctypes.c_int),
        ("_hdr", ctypes.c_void_p * 2),  # union hdr/hdr_v2
        ("index", ctypes.c_void_p),
        ("index_v2", ctypes.c_void_p),
        ("entries", ctypes.POINTER(ctypes.c_void_p)),
        ("chunks", ctypes.c_void_p),
    ]

class _MsysOverlay(ctypes.Structure):
    _fields_ = [
        ("layers", ctypes.POINTER(ctypes.POINTER(_Msys))),
        ("count", ctypes.c_int),
        ("cap", ctypes.c_int),
    ]

class _MsysStream(ctypes.Structure):
    _fields_ = [
        ("base", ctypes.c_void_p),
        ("size", ctypes.c_size_t),
        ("entries", ctypes.c_void_p),
        ("count", ctypes.c_size_t),
        ("pos", ctypes.c_size_t),
    ]

class MsysStat(namedtuple("MsysStat", "file_type mode uid gid size name_hash")):
    """File metadata from msys_stat."""

class _MsysStatStruct(ctypes.Structure):
    _fields_ = [
        ("file_type", ctypes.c_uint16),
        ("mode", ctypes.c_uint16),
        ("uid", ctypes.c_uint32),
        ("gid", ctypes.c_uint32),
        ("size", ctypes.c_uint64),
        ("name_hash", ctypes.c_uint32),
    ]

# ── function signatures ──
_lib.msys_open.restype = ctypes.POINTER(_Msys)
_lib.msys_open.argtypes = [ctypes.c_char_p]

_lib.msys_close.argtypes = [ctypes.POINTER(_Msys)]

_lib.msys_search.restype = ctypes.c_void_p
_lib.msys_search.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

_lib.msys_read.restype = ctypes.c_int
_lib.msys_read.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.c_void_p, ctypes.c_size_t]

_lib.msys_count.restype = ctypes.c_uint32
_lib.msys_count.argtypes = [ctypes.POINTER(_Msys)]

_lib.msys_format_version.restype = ctypes.c_int
_lib.msys_format_version.argtypes = [ctypes.POINTER(_Msys)]

_lib.msys_fstat.restype = ctypes.c_int
_lib.msys_fstat.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t)]

_lib.msys_stat.restype = ctypes.c_int
_lib.msys_stat.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.POINTER(_MsysStatStruct)]

_lib.msys_readlink.restype = ctypes.c_int
_lib.msys_readlink.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]

_lib.msys_verify.restype = ctypes.c_int
_lib.msys_verify.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p]

_lib.msys_verify_all.restype = ctypes.c_int
_lib.msys_verify_all.argtypes = [ctypes.POINTER(_Msys)]

_lib.msys_getxattr.restype = ctypes.c_int
_lib.msys_getxattr.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]

_lib.msys_get_extension.restype = ctypes.c_int
_lib.msys_get_extension.argtypes = [ctypes.POINTER(_Msys), ctypes.c_uint32, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint32)]

_lib.msys_verify_signature.restype = ctypes.c_int
_lib.msys_verify_signature.argtypes = [ctypes.POINTER(_Msys), ctypes.c_char_p]

_lib.msys_stream_open.restype = ctypes.POINTER(_MsysStream)
_lib.msys_stream_open.argtypes = [ctypes.c_char_p]

_lib.msys_stream_next.restype = ctypes.c_int
_lib.msys_stream_next.argtypes = [
    ctypes.POINTER(_MsysStream),
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.POINTER(ctypes.c_size_t),
    ctypes.POINTER(ctypes.c_void_p),
    ctypes.POINTER(ctypes.c_size_t),
]

_lib.msys_stream_close.argtypes = [ctypes.POINTER(_MsysStream)]

_lib.msys_overlay_open.restype = ctypes.POINTER(_MsysOverlay)
_lib.msys_overlay_open.argtypes = [ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]

_lib.msys_overlay_count.restype = ctypes.c_int
_lib.msys_overlay_count.argtypes = [ctypes.POINTER(_MsysOverlay)]

_lib.msys_overlay_search.restype = ctypes.c_void_p
_lib.msys_overlay_search.argtypes = [ctypes.POINTER(_MsysOverlay), ctypes.c_char_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_int)]

_lib.msys_overlay_load.restype = ctypes.c_int
_lib.msys_overlay_load.argtypes = [ctypes.POINTER(_MsysOverlay), ctypes.c_char_p, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)]

_lib.msys_overlay_readdir.restype = ctypes.c_int
_lib.msys_overlay_readdir.argtypes = [ctypes.POINTER(_MsysOverlay), ctypes.c_char_p, ctypes.c_void_p, ctypes.c_void_p]

_lib.msys_overlay_close.argtypes = [ctypes.POINTER(_MsysOverlay)]

# ── Python API ──

class Msys:
    """Handle for an open .msys archive."""

    def __init__(self, path, _handle=None, _overlay=None):
        if _handle is not None:
            self._m = _handle
            self._ol = _overlay
            self._is_overlay = _overlay is not None
            return
        bpath = path.encode() if isinstance(path, str) else path
        self._m = _lib.msys_open(bpath)
        if not self._m:
            raise FileNotFoundError(f"cannot open {path}")
        self._ol = None
        self._is_overlay = False

    def close(self):
        if self._is_overlay and self._ol:
            _lib.msys_overlay_close(self._ol)
        elif self._m:
            _lib.msys_close(self._m)
        self._m = None
        self._ol = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    @property
    def version(self):
        return _lib.msys_format_version(self._m)

    @property
    def count(self):
        return _lib.msys_count(self._m)

    @property
    def flags(self):
        if self._is_overlay:
            return 0
        raw = ctypes.c_uint32.from_address(ctypes.addressof(self._m.contents) +
                                            ctypes.sizeof(ctypes.c_void_p) * 2 +  # base + size + format_version
                                            ctypes.sizeof(ctypes.c_int))  # format_version
        return raw.value

    def search(self, name):
        bname = name.encode() if isinstance(name, str) else name
        size = ctypes.c_size_t()
        if self._is_overlay:
            li = ctypes.c_int()
            ptr = _lib.msys_overlay_search(self._ol, bname, ctypes.byref(size), ctypes.byref(li))
        else:
            ptr = _lib.msys_search(self._m, bname, ctypes.byref(size))
        if not ptr:
            return None
        return ctypes.string_at(ptr, size.value)

    def read(self, name):
        data = self.search(name)
        if data is None:
            raise KeyError(f"'{name}' not found in archive")
        return bytes(data)

    def readlink(self, name):
        buf = ctypes.create_string_buffer(4096)
        r = _lib.msys_readlink(self._m, name.encode(), buf, 4096)
        if r < 0:
            return None
        return buf.value.decode()

    def fstat(self, name):
        size = ctypes.c_size_t()
        r = _lib.msys_fstat(self._m, name.encode(), ctypes.byref(size))
        if r < 0:
            return None
        return size.value

    def stat(self, name):
        raw = _MsysStatStruct()
        r = _lib.msys_stat(self._m, name.encode(), ctypes.byref(raw))
        if r < 0:
            return None
        return MsysStat(
            file_type=raw.file_type,
            mode=raw.mode,
            uid=raw.uid,
            gid=raw.gid,
            size=raw.size,
            name_hash=raw.name_hash,
        )

    def verify(self, name):
        return _lib.msys_verify(self._m, name.encode()) == 0

    def verify_all(self):
        return _lib.msys_verify_all(self._m) == 0

    def getxattr(self, name, key):
        buf = ctypes.create_string_buffer(4096)
        r = _lib.msys_getxattr(self._m, name.encode(), key.encode(), buf, 4096)
        if r < 0:
            return None
        return buf.value[:r].decode()

    def get_extension(self, ext_type):
        ptr = ctypes.c_void_p()
        dlen = ctypes.c_uint32()
        r = _lib.msys_get_extension(self._m, ext_type, ctypes.byref(ptr), ctypes.byref(dlen))
        if r < 0:
            return None
        return ctypes.string_at(ptr, dlen.value)

    def verify_signature(self, pk):
        return _lib.msys_verify_signature(self._m, pk) == 0

    def __repr__(self):
        if self._is_overlay:
            n = _lib.msys_overlay_count(self._ol)
            return f"<Msys overlay ({n} layers)>"
        return f"<Msys v{self.version} {self.count} entries>"


class MsysOverlay(Msys):
    """Overlay of multiple .msys archives."""

    def __init__(self, paths):
        bpaths = (ctypes.c_char_p * len(paths))()
        for i, p in enumerate(paths):
            bpaths[i] = p.encode() if isinstance(p, str) else p
        ol = _lib.msys_overlay_open(bpaths, len(paths))
        if not ol:
            raise FileNotFoundError(f"cannot open overlay: {paths}")
        # Get the first layer's msys handle for property access
        first = ol.contents.layers[0]
        super().__init__(None, _handle=first, _overlay=ol)

    @property
    def count(self):
        return _lib.msys_overlay_count(self._ol)


class MsysStream:
    """Sequential streaming reader for .msys archives."""

    def __init__(self, path):
        bpath = path.encode() if isinstance(path, str) else path
        self._s = _lib.msys_stream_open(bpath)
        if not self._s:
            raise FileNotFoundError(f"cannot open stream: {path}")

    def close(self):
        if self._s:
            _lib.msys_stream_close(self._s)
            self._s = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()

    def __iter__(self):
        return self

    def __next__(self):
        name_ptr = ctypes.c_char_p()
        nlen = ctypes.c_size_t()
        data_ptr = ctypes.c_void_p()
        dsize = ctypes.c_size_t()
        r = _lib.msys_stream_next(self._s, ctypes.byref(name_ptr),
                                   ctypes.byref(nlen), ctypes.byref(data_ptr), ctypes.byref(dsize))
        if r == 0:
            raise StopIteration
        if r < 0:
            raise IOError("stream error")
        name = ctypes.string_at(name_ptr, nlen.value)
        data = ctypes.string_at(data_ptr, dsize.value)
        return (name, data)


# ── convenience functions ──

def open(path):
    """Open a .msys archive. Returns Msys handle."""
    return Msys(path)

def open_overlay(paths):
    """Open an overlay of multiple .msys archives. Returns MsysOverlay handle."""
    return MsysOverlay(paths)
