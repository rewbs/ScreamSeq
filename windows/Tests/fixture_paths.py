"""Long-path filesystem access for owned Windows test fixtures, not API inputs."""
from contextlib import contextmanager
import os
from pathlib import Path
import tempfile


def native_path(path):
    value = os.path.normpath(str(path))
    if not os.path.isabs(value):
        raise ValueError('Fixture filesystem access requires an absolute path')
    if value.startswith('\\\\?\\'):
        return Path(value)
    return Path('\\\\?\\UNC\\' + value[2:] if value.startswith('\\\\') else '\\\\?\\' + value)


@contextmanager
def temporary_directory(*, prefix, root):
    root = Path(root).resolve(strict=True)
    # Keep the reserved directory's native name for TemporaryDirectory cleanup,
    # even when descendants cross MAX_PATH. The app still receives an ordinary
    # path and must implement its own safe long-path handling.
    with tempfile.TemporaryDirectory(prefix=prefix, dir=native_path(root)) as reserved:
        ordinary = ('\\\\' + reserved[8:]) if reserved.startswith('\\\\?\\UNC\\') else reserved[4:]
        directory = Path(ordinary)
        if directory.parent != root or not directory.name.startswith(prefix):
            raise RuntimeError('Temporary fixture escaped its intended root')
        yield directory
