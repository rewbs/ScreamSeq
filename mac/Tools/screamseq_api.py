#!/usr/bin/env python3
"""ScreamSeq API entry point; the legacy module remains import-compatible."""
from resonance_api import *  # noqa: F401,F403

if __name__ == "__main__":
    import sys
    sys.exit(main())
