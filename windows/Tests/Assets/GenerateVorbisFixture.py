"""Regenerate our synthetic Vorbis fixture; ffmpeg/libvorbis needed only here.
No third-party audio. C++ tests consume the checked-in .ogg, not Python/ffmpeg.
"""
import hashlib
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import wave

scratch = Path(os.environ['TMPDIR'])
assert scratch.resolve() == Path(os.environ['TEMP']).resolve() == Path(os.environ['TMP']).resolve()
output = Path(__file__).parent / 'Fixtures' / 'mono-24000.ogg'
output.parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='vorbis-fixture-', dir=scratch) as directory:
    source = Path(directory) / 'tone.wav'
    with wave.open(str(source), 'wb') as wav:
        wav.setparams((1, 2, 24000, 0, 'NONE', 'not compressed'))
        wav.writeframes(b''.join(struct.pack('<h', round(12000 * math.sin(2 * math.pi * 440 * i / 24000))) for i in range(4800)))
    command = [shutil.which('ffmpeg'), '-hide_banner', '-loglevel', 'error', '-y', '-i', str(source),
               '-map_metadata', '-1', '-c:a', 'libvorbis', '-q:a', '3', '-fflags', '+bitexact', str(output)]
    subprocess.run(command, check=True)
print(output, output.stat().st_size, hashlib.sha256(output.read_bytes()).hexdigest())
