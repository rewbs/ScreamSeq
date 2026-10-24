"""Actual-API full-song WAV checks for the deterministic layered composing fixture.

The caller supplies already verified, immutable project derivatives. This is
offline native-provider evidence, not hardware or sustained-audio qualification.
"""
import array
import math
import struct
import sys
from pathlib import Path

from native_roundtrip import write_json
from project_tree import EvidenceError, read_bytes, sha256


def run(client, recorded, layer_directory, output):
    recorded, layer_directory, output = Path(recorded), Path(layer_directory), Path(output)
    output.mkdir(exist_ok=False)
    sources = {'recorded': recorded, 'both-layers': layer_directory / 'layer-source.screamseq'}
    sources.update({name: layer_directory / (name + '.screamseq')
                    for name in ('sample-layer', 'plugin-layer', 'neither-layer')})
    immutable = {name: sha256(read_bytes(path)) for name, path in sources.items()}
    report = dict(format='screamseq-full-song-export-v1', passed=False, deviceQualified=False,
                  sourceSHA256=immutable, toolSHA256=sha256(read_bytes(Path(__file__))),
                  scope='Complete selected sequence, 48 kHz stereo float32 WAV, including hosted tails',
                  renders=[], layerContributions=[])

    def require(condition, message):
        if not condition:
            raise EvidenceError(message)

    def open_project(path):
        revision = client.call('document.get')['revision']
        client.call('document.open', dict(expectedRevision=revision, path=str(path), discard=True))

    try:
        layers = {}
        for name, path in sources.items():
            open_project(path)
            before = client.call('document.get')
            context = client.call('context.get')
            destination = output / (name + '.wav')
            reply = client.call('document.exportAudio', dict(expectedRevision=before['revision'], path=str(destination)))
            require(not reply['changed'] and not reply['playbackStopped'] and reply['revision'] == before['revision'],
                    name + ': export changed the document or stopped transport')
            require(client.call('document.get') == before and client.call('context.get') == context,
                    name + ': export changed accepted state, history, Save baseline or editor context')
            wav = read_bytes(destination)
            require(len(wav) > 44 and (len(wav)-44) % 8 == 0, name + ': incomplete stereo WAV')
            require((wav[:4], wav[8:16], wav[36:40]) == (b'RIFF', b'WAVEfmt ', b'data') and
                    struct.unpack_from('<HHIIHH', wav, 20) == (3, 2, 48000, 384000, 8, 32) and
                    struct.unpack_from('<I', wav, 4)[0] == len(wav)-8 and
                    struct.unpack_from('<I', wav, 40)[0] == len(wav)-44,
                    name + ': invalid float WAV identity, rate or exact chunk lengths')
            frames = (len(wav)-44) // 8
            require(0 < frames <= 48000*60, name + ': fixture export exceeds its bounded minute')
            pcm = array.array('f', wav[44:])
            if sys.byteorder != 'little':
                pcm.byteswap()
            require(all(math.isfinite(value) for value in pcm), name + ': non-finite PCM')
            require(any(abs(value) > 1e-6 for value in pcm), name + ': unexpectedly silent full-song export')
            report['renders'].append(dict(name=name, frames=frames, wavSHA256=sha256(wav),
                                          peak=max(abs(value) for value in pcm)))
            layers[name] = pcm
        # The prior resampling journey deliberately bypassed downstream DC
        # removal/gating in these derivatives so this DC-producing fixture
        # proves both layers rather than mistaking intentional suppression for
        # a missing instrument. The original processed song is exported too.
        for present, absent in (('both-layers', 'sample-layer'), ('both-layers', 'plugin-layer'),
                                ('sample-layer', 'neither-layer'), ('plugin-layer', 'neither-layer')):
            left, right = layers[present], layers[absent]
            require(len(left) == len(right), present + ': removing one layer changed the export duration')
            changed = sum(abs(a-b) > 2/32768 for a, b in zip(left, right) if abs(a) < 1 and abs(b) < 1)
            report['layerContributions'].append(dict(present=present, absent=absent, changedUnsaturatedSamples=changed))
            require(changed >= 64, present + ' versus ' + absent + ': missing audible full-song layer')
        open_project(recorded)
        report['passed'] = True
        return report
    except Exception as error:
        report['error'] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report['inputFilesUnchanged'] = all(sha256(read_bytes(path)) == immutable[name] for name, path in sources.items())
        if not report['inputFilesUnchanged']:
            report['passed'] = False
        write_json(output / 'report.json', report)
        if not report['inputFilesUnchanged']:
            raise EvidenceError('Full-song export changed an original saved input')
