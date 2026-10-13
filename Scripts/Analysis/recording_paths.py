"""Filesystem paths for deeply nested native recordings, including Windows."""
import os
from pathlib import Path


def recording_path(path: Path) -> Path:
    path = Path(path).resolve()
    if os.name != "nt":
        return path
    value = str(path)
    if value.startswith("\\\\?\\"):
        return path
    # Python distributions without longPathAware otherwise silently skip deep
    # directories in rglob; a recording can exist but look like an empty run.
    if value.startswith("\\\\"):
        return Path("\\\\?\\UNC\\" + value[2:])
    return Path("\\\\?\\" + value)
