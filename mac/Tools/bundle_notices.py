import pathlib,sys
root=pathlib.Path(__file__).resolve().parents[2]
output=pathlib.Path(sys.argv[1]);output.mkdir(parents=True,exist_ok=True)
licenses=[('VST3 SDK','mac/ThirdParty/vst3/LICENSE.txt'),('OpenMPT','LICENSE'),('FLAC','include/flac/COPYING.Xiph'),('miniz','include/miniz/LICENSE'),('minimp3','include/minimp3/LICENSE'),('mpt BSD','src/mpt/LICENSE.BSD-3-Clause.txt'),('mpt Boost','src/mpt/LICENSE.BSL-1.0.txt'),('r8brain','include/r8brain/LICENSE')]
text=['ScreamSeq is an independent native macOS derivative of OpenMPT.\nIt is not an official OpenMPT release.\n']
for title,path in licenses:text.append('\n'+title+'\n'+'='*len(title)+'\n\n'+(root/path).read_text())
stb=(root/'include/stb_vorbis/stb_vorbis.c').read_text();text.append('\nstb_vorbis\n'+stb[stb.rfind('This software is available under 2 licenses'):].removesuffix('*/\n'))
text.append('\nOpal OPL3 emulator\nReleased into the public domain by Shayde / Reality Productions.\nAdditional fixes by JP Cimalando.\n')
(output/'Third-Party Notices.txt').write_text('\n'.join(text))
(output/'User Guide.md').write_text((root/'mac/README.md').read_text())

(output/'Compatibility.md').write_text((root/'mac/COMPATIBILITY.md').read_text())
(output/'AUTOMATION.md').write_text((root/'mac/AUTOMATION.md').read_text())
(output/'PLAYBACK_AND_CURVES.md').write_text((root/'mac/PLAYBACK_AND_CURVES.md').read_text())
(output/'PRECISE_NOTES.md').write_text((root/'mac/PRECISE_NOTES.md').read_text())
