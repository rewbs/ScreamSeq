"""Quick CLI benchmark comparing vibe / balanced / decomposed modes on synthetic stems."""

from __future__ import annotations

import tempfile
from pathlib import Path

from tools.stem_import.cli import main
from tools.stem_import.tests.test_pipeline import synthesize_test_stems


def run_benchmark() -> None:
    with tempfile.TemporaryDirectory(prefix="stem_import_bench_") as td:
        root = Path(td)
        stems = root / "stems"
        synthesize_test_stems(stems, bpm=124.0, bars=8)
        for mode in ("vibe", "balanced", "decomposed"):
            argv = [
                "import",
                str(stems),
                "--out",
                str(root / mode),
                "--mode",
                mode,
                "--editability",
                "0.5",
                "--score",
            ]
            if mode == "balanced":
                argv.append("--keep-vibe-backup")
            main(argv)


if __name__ == "__main__":
    run_benchmark()
