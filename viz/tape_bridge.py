#!/usr/bin/env python3

import os
from typing import Optional

TAPE_SIZE = 65536
TAPE_PATH = "/tmp/turingos_tape.bin"
META_SIZE = 64
META_PATH = "/tmp/turingos_meta.bin"

def _resolved_tape_path() -> str:
    # macOS maps /tmp -> /private/tmp; realpath avoids path mismatch.
    return os.path.realpath(TAPE_PATH)


def _resolved_meta_path() -> str:
    # Keep path handling consistent with tape snapshot mapping.
    return os.path.realpath(META_PATH)


def _read_exact(path: str, size: int) -> Optional[bytes]:
    try:
        if not os.path.exists(path):
            return None
        if os.path.getsize(path) < size:
            return None
        with open(path, "rb") as fp:
            data = fp.read(size)
    except OSError:
        return None
    if len(data) < size:
        return None
    return data


def get_tape_map() -> Optional[bytes]:
    # Preserve API name for callers; returns a bytes snapshot.
    return _read_exact(_resolved_tape_path(), TAPE_SIZE)


def get_tape() -> Optional[bytes]:
    return get_tape_map()


def get_meta_map() -> Optional[bytes]:
    # Preserve API name for callers; returns a bytes snapshot.
    return _read_exact(_resolved_meta_path(), META_SIZE)


def get_meta() -> Optional[dict]:
    raw = get_meta_map()
    if raw is None:
        return None

    return {
        "state": raw[0],
        "steps": int.from_bytes(raw[1:5], byteorder="little", signed=False),
        "dirty_map": raw[5:37],
        "pc": (raw[37] << 8) | raw[38],
    }


def get_pc() -> Optional[int]:
    meta = get_meta()
    if meta is None:
        return None
    return meta["pc"]


def get_state() -> Optional[int]:
    meta = get_meta()
    if meta is None:
        return None
    return meta["state"]


def close() -> None:
    # No persistent handles are kept.
    return
