"""Experimental stem-to-ScreamSeq decompiler prototype.

Turns downloaded stems (e.g. Suno exports, Demucs separations, or multi-track
renders) into playable multi-sample ScreamSeq / Impulse Tracker instruments and
factorized patterns, with a two-tier progressive workflow:

  - Tier 1 ("Vibe Lock"): clustered drum kit + deduplicated musical smart chops
  - Tier 2 ("Promote"):   pitch-tracked chromatic multi-sample instruments &
                          fine-grained stab kits
"""

from .plan import ScratchpadPlan

__all__ = ["ScratchpadPlan"]
