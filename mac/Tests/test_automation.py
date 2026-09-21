#!/usr/bin/env python3
"""End-to-end socket/client checks using a private, silent, windowless app host."""
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
BUILD = Path(os.environ.get("RESONANCE_BUILD_DIR", str(ROOT / "bin/mac-native"))).resolve()
sys.path.insert(0, str(ROOT / "mac/Tools"))
from resonance_api import APIError, Client, drum_roll, endpoints
import plistlib


def expect_error(code, action):
    try:
        action()
    except APIError as error:
        assert error.code == code, (error.code, str(error))
    else:
        raise AssertionError(f"Expected API error {code}")


def instrument_envelopes(client):
    def write(method, **params): return client.call(method,{"expectedRevision":client.call("document.get")["revision"],**params})
    index=write("instrument.create")["data"]["instrument"]
    identity=next(i["id"] for i in client.call("document.get")["data"]["instruments"] if i["index"]==index)
    target={"instrument":identity,"envelope":"volume"}
    def read():return client.call("instrument.envelope.get",target)
    # New envelopes can be generated while remaining disabled until enabled explicitly.
    empty=read();made=write("instrument.envelope.transform",**target,operation="ramp",end=13)
    assert made["changed"] and read()["data"]["points"]==[[0,0],[12,64]] and not read()["data"]["enabled"]
    write("history.undo",domain="document");assert read()["data"]==empty["data"]
    write("instrument.patch",instrument=index,values={"envelope":0,"points":[[0,64],[4,48],[8,32],[12,0]],"enabled":True,"loop":True,"loopStart":1,"loopEnd":2,"sustain":True,"sustainPoint":1,"sustainEnd":2})
    baseline=read();clip=client.call("instrument.envelope.copy",{**target,"start":0,"end":5})["data"]
    assert clip=={"span":5,"points":[[0,64],[4,48]],"units":"ticks"}
    tools=[("flip-time",0,13,{}),("flip-values",0,13,{}),("shift",4,9,{"amount":1}),
           ("scale",0,13,{"amount":0.5}),("ramp",0,13,{"from":10,"to":60}),
           ("sine",0,17,{"spacing":4}),("humanize",1,13,{"amount":5,"jitter":1,"seed":123}),
           ("paste",8,None,{"clip":clip}),("insert",4,None,{"clip":clip,"repeats":2})]
    for op,start,end,options in tools:
        before=read();params={**target,"operation":op,"start":start,"options":options,"expectedRevision":before["revision"]}
        if end is not None:params["end"]=end
        preview=client.call("instrument.envelope.transform",{**params,"dryRun":True})
        assert read()==before and not preview["changed"] and not preview["playbackStopped"]
        assert preview["data"]["wouldChange"]
        changed=client.call("instrument.envelope.transform",params,"instrument-envelope-"+op)
        assert client.call("instrument.envelope.transform",params,"instrument-envelope-"+op)==changed
        expect_error(-32001,lambda:client.call("instrument.envelope.transform",params))
        assert read()["data"]==preview["data"]["after"]
        write("history.undo",domain="document");assert read()["data"]==baseline["data"]
        assert not write("instrument.envelope.transform",**target,operation="scale",options={"amount":1})["changed"]
        write("history.redo",domain="document");assert read()["data"]==preview["data"]["after"]
        write("history.undo",domain="document")
    before=read()
    for bad in [{"operation":"unknown"},{"operation":"shift","options":{"amount":1}},{"operation":"shift","options":{"amount":0.1}},
                {"operation":"ramp","options":{"from":65}},{"operation":"ramp","options":{"curve":"smooth"}},
                {"operation":"sine","end":65536,"options":{"spacing":1}},{"operation":"humanize","options":{"seed":True}},
                {"operation":"flip-time","start":0.5},{"operation":"flip-time","start":13,"end":13},
                {"operation":"paste","end":13,"options":{"clip":clip}},{"operation":"paste","options":{"clip":{**clip,"units":"rows"}}},
                {"operation":"paste","options":{"clip":{**clip,"points":[[0,0],[0,64]]}}}]:
        expect_error(-32602,lambda:write("instrument.envelope.transform",**target,**bad))
    assert read()==before
    expect_error(-32602,lambda:client.call("instrument.envelope.get",{"instrument":index,"envelope":"volume"}))
    expect_error(-32602,lambda:client.call("instrument.envelope.get",{**target,"envelope":"filter"}))
    expect_error(-32602,lambda:client.call("instrument.envelope.copy",{**target,"start":20,"end":30}))
    write("history.undo",domain="document");write("history.undo",domain="document")
    expect_error(-32602,lambda:read())
    print("PASS instrument envelope socket: stable identity, all shared tools, native tick/value clipboard, preview/retry/stale/no-op/history, empty generation, strict limits and removed identities")


def plugin_aliases(client):
    def write(method, **params): return client.call(method,{"expectedRevision":client.call("document.get")["revision"],**params})
    instruments=[write("instrument.create")["data"]["instrument"] for _ in range(3)]
    descriptor={"type":0,"subtype":0,"manufacturer":0,"name":"Resonance Test Instrument","format":"VST3","path":str(BUILD/"test-plugins/ResonanceFixture.vst3"),"classID":"5245534F4E414E43494E535452550001","isInstrument":True}
    write("plugin.add",descriptor=descriptor)
    identity=client.call("document.get")["data"]["nativePlugins"][0]["instanceID"]
    def read(plugin=identity): return client.call("plugin.instruments.get",{"plugin":plugin})
    initial=read();assignments=[{"instrument":instrument,"channel":channel} for instrument,channel in zip(instruments,[2,7,16])]
    preview=write("plugin.instruments.set",plugin=identity,assignments=assignments,dryRun=True)
    assert not preview["changed"] and not preview["playbackStopped"] and preview["data"]["wouldChange"] and read()==initial
    params={"expectedRevision":initial["revision"],"plugin":identity,"assignments":assignments}
    applied=client.call("plugin.instruments.set",params,"aliases-retry")
    assert client.call("plugin.instruments.set",params,"aliases-retry")==applied
    expect_error(-32001,lambda:client.call("plugin.instruments.set",params))
    route=read()["data"]
    assert [{k:a[k] for k in ["instrument","channel"]} for a in route["assignments"]]==assignments
    assert all(a["available"] for a in route["assignments"])
    assert all(i["owner"]==identity for i in route["instruments"] if i["instrument"] in instruments)
    assert not write("plugin.instruments.set",plugin=identity,assignments=assignments)["changed"]
    assert not write("instrument.plugin.set",instrument=instruments[0],plugin=identity,channel=2)["changed"]
    current=read()
    trial=write("instrument.plugin.set",instrument=instruments[1],plugin="",dryRun=True)
    assert trial["data"]["wouldChange"] and not trial["changed"] and read()==current
    write("instrument.plugin.set",instrument=instruments[1],plugin="")
    assert [a["instrument"] for a in read()["data"]["assignments"]]==[instruments[0],instruments[2]]
    write("history.undo",domain="plugins")
    assert read()["data"]==current["data"]
    expect_error(-32602,lambda:write("instrument.plugin.set",instrument=instruments[1],plugin="missing"))

    for bad in [[assignments[0],assignments[0]],[{"instrument":0,"channel":1}],[{"instrument":instruments[0],"channel":0}],
                [{"instrument":instruments[0],"channel":17}],[{"instrument":True,"channel":1}],
                [{"instrument":255,"channel":1}],[{"instrument":instruments[0],"channel":1,"unknown":0}]]:
        expect_error(-32602,lambda:write("plugin.instruments.set",plugin=identity,assignments=bad))
    expect_error(-32602,lambda:write("plugin.instruments.set",plugin="missing",assignments=[]))
    expect_error(-32602,lambda:client.call("plugin.instruments.get",{"plugin":identity,"slot":0}))
    write("history.undo",domain="plugins");assert not read()["data"]["assignments"]
    assert not write("plugin.instruments.set",plugin=identity,assignments=[])["changed"]
    write("history.redo",domain="plugins");assert read()["data"]==route
    # Another instance cannot claim an already-owned tracker instrument.
    write("plugin.add",descriptor=descriptor);neighbor=client.call("document.get")["data"]["nativePlugins"][1]["instanceID"]
    expect_error(-32602,lambda:write("plugin.instruments.set",plugin=neighbor,assignments=[assignments[0]]))
    before_move=read()["data"]
    stale_revision=client.call("document.get")["revision"]
    write("instrument.plugin.set",instrument=instruments[1],plugin=neighbor,channel=12)
    assert [a["instrument"] for a in read()["data"]["assignments"]]==[instruments[0],instruments[2]]
    assert read(neighbor)["data"]["assignments"][0]["channel"]==12
    expect_error(-32001,lambda:client.call("instrument.plugin.set",{"instrument":instruments[1],"plugin":identity,"expectedRevision":stale_revision}))
    write("history.undo",domain="plugins")
    assert read()["data"]==before_move and not read(neighbor)["data"]["assignments"]

    write("plugin.move",slot=0,direction=1);assert read()["data"]["assignments"]==route["assignments"]
    write("plugin.parameters.set",slot=1,values=[{"id":7,"value":0.3}]);assert read()["data"]["assignments"]==route["assignments"]
    # Native presets transfer sound settings, preserving every instrument/channel route.
    with tempfile.TemporaryDirectory() as temp:
        path=str(Path(temp)/"aliases.resonance-preset")
        write("plugin.preset.save",plugin=identity,path=path,name="Shared")
        info=client.call("plugin.preset.inspect",{"path":path})["data"]
        write("plugin.parameters.set",slot=1,values=[{"id":7,"value":0.8}])
        write("plugin.preset.load",plugin=identity,path=path,expectedPresetRevision=info["presetRevision"])
        assert read()["data"]["assignments"]==route["assignments"]
        project=Path(temp)/"aliases.resonance"
        assert write("document.save",path=str(project),dryRun=True)["data"]["projectVersion"]==5 and not project.exists()
        assert write("document.save",path=str(project))["data"]["projectVersion"]==5
        root=plistlib.loads(project.read_bytes());assert root["version"]==5 and root["plugins"][1]["instrumentAssignments"]==assignments
    # Legacy single-assignment API can move an alias and promotes the previous owner's remaining primary.
    write("plugin.assign",slot=0,instrument=instruments[0]);assert read(neighbor)["data"]["assignments"][0]["instrument"]==instruments[0]
    assert [a["instrument"] for a in read()["data"]["assignments"]]==instruments[1:]
    write("plugin.remove",slot=1);expect_error(-32602,lambda:read())
    write("plugin.remove",slot=0)
    for _ in instruments:write("history.undo",domain="document")
    print("PASS alias socket: stable owners/MIDI channels, preview/retry/stale/no-op/Undo, conflicts and strict validation, reorder, presets, v5 persistence and legacy reassignment")


def song_timing(client):
    def read(): return client.call("document.timing.get")
    def write(**params): return client.call("document.timing.set",{"expectedRevision":read()["revision"],**params})
    initial=read(); original=initial["data"]
    patch={"mode":"modern","tempo":127.125,"speed":7,"rowsPerBeat":4,"rowsPerMeasure":12,"groove":[1.5,0.5,1.25,0.75]}
    preview=write(**patch,dryRun=True)
    assert not preview["changed"] and not preview["playbackStopped"] and preview["data"]["wouldChange"] and read()==initial
    params={"expectedRevision":initial["revision"],**patch}
    applied=client.call("document.timing.set",params,"timing-retry")
    assert client.call("document.timing.set",params,"timing-retry")==applied
    expect_error(-32001,lambda:client.call("document.timing.set",params))
    current=read()
    assert all(current["data"][k]==v for k,v in patch.items()) and current["data"]["grooveActive"]
    assert client.call("document.get")["data"]["tempo"]==127.125
    assert not write(**patch)["changed"]
    for bad in [{"mode":"bad"},{"mode":"classic"},{"rowsPerBeat":3},{"rowsPerMeasure":3},{"groove":[1,1]},
                {"tempo":True},{"tempo":31},{"tempo":513},{"speed":1.5},{"rowsPerBeat":33},{"rowsPerMeasure":129},
                {"groove":[0.25,4,4,4]},{"groove":[True,1,1,1]},{"dryRun":1},{"unknown":1}]:
        expect_error(-32602,lambda:write(**bad))
    assert read()==current
    undo=client.call("history.undo",{"expectedRevision":current["revision"],"domain":"document"})
    assert read()["data"]==original
    # Read/no-op/preview/invalid calls must not erase the pending Redo.
    assert not write(tempo=original["tempo"])["changed"]
    write(**patch,dryRun=True)
    client.call("history.redo",{"expectedRevision":read()["revision"],"domain":"document"})
    assert read()["data"]==current["data"]
    # Existing document.patch also accepts fractional tempo without erasing groove.
    client.call("document.patch",{"expectedRevision":read()["revision"],"tempo":129.01234})
    rounded=read()["data"]
    assert abs(rounded["tempo"]-129.0123)<1e-10 and rounded["groove"]==patch["groove"]
    client.call("history.undo",{"expectedRevision":read()["revision"],"domain":"document"})
    client.call("history.undo",{"expectedRevision":read()["revision"],"domain":"document"})
    assert read()["data"]==original
    print("PASS timing socket: fractional BPM, musical mode/beat/bar/groove, exact preview/retry/stale/no-op/history, strict fields, native document display and legacy patch parity")


def navigation_tools(client):
    initial = client.call("context.get")
    document = client.call("document.get")
    old = initial["data"]
    base = {"expectedRevision":initial["revision"],"expectedContext":old["contextRevision"]}
    moved = client.call("context.set", {**base,"row":3,"channel":1,"column":4,"following":False}, "navigation-retry")
    assert client.call("context.set", {**base,"row":3,"channel":1,"column":4,"following":False}, "navigation-retry") == moved
    assert not moved["changed"] and moved["contextChanged"] and not moved["playbackStopped"]
    assert moved["revision"] == initial["revision"] and moved["data"]["row"] == 3 and not moved["data"]["following"]
    expect_error(-32001, lambda: client.call("context.set", {**base,"row":4}))
    base = {**base,"expectedContext":moved["data"]["contextRevision"]}
    no_op = client.call("context.set", {**base,"row":3})
    assert not no_op["contextChanged"] and not no_op["changed"]
    for extra in [{"row":True},{"row":1.5},{"row":-1},{"row":1000000},{"channel":128},{"column":5},
                  {"pattern":65535},{"following":1},{"following":"false"},{"typo":0}]:
        expect_error(-32602, lambda: client.call("context.set", {**base,**extra}))
    expect_error(-32001, lambda: client.call("context.set", {**base,"row":4,"expectedRevision":"stale"}))
    assert client.call("context.get")["data"] == moved["data"]
    restored = client.call("context.set", {**base,**{k:old[k] for k in ["pattern","row","channel","column","following"]}})
    assert all(restored["data"][k] == old[k] for k in ["pattern","row","channel","column","following"])
    assert client.call("document.get") == document, "Navigation must preserve document, dirty state, history and transport"
    print("PASS navigation socket: cursor/follow read/write, strict coordinates/types, stale cursor/song, retry/no-op, preserved document/history/transport")


def plugin_library_tools(client, directory):
    song = client.call("document.get")
    def library(**params): return client.call("plugin.library.get", {"format":"Built-in",**params})["data"]
    initial = library(includeHidden=True)
    assert initial["plugins"] and all(p["format"]=="Built-in" and p["catalogID"].startswith("p") and len(p["catalogID"])==65 for p in initial["plugins"])
    assert len({p["catalogID"] for p in initial["plugins"]})==len(initial["plugins"]),"Each built-in effect needs its own identity"
    plugin = initial["plugins"][0]
    base = {"expectedLibraryRevision":initial["libraryRevision"],"catalogID":plugin["catalogID"]}
    preview = client.call("plugin.library.set",{**base,"favorite":True,"category":"Agent effects","dryRun":True})
    assert not preview["changed"] and not preview["playbackStopped"] and preview["data"]["wouldChange"] and not preview["data"]["written"]
    assert library(includeHidden=True)==initial
    written = client.call("plugin.library.set",{**base,"favorite":True,"hidden":True,"category":"Agent effects"},"library-retry")
    assert client.call("plugin.library.set",{**base,"favorite":True,"hidden":True,"category":"Agent effects"},"library-retry")==written
    assert not written["changed"] and not written["playbackStopped"] and written["data"]["written"]
    assert not any(p["catalogID"]==plugin["catalogID"] for p in library()["plugins"])
    matched=library(includeHidden=True,favoritesOnly=True,category="Agent effects",search="AGENT",kind="effect")["plugins"]
    assert len(matched)==1 and matched[0]["catalogID"]==plugin["catalogID"] and matched[0]["descriptor"]==plugin["descriptor"]
    assert not library(kind="instrument")["plugins"]
    expect_error(-32001,lambda:client.call("plugin.library.set",{**base,"favorite":False}))
    base["expectedLibraryRevision"]=written["data"]["libraryRevision"]
    unchanged=client.call("plugin.library.set",{**base,"favorite":True})
    assert not unchanged["data"]["wouldChange"] and not unchanged["data"]["written"]
    for patch in [{},{"favorite":1},{"hidden":"yes"},{"category":123},{"category":"x"*81},{"unknown":True},{"catalogID":"bad","favorite":True}]:
        expect_error(-32602,lambda:client.call("plugin.library.set",{**base,**patch}))
    for patch in [{"favoritesOnly":1},{"includeHidden":"yes"},{"kind":"synth"},{"format":"VST2"},{"search":False},{"rescan":1}]:
        expect_error(-32602,lambda:library(**patch))
    restored=client.call("plugin.library.set",{**base,"favorite":False,"hidden":False,"category":""})
    assert restored["data"]["written"] and library(includeHidden=True)["plugins"]==initial["plugins"]
    preferences_path = directory / "plugin-library.json"
    saved = preferences_path.read_bytes()
    preferences_path.write_bytes(b"broken preferences")
    fallback=library(includeHidden=True)
    assert fallback["plugins"] and not fallback["preferencesAvailable"] and not fallback["libraryRevision"] and fallback["warning"]
    expect_error(-32602,lambda:client.call("plugin.library.set",{"expectedLibraryRevision":restored["data"]["libraryRevision"],"catalogID":plugin["catalogID"],"favorite":True}))
    assert preferences_path.read_bytes()==b"broken preferences"
    preferences_path.write_bytes(saved)
    assert library()["preferencesAvailable"]
    assert client.call("document.get")==song,"Library preferences leave song revision, document history, dirty flag and transport unchanged"
    print("PASS plugin library socket: stable clean descriptors, search/format/kind/category/hidden/favorite filters, separate revision, persistence boundary, preview/retry/no-op/stale/strict fields and unchanged song/history/transport")


def note_track_tools(client):
    before = client.call("document.get")["data"]
    def read(): return client.call("track.get")["data"]
    def write(method, **params):
        return client.call(method, {"expectedRevision":client.call("document.get")["revision"],**params})
    def undo(): return write("history.undo",domain="document")
    initial = read()
    original_cells = client.call("pattern.get",{"pattern":0})["data"]["cells"]
    rev = client.call("document.get")["revision"]
    preview = write("track.group",channels=[0,1],name="Chords",dryRun=True)
    assert not preview["changed"] and preview["data"]["wouldChange"] and read()==initial
    grouped = write("track.group",channels=[0,1],name="Chords")
    identity = grouped["data"]["affectedID"]
    assert grouped["changed"] and read()["noteTracks"][0]["id"]==identity
    assert read()["columns"][1]["noteColumn"]==1 and read()["columns"][1]["track"]==identity
    expect_error(-32001,lambda:client.call("track.ungroup",{"expectedRevision":rev,"track":identity}))
    expect_error(-32602,lambda:write("track.group",channels=[0,2]))
    expect_error(-32602,lambda:write("track.group",channels=[2,4]))
    expect_error(-32602,lambda:write("track.group",channels=[3,2]))
    expect_error(-32602,lambda:write("track.group",channels=[]))
    expect_error(-32602,lambda:write("track.group",channels=[True]))
    expect_error(-32602,lambda:write("track.column.set",column=initial["columns"][0]["id"],mute=1))
    transform = write("pattern.transform",operation="transpose",scope="note-track",track=identity,pattern=0,amount=12,dryRun=True)
    assert not transform["changed"] and all(c["channel"] in [0,1] for c in transform["data"]["changes"])
    assert transform["data"]["changedCells"]>0
    applied = write("pattern.transform",operation="transpose",scope="note-track",track=identity,pattern=0,amount=12)
    assert applied["changed"];undo()
    assert client.call("pattern.get",{"pattern":0})["data"]["cells"]==original_cells
    expect_error(-32602,lambda:write("pattern.transform",operation="clear",scope="note-track",track=identity,pattern=0,startChannel=0))
    current = read()
    master = next(b["id"] for b in client.call("mixer.get")["data"]["buses"] if b["kind"]=="master")
    expect_error(-32602,lambda:write("mixer.bus.set",bus=initial["columns"][0]["id"],output=master))
    assert read()==current
    muted = write("track.column.set",column=initial["columns"][0]["id"],mute=True)
    assert muted["changed"] and not muted["playbackStopped"] and read()["columns"][0]["mute"] and not read()["columns"][1]["mute"]
    assert not write("track.column.set",column=initial["columns"][0]["id"],mute=True)["changed"]
    undo();assert not read()["columns"][0]["mute"]
    write("history.redo",domain="document");assert read()["columns"][0]["mute"]
    undo()
    mixer = client.call("mixer.get")["data"]["buses"]
    write("track.ungroup",track=identity)
    assert not read()["noteTracks"] and client.call("mixer.get")["data"]["buses"]==mixer
    undo();assert read()["noteTracks"][0]["id"]==identity
    write("mixer.enable",enabled=False);assert not read()["noteTracks"]
    undo();assert read()["noteTracks"][0]["id"]==identity
    write("mixer.bus.remove",bus=identity);assert not read()["noteTracks"]
    undo();assert read()["noteTracks"][0]["id"]==identity
    pre_create = read()
    created_preview = write("track.create",columns=3,name="New",dryRun=True)
    assert not created_preview["changed"] and read()==pre_create
    created = write("track.create",columns=3,name="New")
    assert created["data"]["appendedColumns"]==3 and len(read()["columns"])==len(initial["columns"])+3
    assert read()["columns"][:len(initial["columns"])]==pre_create["columns"]
    pattern = client.call("pattern.get",{"pattern":0,"startChannel":len(initial["columns"]),"channelCount":3})["data"]
    assert all(not any(cell[k] for k in ["note","instrument","volumeCommand","volume","effect","parameter"]) for cell in pattern["cells"])
    undo();assert read()==pre_create
    expect_error(-32602,lambda:write("track.create",columns=128))
    expect_error(-32602,lambda:write("track.create",columns=127))
    undo();assert read()==initial
    after=client.call("document.get")["data"]
    for key in ["channels","tracks","patterns","trackLayout"]:
        assert after[key]==before[key],key
    assert client.call("pattern.get",{"pattern":0})["data"]["cells"]==original_cells
    print("PASS note-track socket: group/create/mute/ungroup, dry-run/stale/no-op/strict validation, routing protection, one-step Undo, mixer removal/disable, empty appended columns and intact original cells")


def navigation_pattern(client):
    original = client.call("context.get")["data"]
    document = client.call("document.get")
    created = client.call("pattern.create", {"expectedRevision":document["revision"],"rows":8})
    target = created["data"]["pattern"]
    def navigate(fields):
        context = client.call("context.get")
        return client.call("context.set", {"expectedRevision":context["revision"],"expectedContext":context["data"]["contextRevision"],**fields})
    navigate({"pattern":0,"row":31,"following":True})
    changed = navigate({"pattern":target})
    assert changed["data"]["pattern"] == target and changed["data"]["row"] == 7 and not changed["data"]["following"]
    assert changed["revision"] == created["revision"] and not changed["changed"]
    client.call("history.undo", {"expectedRevision":created["revision"],"domain":"document"})
    navigate({k:original[k] for k in ["pattern","row","channel","column","following"]})
    assert client.call("document.get")["data"]["patterns"] == document["data"]["patterns"]
    print("PASS independent pattern navigation: allocated pattern switch, shorter-pattern clamp, automatic follow off, unchanged song revision and restoration after structural undo")


def saving_tools(client, directory, app_test):
    revision = client.call("document.get")["revision"]
    path = directory / "agent-saved.screamseq"
    base = {"expectedRevision": revision, "path": str(path)}
    preview = client.call("document.save", {**base, "dryRun": True})
    assert not path.exists() and not preview["changed"] and not preview["data"]["written"]
    for extra in [{"path": "relative.resonance"}, {"path": str(path.with_suffix('.mod'))},
                  {"overwrite": 1}, {"dryRun": 1}, {"unexpected": True},
                  {"path": str(directory / 'missing' / 'song.resonance')}]:
        expect_error(-32602, lambda: client.call("document.save", {**base, **extra}))
    expect_error(-32001, lambda: client.call("document.save", {**base, "expectedRevision": "stale"}))
    result = client.call("document.save", base)
    assert result["revision"] == revision and not result["changed"] and result["data"]["written"]
    project = plistlib.loads(path.read_bytes())
    assert project['version'] == 4 and project['module'].startswith(b'RSONGS1\0')
    saved = path.read_bytes()
    expect_error(-32602, lambda: client.call("document.save", base))
    assert path.read_bytes() == saved
    assert client.call("document.save", {**base, "overwrite": True})["data"]["written"]
    if app_test:
        context = client.call("context.get")["data"]
        assert context["file"] == str(path) and not context["dirty"]
    # A separate export retains the native destination and document dirty state.
    raw = directory / "agent-export.mptm"
    export = {"expectedRevision": revision, "path": str(raw)}
    assert not client.call("document.exportModule", {**export, "dryRun": True})["data"]["written"]
    assert not raw.exists()
    assert client.call("document.exportModule", export)["data"]["written"]
    assert raw.read_bytes().startswith(b'IMPM')
    if app_test:
        assert client.call("context.get")["data"]["file"] == str(path)
    assert client.call("document.get")["revision"] == revision
    assert not list(directory.glob('.*.staged.*'))
    print("PASS native save/module export socket: dry run, strict paths/overwrite, stale revisions, v4 payload, unchanged history and native UI save destination")


def clipboard_tools(client):
    import base64
    def read(method, params=None):
        return client.call(method, params or {})
    def edit(method, params):
        return client.call(method, {"expectedRevision": read("document.get")["revision"], **params})
    def pcm():
        return base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"])
    def undo():
        return edit("history.undo", {"domain": "document"})
    original = bytes((n * 11 + 31) % 256 for n in range(20))
    edit("sample.pcm.set", {"sample": 1, "format": "s8", "channels": 2, "rate": 22050,
                            "data": base64.b64encode(original).decode()})
    edit("sample.patch", {"sample": 1, "values": {"loop": True, "loopStart": 3, "loopEnd": 8}})
    rev = read("document.get")["revision"]
    copied = edit("sample.clipboard.copy", {"sample": 1, "start": 1, "end": 4, "channels": "right"})
    assert not copied["changed"] and copied["revision"] == rev and not copied["playbackStopped"]
    clipboard = read("sample.clipboard.get", {"start": 0, "frames": 65536})["data"]
    assert clipboard["channels"] == 1 and clipboard["frames"] == 3 and clipboard["readFrames"] == 3
    source = original[3:8:2]
    assert base64.b64decode(clipboard["data"]) == source
    mono_to_stereo = bytes(v for x in source for v in (x, x))
    for mode in ["insert", "overwrite", "mix", "replace"]:
        p = {"sample": 1, "at": 2, "mode": mode, "clipboardId": clipboard["clipboardId"]}
        if mode == "replace": p["end"] = 4
        preview = edit("sample.paste", {**p, "dryRun": True})
        assert not preview["changed"] and pcm() == original
        data = preview["data"]
        assert data["insertedFrames"] == 3 and len(data["changes"]) <= 256
        if mode == "insert":
            expected = original[:4] + mono_to_stereo + original[4:]
            assert data["after"]["loopStart"] == 6 and data["after"]["loopEnd"] == 11
        elif mode == "replace":
            expected = original[:4] + mono_to_stereo + original[8:]
        elif mode == "overwrite":
            expected = original[:4] + mono_to_stereo + original[10:]
        else:
            signed = lambda b: b if b < 128 else b - 256
            mixed = bytes(max(-128, min(127, signed(a) + signed(b))) % 256
                          for a, b in zip(original[4:10], mono_to_stereo))
            expected = original[:4] + mixed + original[10:]
        applied = edit("sample.paste", p)
        assert applied["changed"] and pcm() == expected
        assert {k:v for k,v in applied["data"].items() if k != "dryRun"} == {k:v for k,v in data.items() if k != "dryRun"}
        undo(); assert pcm() == original
    rev = read("document.get")["revision"]
    # No-op overwrite preserves redo; clipboard mutations do not consume document history.
    same = edit("sample.clipboard.copy", {"sample": 1})["data"]
    noop = edit("sample.paste", {"sample": 1, "at": 0, "mode": "overwrite", "clipboardId": same["clipboardId"]})
    assert not noop["changed"] and noop["data"]["historyBytes"] == 0 and noop["revision"] == rev
    assert read("document.get")["data"]["canRedo"]
    valid_clip = {"format": "s8", "channels": 1, "rate": 22050, "data": "AQ==", "expectedRevision": rev}
    for extra in [{"format": "s16le"}, {"format": "f32le"}, {"channels": 3}, {"channels": True},
                  {"rate": 99}, {"rate": 768001}, {"rate": 44100.5}, {"data": ""}, {"data": "%%%"},
                  {"name": "x" * 201}, {"unexpected": 1}]:
        expect_error(-32602, lambda: read("sample.clipboard.set", {**valid_clip, **extra}))
        assert read("sample.clipboard.get")["data"]["clipboardId"] == same["clipboardId"]
        assert read("document.get")["revision"] == rev and pcm() == original
    invalid_paste = {"sample": 1, "at": 0, "clipboardId": same["clipboardId"], "expectedRevision": rev, "dryRun": True}
    for extra in [{"at": True}, {"at": 11}, {"end": 2}, {"mode": "replace"}, {"rateMode": "linear"},
                  {"channels": "left"}, {"sourceGainDB": 25}, {"destinationGainDB": 0}, {"dryRun": 1}]:
        expect_error(-32602, lambda: read("sample.paste", {**invalid_paste, **extra}))
    for method, params in [("sample.clipboard.get", {"frames": 65537}), ("sample.clipboard.get", {"start": 0}),
                           ("sample.cut", {"sample": 1, "start": 2, "end": 2, "expectedRevision": rev}),
                           ("sample.delete", {"sample": 1, "start": 0, "end": 11, "expectedRevision": rev})]:
        expect_error(-32602, lambda: read(method, params))
    cut_preview = edit("sample.cut", {"sample": 1, "start": 2, "end": 5, "dryRun": True})
    assert not cut_preview["changed"] and read("sample.clipboard.get")["data"]["clipboardId"] == same["clipboardId"]
    cut = edit("sample.cut", {"sample": 1, "start": 2, "end": 5})
    assert pcm() == original[:4] + original[10:]
    assert base64.b64decode(read("sample.clipboard.get", {"frames": 20})["data"]["data"]) == original[4:10]
    undo(); assert pcm() == original
    assert read("sample.clipboard.get")["data"]["clipboardId"] == cut["data"]["clipboardId"]
    # Clipboard identity changes independently of song revision, invalidating an old preview.
    expect_error(-32001, lambda: edit("sample.paste", {"sample": 1, "at": 0, "clipboardId": same["clipboardId"]}))
    edit("sample.delete", {"sample": 1, "start": 0, "end": 10})
    assert pcm() == b''
    imported = edit("sample.clipboard.set", {"format": "s8", "channels": 1, "rate": 11025,
                     "data": base64.b64encode(bytes([64] * 8)).decode(), "name": "Agent PCM"})
    assert not imported["changed"]
    preview = edit("sample.paste", {"sample": 1, "at": 0, "clipboardId": imported["data"]["clipboardId"], "dryRun": True})
    assert preview["data"]["insertedFrames"] == 16 and pcm() == b''
    edit("sample.paste", {"sample": 1, "at": 0, "clipboardId": imported["data"]["clipboardId"]})
    assert pcm() == bytes([64] * 32), "Resampled mono DC is duplicated into empty stereo sample"
    undo(); undo(); assert pcm() == original
    # Copy-to-new shares the core command but neither reads nor replaces the clipboard.
    clip_id = read("sample.clipboard.get")["data"]["clipboardId"]
    inventory = read("document.get")["data"]["samples"]
    revision = read("document.get")["revision"]
    request = {"sample": 1, "start": 2, "end": 9, "channels": "right", "name": "Copied right", "expectedRevision": revision}
    preview = read("sample.copyToNew", {**request, "dryRun": True})
    result = preview["data"]
    assert not preview["changed"] and result["frames"] == 7 and result["sampleChannels"] == 1
    assert result["loopStart"] == 1 and result["loopEnd"] == 6 and result["name"] == "Copied right"
    assert read("document.get")["data"]["samples"] == inventory and read("sample.clipboard.get")["data"]["clipboardId"] == clip_id
    for extra in [{"start": True}, {"start": 9, "end": 9}, {"end": 11}, {"channels": "middle"}, {"dryRun": 1}, {"name": "x" * 201}, {"slot": 0}]:
        expect_error(-32602, lambda: read("sample.copyToNew", {**request, **extra}))
    applied = read("sample.copyToNew", request)
    assert applied["changed"] and {k:v for k,v in applied["data"].items() if k != "dryRun"} == {k:v for k,v in result.items() if k != "dryRun"}
    assert base64.b64decode(read("sample.pcm.get", {"sample": result["sample"]})["data"]["data"]) == original[5:18:2]
    assert read("sample.clipboard.get")["data"]["clipboardId"] == clip_id and pcm() == original
    expect_error(-32001, lambda: read("sample.copyToNew", request))
    undo(); assert read("document.get")["data"]["samples"] == inventory
    edit("history.redo", {"domain": "document"})
    assert next(s for s in read("document.get")["data"]["samples"] if s["index"] == result["sample"])["id"] == result["id"]
    undo()
    print("PASS sample clipboard socket: copy/set/paged reads, four paste modes, exact previews/loops/PCM, cut/delete/undo, no-op redo, strict inputs, stale clipboard and empty-sample resampling")


def loop_tools(client):
    import base64, struct
    def read(method, params=None): return client.call(method, params or {})
    def edit(method, params): return read(method, {"expectedRevision": read("document.get")["revision"], **params})
    original = struct.pack("<" + "h"*2048, *[i*997 % 65535-32767 for i in range(2048)])
    edit("sample.pcm.set", {"sample": 1, "format": "s16le", "channels": 2, "rate": 22050, "data": base64.b64encode(original).decode()})
    assert "sample.loops.set" in read("api.describe")["data"]["writes"]
    normal = {"enabled": True, "start": 65, "end": 1000, "pingpong": False, "reverse": False}
    sustain = {"enabled": True, "start": 129, "end": 768, "pingpong": True, "reverse": False}
    before = read("sample.get", {"sample": 1})["data"]
    params = {"sample": 1, "normal": normal, "sustain": sustain, "expectedRevision": read("document.get")["revision"]}
    preview = read("sample.loops.set", {**params, "dryRun": True})
    assert not preview["changed"] and preview["data"]["after"] == {"normal": normal, "sustain": sustain}
    applied = client.call("sample.loops.set", params, "loop-retry")
    assert applied == client.call("sample.loops.set", params, "loop-retry")
    assert applied["changed"] and applied["data"]["after"] == preview["data"]["after"]
    assert base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"]) == original
    after = read("sample.get", {"sample": 1})["data"]
    assert after == {**before, "loop": True, "loopStart": 65, "loopEnd": 1000, "sustainLoop": True, "sustainStart": 129, "sustainEnd": 768, "sustainPingpong": True}
    expect_error(-32001, lambda: read("sample.loops.set", params))
    edit("history.undo", {"domain": "document"})
    assert read("sample.get", {"sample": 1})["data"] == before
    edit("history.redo", {"domain": "document"})
    edit("sample.loops.set", {"sample": 1, "normal": {**normal, "start": 66}})
    edit("history.undo", {"domain": "document"})
    noop = edit("sample.loops.set", {"sample": 1, "normal": normal})
    assert not noop["changed"] and read("document.get")["data"]["canRedo"]
    assert read("sample.get", {"sample": 1})["data"] == after
    revision = read("document.get")["revision"]
    bad = [{}, {"normal": {}}, {"normal": None}, {"normal": {**normal, "start": -1}},
           {"normal": {**normal, "start": True}}, {"normal": {**normal, "end": 1025}},
           {"normal": {**normal, "end": 65}}, {"normal": {**normal, "enabled": 1}},
           {"normal": {**normal, "enabled": False, "pingpong": True}},
           {"normal": {**normal, "mode": "reverse"}}, {"normal": normal, "dryRun": 1},
           {"normal": normal, "sustain": {**sustain, "start": 800}}, {"extra": 1}]
    for fields in bad:
        expect_error(-32602, lambda: read("sample.loops.set", {"sample": 1, "expectedRevision": revision, **fields}))
    assert read("document.get")["revision"] == revision and read("document.get")["data"]["canRedo"]
    assert read("sample.get", {"sample": 1})["data"] == after
    edit("sample.loops.set", {"sample": 1, "sustain": {**sustain, "pingpong": False}})
    fade = edit("sample.crossfade", {"sample": 1, "loop": "sustain", "frames": 32, "mode": "overlap"})
    assert fade["changed"] and read("sample.get", {"sample": 1})["data"]["sustainStart"] == 161
    edit("history.undo", {"domain": "document"})
    assert base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"]) == original
    for held in [False, True]:
        key = "sustain" if held else "normal"
        current = {**(sustain if held else normal), "pingpong": False, "reverse": True}
        params = {"sample": 1, key: current}
        preview = edit("sample.loops.set", {**params, "dryRun": True})
        applied = edit("sample.loops.set", params)
        assert preview["data"]["after"] == applied["data"]["after"] and applied["changed"]
        assert read("sample.get", {"sample": 1})["data"]["sustainReverse" if held else "reverseLoop"]
        expect_error(-32602, lambda: edit("sample.crossfade", {"sample": 1, "loop": key, "frames": 32}))
        for extra in [{"reverse": 1}, {"reverse": True, "pingpong": True}, {"reverse": True, "enabled": False}]:
            expect_error(-32602, lambda: edit("sample.loops.set", {"sample": 1, key: {**current, **extra}}))
        edit("history.undo", {"domain": "document"})
        assert not read("sample.get", {"sample": 1})["data"]["sustainReverse" if held else "reverseLoop"]
    print("PASS normal/sustain loop API: atomic preview/apply/history, retry/stale, omitted targets, no-op/redo, crossfade integration and strict fields")


def crossfade_tools(client):
    import base64, math, struct
    def read(method, params=None): return client.call(method, params or {})
    def edit(method, params): return read(method, {"expectedRevision": read("document.get")["revision"], **params})
    def pcm(): return base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"])
    for bits in [8, 16]:
        for channels in [1, 2]:
            scale = 1 << (bits-1)
            values = [(i*997) % (2*scale) - scale for i in range(1024*channels)]
            format = "<" + ("b" if bits == 8 else "h") * len(values)
            original = struct.pack(format, *values)
            edit("sample.pcm.set", {"sample": 1, "format": "s8" if bits == 8 else "s16le", "channels": channels, "rate": 22050, "data": base64.b64encode(original).decode()})
            edit("sample.patch", {"sample": 1, "values": {"loop": True, "loopStart": 256, "loopEnd": 1005}})
            metadata = read("sample.get", {"sample": 1})["data"]
            for mode in ["preserve", "overlap"]:
                for curve in ["linear", "equal-power"]:
                    params = {"sample": 1, "frames": 129, "mode": mode, "curve": curve, "expectedRevision": read("document.get")["revision"]}
                    preview = read("sample.crossfade", {**params, "dryRun": True})
                    assert not preview["changed"] and pcm() == original and preview["data"]["loopBefore"] == {"start": 256, "end": 1005, "frames": 749}
                    expected = values[:]
                    for i in range(129):
                        for c in range(channels):
                            a, b = values[(876+i)*channels+c], values[((127 if mode == "preserve" else 256)+i)*channels+c]
                            value = (a*(128-i)+b*i)/128 if curve == "linear" else a*math.cos(i/128*math.pi/2)+b*math.sin(i/128*math.pi/2)
                            if i == 0: value = a
                            if i == 128: value = b
                            value = max(-scale, min(scale-1, value))
                            expected[(876+i)*channels+c] = math.floor(value+.5) if value >= 0 else math.ceil(value-.5)
                    request_id = f"crossfade-{bits}-{channels}-{mode}-{curve}"
                    applied = client.call("sample.crossfade", params, request_id)
                    assert applied == client.call("sample.crossfade", params, request_id), "Uncertain crossfade retry cannot apply twice"
                    assert applied["changed"] and pcm() == struct.pack(format, *expected)
                    assert {k:v for k,v in applied["data"].items() if k != "dryRun"} == {k:v for k,v in preview["data"].items() if k != "dryRun"}
                    after = read("sample.get", {"sample": 1})["data"]
                    assert after == {**metadata, "loopStart": 385 if mode == "overlap" else 256}
                    assert applied["data"]["loopAfter"] == {"start": after["loopStart"], "end": 1005, "frames": 1005-after["loopStart"]}
                    expect_error(-32001, lambda: read("sample.crossfade", params))
                    edit("history.undo", {"domain": "document"})
                    assert pcm() == original and read("sample.get", {"sample": 1})["data"] == metadata
            revision = read("document.get")["revision"]
            for extra in [{"frames": 0}, {"frames": 1}, {"frames": True}, {"frames": 2.5}, {"frames": 1048577}, {"frames": 257},
                          {"mode": "unknown"}, {"loop": "bad"}, {"loop": "sustain"}, {"curve": "sqrt"}, {"dryRun": 1}, {"start": 0}, {"channels": "left"},
                          {"mode": "overlap", "frames": 375}]:
                expect_error(-32602, lambda: read("sample.crossfade", {"sample": 1, "frames": 64, "expectedRevision": revision, **extra}))
            assert pcm() == original and read("document.get")["revision"] == revision and read("document.get")["data"]["canRedo"]
    # Constant PCM: moving the loop is still an edit, preserving its length is not.
    constant = struct.pack("<" + "h"*2048, *([12000]*2048))
    edit("sample.pcm.set", {"sample": 1, "format": "s16le", "channels": 2, "rate": 22050, "data": base64.b64encode(constant).decode()})
    edit("sample.patch", {"sample": 1, "values": {"loop": True, "loopStart": 0, "loopEnd": 1024}})
    expect_error(-32602, lambda: edit("sample.crossfade", {"sample": 1, "frames": 64, "mode": "preserve"}))
    changed = edit("sample.crossfade", {"sample": 1, "frames": 64, "mode": "overlap"})
    assert changed["changed"] and changed["data"]["changedFrames"] == 0 and changed["data"]["loopChanged"] and pcm() == constant
    second = edit("sample.crossfade", {"sample": 1, "frames": 64, "mode": "overlap"})
    edit("history.undo", {"domain": "document"})
    revision = read("document.get")["revision"]
    no_op = edit("sample.crossfade", {"sample": 1, "frames": 64, "mode": "preserve"})
    assert not no_op["changed"] and no_op["revision"] == revision and read("document.get")["data"]["canRedo"]
    edit("sample.patch", {"sample": 1, "values": {"pingpong": True}})
    expect_error(-32602, lambda: edit("sample.crossfade", {"sample": 1, "frames": 64, "mode": "overlap"}))
    edit("sample.patch", {"sample": 1, "values": {"pingpong": False}})
    print("PASS crossfade socket: exact PCM/geometry, preview/apply/retry/stale, period changes, constant-audio no-op/redo, strict parameters and invalid loop rejection")


def snapping_tools(client):
    import base64, struct
    def read(method, params=None): return client.call(method, params or {})
    def edit(method, params): return read(method, {"expectedRevision": read("document.get")["revision"], **params})
    for bits in [8, 16]:
        for channels in [1, 2]:
            frames = 513
            values = [((f * 13 + c * 29) % 127 - 63) * (1 if bits == 8 else 257) for f in range(frames) for c in range(channels)]
            raw = struct.pack("<" + ("b" if bits == 8 else "h") * len(values), *values)
            edit("sample.pcm.set", {"sample": 1, "format": "s8" if bits == 8 else "s16le", "channels": channels, "rate": 22050, "data": base64.b64encode(raw).decode()})
            edit("sample.draw", {"sample": 1, "points": [{"frame": 0, "value": 0.5}]})
            edit("history.undo", {"domain": "document"})
            state = read("document.get")
            assert state["data"]["canRedo"]
            positions = [0, 1, 2, 17, 19, 255, 511, 512, 513, 17]
            for channel in (["both", "left"] if channels == 1 else ["both", "left", "right"]):
                chosen = list(range(channels)) if channel == "both" else [0 if channel == "left" else 1]
                boundaries = [0] + [f for f in range(1, frames) if all(values[(f-1)*channels+c] * values[f*channels+c] <= 0 for c in chosen)] + [frames]
                for direction in ["nearest", "before", "after"]:
                    for radius in [0, 7, 65536]:
                        result = read("sample.snap.get", {"sample": 1, "positions": positions, "channels": channel, "direction": direction, "radius": radius})
                        expected = []
                        for position in positions:
                            candidates = [b for b in boundaries if abs(b-position) <= radius and (direction != "before" or b <= position) and (direction != "after" or b >= position)]
                            expected.append({"before": position, "after": min(candidates, key=lambda b: (abs(b-position), b)) if candidates else position, "matched": bool(candidates)})
                        assert result["data"]["positions"] == expected and not result["changed"] and not result["playbackStopped"] and result["revision"] == state["revision"]
            for direction in ["nearest", "before", "after"]:
                result = read("sample.snap.get", {"sample": 1, "positions": positions, "mode": "grid", "step": 32, "origin": 17, "direction": direction})
                candidates = list(range(17, frames+1, 32))
                for before, after in zip(positions, result["data"]["positions"]):
                    eligible = [p for p in candidates if (direction != "before" or p <= before) and (direction != "after" or p >= before)]
                    assert after == {"before": before, "after": min(eligible, key=lambda p: (abs(p-before), p)) if eligible else before, "matched": bool(eligible)}
            for extra in [{"positions": []}, {"positions": [0]*65}, {"positions": [True]}, {"positions": [1.5]}, {"positions": [-1]}, {"positions": [514]},
                          {"radius": True}, {"radius": 65537}, {"radius": -1}, {"mode": "bad"}, {"direction": "bad"}, {"channels": "mid"},
                          {"step": 8}, {"origin": 0}, {"expectedRevision": state["revision"]}, {"mode": "grid"},
                          {"mode": "grid", "step": 0}, {"mode": "grid", "step": True}, {"mode": "grid", "step": 8, "origin": 514},
                          {"mode": "grid", "step": 8, "radius": 1}, {"mode": "grid", "step": 8, "channels": "both"}]:
                expect_error(-32602, lambda: read("sample.snap.get", {"sample": 1, "positions": [17], **extra}))
            if channels == 1:
                expect_error(-32602, lambda: read("sample.snap.get", {"sample": 1, "positions": [17], "channels": "right"}))
            assert read("document.get")["revision"] == state["revision"] and read("document.get")["data"]["canRedo"]
            assert base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"]) == raw
    print("PASS sample snapping socket: independent zero/grid boundaries, stereo choices, ties/directions/endpoints, bounded search, strict input and unchanged revision/PCM/redo")


def drawing_tools(client):
    import base64, struct
    def read(method, params=None): return client.call(method, params or {})
    def edit(method, params): return read(method, {"expectedRevision": read("document.get")["revision"], **params})
    def pcm(): return base64.b64decode(read("sample.pcm.get", {"sample": 1})["data"]["data"])
    values = [((f * 997 + c * 123) % 65535) - 32767 for f in range(1024) for c in range(2)]
    original = struct.pack("<" + "h" * len(values), *values)
    edit("sample.pcm.set", {"sample": 1, "format": "s16le", "channels": 2, "rate": 22050, "data": base64.b64encode(original).decode()})
    edit("sample.patch", {"sample": 1, "values": {"loop": True, "loopStart": 20, "loopEnd": 1005}})
    metadata = read("sample.get", {"sample": 1})["data"]
    for mode in ["linear", "step"]:
        params = {"sample": 1, "channels": "right", "interpolation": mode,
                  "points": [{"frame": 250, "value": -0.5}, {"frame": 506, "value": 0.5}, {"frame": 762, "value": -0.5}],
                  "expectedRevision": read("document.get")["revision"]}
        preview = read("sample.draw", {**params, "dryRun": True})
        assert not preview["changed"] and pcm() == original and preview["data"]["previewTruncated"]
        assert preview["data"]["start"] == 250 and preview["data"]["end"] == 763 and len(preview["data"]["changes"]) == 256
        expected = values[:]
        for f in range(250, 763):
            expected[f * 2 + 1] = ((f - 250) * 128 - 16384 if f <= 506 else 16384 - (f - 506) * 128) if mode == "linear" else (-16384 if f < 506 or f == 762 else 16384)
        applied = client.call("sample.draw", params, "drawing-" + mode)
        assert applied == client.call("sample.draw", params, "drawing-" + mode), "Retry cannot create a second stroke"
        assert applied["changed"] and pcm() == struct.pack("<" + "h" * len(expected), *expected)
        assert {k:v for k,v in applied["data"].items() if k != "dryRun"} == {k:v for k,v in preview["data"].items() if k != "dryRun"}
        assert read("sample.get", {"sample": 1})["data"] == metadata
        expect_error(-32001, lambda: read("sample.draw", params))
        edit("history.undo", {"domain": "document"}); assert pcm() == original
    revision = read("document.get")["revision"]
    same = {"sample": 1, "channels": "left", "points": [{"frame": 17, "value": values[34] / 32768}], "expectedRevision": revision}
    noop = read("sample.draw", same)
    assert not noop["changed"] and noop["revision"] == revision and read("document.get")["data"]["canRedo"]
    for points in [[], [{"frame": True, "value": 0}], [{"frame": 1.5, "value": 0}], [{"frame": 1024, "value": 0}],
                   [{"frame": 1, "value": True}], [{"frame": 1, "value": 1.01}], [{"frame": 1}],
                   [{"frame": 2, "value": 0}, {"frame": 1, "value": 0}], [{"frame": 1, "value": 0}, {"frame": 1, "value": 1}],
                   [{"frame": 1, "value": 0, "unexpected": 1}], [{"frame": 1, "value": 0}] * 4097]:
        expect_error(-32602, lambda: read("sample.draw", {**same, "points": points}))
    for extra in [{"channels": "mid"}, {"interpolation": "spline"}, {"dryRun": 1}, {"start": 0}]:
        expect_error(-32602, lambda: read("sample.draw", {**same, **extra}))
    assert read("document.get")["revision"] == revision and pcm() == original
    clipped = edit("sample.draw", {"sample": 1, "points": [{"frame": 0, "value": 1}]})
    assert clipped["data"]["clippedSamples"] == 2 and pcm()[:4] == struct.pack("<hh", 32767, 32767)
    edit("history.undo", {"domain": "document"}); assert pcm() == original
    print("PASS sample drawing socket: exact linear/step/channel PCM, preview/apply, bounded changes, clipping, unchanged settings, one-step undo, no-op redo, retry/stale and strict validation")


def row_tools(client):
    def write(method, params):
        return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})
    def pattern(): return client.call("pattern.get", {"pattern": 0})["data"]
    original = pattern()
    seed = [{"pattern": 0, "row": r, "channel": c, "note": 40+r, "instrument": c,
             "volumeCommand": 1, "volume": 20+r, "effect": 0, "parameter": 0}
            for r in range(3, 11) for c in [1, 2]]
    seed += [{"pattern": 0, "row": r, "channel": 1, "note": 0, "instrument": 0,
              "volumeCommand": 0, "volume": 0, "effect": 0, "parameter": 0} for r in range(60, 64)]
    write("pattern.apply", {"cells": seed})
    seeded = pattern()
    for operation in ["insertRows", "deleteRows"]:
        before = client.call("document.get")
        params = {"expectedRevision": before["revision"], "pattern": 0, "scope": "selection",
                  "startRow": 3, "rowCount": 8, "startChannel": 1, "channelCount": 2,
                  "operation": operation, "amount": 2, "fields": ["note", "instrument"]}
        expect_error(-32602, lambda: client.call("pattern.transform", {**params, "dryRun": True}))
        assert client.call("document.get") == before and pattern() == seeded
        params["allowDataLoss"] = True
        for bad in [True, -1, 0, 1.5, 9, "2", None]:
            expect_error(-32602, lambda: client.call("pattern.transform", {**params, "amount": bad}))
        expect_error(-32602, lambda: client.call("pattern.transform", {**params, "allowDataLoss": 1}))
        expected = {(c["row"], c["channel"]): dict(c) for c in seeded["cells"]}
        for channel in [1, 2]:
            values = [{k: expected[r, channel][k] for k in ["note", "instrument"]} for r in range(3, 11)]
            blank = {"note": 0, "instrument": 0}
            shifted = ([blank, blank] + values)[:8] if operation == "insertRows" else values[2:] + [blank, blank]
            for row, values in enumerate(shifted, 3): expected[row, channel].update(values)
        preview = client.call("pattern.transform", {**params, "dryRun": True})
        assert not preview["changed"] and preview["revision"] == before["revision"] and pattern() == seeded
        applied = client.call("pattern.transform", params, "rows-" + operation)
        assert client.call("pattern.transform", params, "rows-" + operation) == applied
        expect_error(-32001, lambda: client.call("pattern.transform", params))
        assert pattern()["cells"] == list(expected.values())
        assert applied["data"]["changes"] == preview["data"]["changes"] and applied["data"]["changedCells"] == 16
        write("history.undo", {"domain": "document"}); assert pattern() == seeded
        redo_before = client.call("document.get")
        noop = write("pattern.transform", {"pattern": 0, "scope": "selection", "startRow": 60, "rowCount": 4,
                     "startChannel": 1, "channelCount": 1, "operation": operation, "amount": 4})
        assert not noop["changed"] and noop["data"]["changedCells"] == 0 and client.call("document.get") == redo_before
        write("history.redo", {"domain": "document"}); assert pattern()["cells"] == list(expected.values())
        write("history.undo", {"domain": "document"})
    write("history.undo", {"domain": "document"}); assert pattern() == original
    print("PASS row tools socket: insert/delete, exact masked previews, tail/removal loss guards, strict counts, preserved exterior fields, retry/stale/no-op/one-step history")


def pattern_tools(client):
    initial = client.call("pattern.get", {"pattern": 0})
    base = {"expectedRevision": initial["revision"], "scope": "selection", "pattern": 0,
            "startRow": 0, "rowCount": 8, "startChannel": 0, "channelCount": 1}
    ramp = {**base, "operation": "interpolate", "target": "volume", "only": "notes",
            "from": 4, "to": 64, "curve": "exponential", "dryRun": True}
    preview = client.call("pattern.transform", ramp)
    assert not preview["changed"] and preview["revision"] == initial["revision"]
    assert client.call("pattern.get", {"pattern": 0}) == initial
    assert [c["after"]["volume"] for c in preview["data"]["changes"]] == [4, 64]
    applied = client.call("pattern.transform", {**ramp, "dryRun": False})
    assert applied["data"]["changes"] == preview["data"]["changes"]
    expect_error(-32001, lambda: client.call("pattern.transform", {**ramp, "dryRun": False}))
    client.call("history.undo", {"expectedRevision": applied["revision"], "domain": "document"})
    restored = client.call("pattern.get", {"pattern": 0})
    assert restored["data"] == initial["data"]
    base["expectedRevision"] = restored["revision"]
    random = {**base, "operation": "randomize", "target": "volume", "from": 4, "to": 60,
              "seed": 9123, "only": "notes", "dryRun": True}
    assert client.call("pattern.transform", random)["data"] == client.call("pattern.transform", random)["data"]
    for extra in [{"seed": True}, {"seed": -1}, {"seed": 1.5}, {"seed": 4294967296}, {"typo": 1}]:
        expect_error(-32602, lambda: client.call("pattern.transform", {**random, **extra}))
    expect_error(-32602, lambda: client.call("pattern.transform", {**base, "operation": "reverse", "fields": ["note", "note"]}))
    expect_error(-32602, lambda: client.call("pattern.transform", {**base, "operation": "expand", "amount": 2}))
    expect_error(-32602, lambda: client.call("pattern.transform", {**base, "operation": "reverse", "startRow": 63}))
    expect_error(-32602, lambda: client.call("pattern.transform", {**base, "operation": "unknown"}))
    assert client.call("pattern.get", {"pattern": 0})["data"] == initial["data"]
    paste = {"expectedRevision": base["expectedRevision"], "pattern": 0, "startRow": 0, "startChannel": 0,
             "rows": 1, "channels": 1, "cells": [[60, 2, 1, 12, 1, 0x34]], "mode": "mix", "dryRun": True}
    mixed = client.call("pattern.paste", paste)
    assert mixed["data"]["changedCells"] == 1
    before, after = (mixed["data"]["changes"][0][k] for k in ["before", "after"])
    assert all(before[k] == after[k] for k in ["note", "instrument", "volumeCommand", "volume"])
    assert after["effect"] == 1 and after["parameter"] == 0x34
    changed = client.call("pattern.paste", {**paste, "dryRun": False})
    client.call("history.undo", {"expectedRevision": changed["revision"], "domain": "document"})
    assert client.call("pattern.get", {"pattern": 0})["data"] == initial["data"]
    revision = client.call("document.get")["revision"]
    created = client.call("pattern.create", {"expectedRevision": revision, "rows": 1024})
    pattern = created["data"]["pattern"]
    fill = {"expectedRevision": created["revision"], "pattern": pattern, "scope": "pattern",
            "operation": "fill", "target": "volume", "only": "all", "from": 20, "dryRun": True}
    large = client.call("pattern.transform", fill)
    assert large["data"]["changedCells"] == 8192 and large["data"]["previewTruncated"]
    assert len(large["data"]["changes"]) == 512 and not large["changed"]
    filled = client.call("pattern.transform", {**fill, "dryRun": False})
    assert filled["data"]["changedCells"] == 8192
    last = client.call("pattern.get", {"pattern": pattern, "startRow": 1023, "rowCount": 1})
    assert all(c["volumeCommand"] == 1 and c["volume"] == 20 for c in last["data"]["cells"])
    undone = client.call("history.undo", {"expectedRevision": filled["revision"], "domain": "document"})
    last = client.call("pattern.get", {"pattern": pattern, "startRow": 1023, "rowCount": 1})
    assert all(c["volumeCommand"] == 0 for c in last["data"]["cells"])
    client.call("history.undo", {"expectedRevision": undone["revision"], "domain": "document"})
    assert client.call("pattern.get", {"pattern": 0})["data"] == initial["data"]
    print("PASS pattern tools through socket: exact preview/apply, stale revisions, seeded generation, masks, loss guards, invalid requests, mix paste, bounded large previews and atomic undo")


def sample_tools(client):
    import base64
    import math
    import struct

    original = client.call("sample.pcm.get", {"sample": 1})["data"]
    def revision():
        return client.call("document.get")["revision"]
    def mutate(method, params):
        return client.call(method, {**params, "expectedRevision": revision()})
    def pcm():
        data = client.call("sample.pcm.get", {"sample": 1})["data"]
        raw = base64.b64decode(data["data"])
        return list(struct.unpack("<" + ("h" if data["format"] == "s16le" else "b") * (len(raw) // (2 if data["format"] == "s16le" else 1)), raw))
    def undo():
        return mutate("history.undo", {"domain": "document"})
    for bits in [8, 16]:
        scale = 2 ** (bits-1)
        left = [-scale, scale-1, 10, -20, 30, -40, 0, 60]
        source = [x for pair in zip(left, [7] * len(left)) for x in pair]
        raw = struct.pack("<" + ("h" if bits == 16 else "b") * len(source), *source)
        mutate("sample.pcm.set", {"sample": 1, "format": "s16le" if bits == 16 else "s8", "channels": 2, "rate": 22050, "data": base64.b64encode(raw).decode(), "name": "API sample processing fixture"})
        before = client.call("sample.get", {"sample": 1})
        for channel, selected in [("left", [0]), ("right", [1]), ("both", [0, 1])]:
            waveform = client.call("sample.waveform.get", {"sample": 1, "start": 1, "end": 7, "bins": 3, "channels": channel})
            expected = []
            for a in [1, 3, 5]:
                values = [source[i*2+c] / scale for i in [a, a+1] for c in selected]
                expected.extend([min(values), max(values)])
            assert waveform["data"]["peaks"] == expected and waveform["revision"] == before["revision"]
        request = {"sample": 1, "operation": "silence", "start": 2, "end": 3, "channels": "left", "expectedRevision": before["revision"]}
        preview = client.call("sample.process", {**request, "dryRun": True})
        assert not preview["changed"] and preview["revision"] == before["revision"] and not preview["playbackStopped"]
        assert preview["data"]["changes"] == [{"frame": 2, "channel": 0, "before": 10, "after": 0}]
        assert preview["data"]["changedFrames"] == preview["data"]["changedSamples"] == 1
        assert preview["data"]["patchBytes"] < 8192 and not preview["data"]["previewTruncated"]
        assert pcm() == source and client.call("sample.get", {"sample": 1}) == before
        for extra in [{"operation": "bogus"}, {"start": 8}, {"end": 0}, {"start": True}, {"end": 1.5}, {"channels": "mid"}, {"gainDB": 1}, {"curve": "linear"}, {"window": 5}, {"targetDB": 0}, {"exponent": 3}, {"dryRun": 1}, {"typo": 1}]:
            expect_error(-32602, lambda: client.call("sample.process", {**request, **extra}))
        applied = client.call("sample.process", request, f"sample-silence-{bits}")
        assert client.call("sample.process", request, f"sample-silence-{bits}") == applied
        assert applied["data"]["changes"] == preview["data"]["changes"]
        after = source.copy(); after[4] = 0
        assert pcm() == after
        expect_error(-32001, lambda: client.call("sample.process", request))
        noop = client.call("sample.process", {**request, "expectedRevision": applied["revision"]})
        assert not noop["changed"] and noop["revision"] == applied["revision"]
        undo(); assert pcm() == source
        rev = revision()
        noop = mutate("sample.process", {"sample": 1, "operation": "gain", "gainDB": 0})
        assert noop["revision"] == rev and client.call("document.get")["data"]["canRedo"]
        mutate("history.redo", {"domain": "document"}); assert pcm() == after
        undo()
        # DC removal has an independently obvious constant-channel result.
        mutate("sample.process", {"sample": 1, "operation": "remove-dc", "channels": "right"})
        assert pcm() == [x for pair in zip(left, [0] * len(left)) for x in pair]
        undo()
        inversion = mutate("sample.process", {"sample": 1, "operation": "invert", "start": 0, "end": 1, "channels": "left"})
        assert inversion["data"]["clippedSamples"] == 1 and pcm()[0] == scale-1
        undo()
        for operation, expected in [("swap-channels", [x for pair in zip([7]*len(left), left) for x in pair]), ("copy-left", [x for a in left for x in [a, a]]), ("copy-right", [7]*len(source))]:
            mutate("sample.process", {"sample": 1, "operation": operation}); assert pcm() == expected
            undo()
        for operation, extra in [("gain", {"gainDB": True}), ("normalize", {"targetDB": 1}), ("smooth", {"window": 4}), ("fade-in", {"curve": "linear", "exponent": 3}), ("fade-out", {"curve": "exponential", "exponent": 9}), ("trim", {"channels": "left"})]:
            expect_error(-32602, lambda: mutate("sample.process", {"sample": 1, "operation": operation, **extra}))
        for extra in [{"bins": 0}, {"bins": 16385}, {"bins": True}, {"start": 5, "end": 4}, {"end": 9}, {"channels": "mid"}, {"typo": 1}]:
            expect_error(-32602, lambda: client.call("sample.waveform.get", {"sample": 1, **extra}))
        assert client.call("sample.waveform.get", {"sample": 1, "start": 4, "end": 4, "bins": 2})["data"]["peaks"] == [0, 0, 0, 0]
        trimmed = mutate("sample.process", {"sample": 1, "operation": "trim", "start": 2, "end": 7, "dryRun": True})
        assert trimmed["data"]["removedFrames"] == 3 and not trimmed["changed"] and pcm() == source
        mutate("sample.process", {"sample": 1, "operation": "trim", "start": 2, "end": 7}); assert pcm() == source[4:14]
        undo(); assert pcm() == source
        undo(); assert client.call("sample.pcm.get", {"sample": 1})["data"] == original
    raw = struct.pack("<600h", *([1000]*600))
    mutate("sample.pcm.set", {"sample": 1, "format": "s16le", "channels": 1, "rate": 48000, "data": base64.b64encode(raw).decode()})
    p = mutate("sample.process", {"sample": 1, "operation": "silence", "dryRun": True})["data"]
    assert p["changedFrames"] == p["changedSamples"] == 600 and len(p["changes"]) == 256 and p["previewTruncated"]
    for operation in ["swap-channels", "copy-left", "copy-right", "stereo-average"]:
        expect_error(-32602, lambda: mutate("sample.process", {"sample": 1, "operation": operation}))
    expect_error(-32602, lambda: mutate("sample.process", {"sample": 1, "operation": "gain", "channels": "right"}))
    expect_error(-32602, lambda: client.call("sample.waveform.get", {"sample": 1, "channels": "right"}))
    for curve in ["linear", "smooth", "exponential", "logarithmic"]:
        args = {"sample": 1, "operation": "fade-in", "start": 5, "end": 10, "curve": curve}
        if curve in ["exponential", "logarithmic"]:
            args["exponent"] = 2
        preview = mutate("sample.process", {**args, "dryRun": True})
        applied = mutate("sample.process", args)
        assert preview["data"]["changes"] == applied["data"]["changes"]
        weights = {"linear": [0, .25, .5, .75, 1], "smooth": [0, .15625, .5, .84375, 1], "exponential": [0, .0625, .25, .5625, 1], "logarithmic": [0, .4375, .75, .9375, 1]}[curve]
        assert pcm() == [1000]*5 + [math.floor(x*1000+.5) for x in weights] + [1000]*590
        undo()
    undo(); assert client.call("sample.pcm.get", {"sample": 1})["data"] == original
    print("PASS sample socket: exact preview/apply, ranges/channels/curves, clipping, strict options, bounded replies, retry/stale protection, no-op redo, waveform and PCM undo")


def arrangement_tools(client):
    document = client.call("document.get")
    original = client.call("arrangement.get")
    slot = original["data"]["orders"][0]["id"]
    labelled = client.call("song.annotate", {"expectedRevision": original["revision"], "id": slot, "name": "Intro", "color": 0x52cdb4})
    assert labelled["changed"]
    same = client.call("song.annotate", {"expectedRevision": labelled["revision"], "id": slot, "name": "Intro"})
    assert not same["changed"] and same["revision"] == labelled["revision"]
    for bad in [{"id": "n0001"}, {"color": True}, {"color": 0x1000000}, {"name": "x" * 257}, {"typo": 1}]:
        expect_error(-32602, lambda: client.call("song.annotate", {"expectedRevision": labelled["revision"], "id": slot, "name": "Intro", **bad}))
    inserted = client.call("order.edit", {"expectedRevision": labelled["revision"], "order": 0, "pattern": 0, "operation": "before"})
    arrangement = client.call("arrangement.get")["data"]
    assert arrangement["orders"][1]["id"] == slot and arrangement["sections"][0]["firstOrder"] == 1
    moved = client.call("order.edit", {"expectedRevision": inserted["revision"], "order": 1, "operation": "up"})
    assert client.call("arrangement.get")["data"]["sections"][0]["firstOrder"] == 0
    matrix = client.call("arrangement.matrix", {"orderCount": 2, "channelCount": 8})["data"]
    assert len(matrix["orders"]) == 2 and matrix["orders"][0]["blocks"][0]["notes"] == 16
    assert sum(matrix["orders"][0]["blocks"][0]["bins"]) == matrix["orders"][0]["blocks"][0]["events"]
    expect_error(-32602, lambda: client.call("arrangement.matrix", {"orderCount": 129}))
    copy = {"expectedRevision": moved["revision"], "sourceOrder": 0, "sourceChannel": 0,
            "targetOrder": 1, "targetChannel": 7, "dryRun": True}
    preview = client.call("arrangement.copyBlock", copy)
    assert preview["data"]["clonesPattern"] and preview["data"]["changedCells"] == 16 and not preview["changed"]
    applied = client.call("arrangement.copyBlock", {**copy, "dryRun": False})
    matrix = client.call("arrangement.matrix", {"orderCount": 2, "channelCount": 8})["data"]
    assert matrix["orders"][0]["pattern"] != matrix["orders"][1]["pattern"]
    assert matrix["orders"][0]["blocks"][7]["notes"] == 0 and matrix["orders"][1]["blocks"][7]["notes"] == 16
    client.call("history.undo", {"expectedRevision": applied["revision"], "domain": "document"})
    for _ in range(3):
        current = client.call("document.get")["revision"]
        client.call("history.undo", {"expectedRevision": current, "domain": "document"})
    assert client.call("arrangement.get")["data"] == original["data"]
    assert client.call("document.get")["data"]["patterns"] == document["data"]["patterns"]
    print("PASS arrangement socket: stable identities, annotations, no-op revisions, invalid input, section movement and unified document undo")


def musical_automation(client):
    revision = client.call("document.get")["revision"]
    plugin = {"format": "VST3", "type": 0, "subtype": 0, "manufacturer": 0,
              "name": "Resonance Test Gain", "path": str(BUILD / "test-plugins/ResonanceFixture.vst3"),
              "classID": "5245534F4E414E434546464543540001", "isInstrument": False}
    client.call("plugin.add", {"expectedRevision": revision, "descriptor": plugin})
    document = client.call("document.get")
    instance = document["data"]["nativePlugins"][0]["instanceID"]
    params = {"expectedRevision": document["revision"], "pattern": 0, "plugin": instance, "parameter": 7,
              "points": [{"position": 0, "value": .1, "curve": "exponential"}, {"position": 63 * 256, "value": .9}]}
    preview = client.call("automation.pattern.set", {**params, "dryRun": True})
    assert not preview["changed"] and preview["data"]["wouldChange"]
    for points in [[], [{"position": 0, "value": 2}], [{"position": True, "value": .5}],
                   [{"position": 16384, "value": .5}], [{"position": 0, "value": .5, "curve": "unknown"}],
                   [{"position": 5, "value": .1}, {"position": 5, "value": .2}],
                   [{"position": 5, "value": .1}, {"position": 4, "value": .2}]]:
        expect_error(-32602, lambda: client.call("automation.pattern.set", {**params, "points": points}))
    applied = client.call("automation.pattern.set", params)
    expect_error(-32001, lambda: client.call("automation.pattern.set", params))
    lane = client.call("automation.pattern.get", {"pattern": 0})["data"]["lanes"][0]
    assert lane["resolved"] and lane["plugin"] == instance and lane["points"][0]["curve"] == "exponential"
    expect_error(-32602, lambda: client.call("automation.replaceLane", {"expectedRevision": applied["revision"],
        "slot": 0, "id": 7, "points": [{"frame": 0, "value": .5}]}))
    no_op = client.call("automation.pattern.set", {**params, "expectedRevision": applied["revision"]})
    assert not no_op["changed"]
    client.call("history.undo", {"expectedRevision": applied["revision"], "domain": "document"})
    assert client.call("automation.pattern.get", {"pattern": 0})["data"]["lanes"] == []
    revision = client.call("document.get")["revision"]
    client.call("history.redo", {"expectedRevision": revision, "domain": "document"})
    current = client.call("automation.pattern.get", {"pattern": 0})
    assert current["data"]["lanes"][0]["id"] == lane["id"]
    # Shared envelope operators: exact previews, independent clip geometry, retry/no-op/history.
    original_points = lane["points"]
    def transform(operation, **extra):
        return client.call("automation.pattern.transform", {"expectedRevision": client.call("document.get")["revision"],
            "lane": lane["id"], "operation": operation, **extra})
    copied = client.call("automation.pattern.copy", {"lane": lane["id"], "start": 0, "end": 16384}, "copy-current-envelope")
    assert copied["data"]["span"] == 16384 and copied["data"]["points"] == original_points
    request = {"expectedRevision": copied["revision"], "lane": lane["id"], "operation": "flip-time"}
    preview = client.call("automation.pattern.transform", {**request, "dryRun": True})
    assert preview["data"]["after"][0]["position"] == 255 and preview["data"]["after"][0]["curve"] == "exponential-reverse"
    flipped = client.call("automation.pattern.transform", request, request_id="automation-flip-retry")
    assert flipped == client.call("automation.pattern.transform", request, request_id="automation-flip-retry")
    assert flipped["data"]["after"] == preview["data"]["after"]
    expect_error(-32001, lambda: client.call("automation.pattern.transform", request))
    client.call("history.undo", {"expectedRevision": flipped["revision"], "domain": "document"})
    assert client.call("automation.pattern.get", {"pattern":0})["data"]["lanes"][0]["points"] == original_points
    noop = transform("shift", options={"amount":0})
    assert not noop["changed"] and client.call("document.get")["data"]["canRedo"]
    for extra in [{"options":{"amount":True}}, {"options":{"amount":.5}}, {"options":{"amount":0,"unknown":1}}, {"end":0,"options":{"amount":0}}]:
        expect_error(-32602, lambda: transform("shift", **extra))
    expect_error(-32602, lambda: transform("humanize", options={"amount":.1}))
    h1=transform("humanize",options={"amount":.1,"seed":42},dryRun=True)
    h2=transform("humanize",options={"amount":.1,"seed":42},dryRun=True)
    assert h1 == h2 and not h1["changed"]
    ramp=transform("ramp",start=0,end=513,options={"from":.2,"to":.8})
    assert client.call("automation.pattern.copy", {"lane":lane["id"],"start":0,"end":16384}, "copy-current-envelope")["data"]["points"] != copied["data"]["points"], "Read retries must observe the new lane, not a cached old clip"
    short_clip=client.call("automation.pattern.copy",{"lane":lane["id"],"start":0,"end":513})["data"]
    assert short_clip["points"] == [{"position":0,"value":.2,"curve":"linear"},{"position":512,"value":.8,"curve":"linear"}]
    repeated=transform("paste",start=1024,options={"clip":short_clip,"repeats":2})
    assert [p["position"] for p in repeated["data"]["after"]] == [0,512,1024,1536,1537,2049,16128]
    expect_error(-32602,lambda:transform("insert",start=0,options={"clip":short_clip}))
    expect_error(-32602,lambda:transform("paste",end=100,options={"clip":short_clip}))
    for _ in range(2):
        client.call("history.undo",{"expectedRevision":client.call("document.get")["revision"],"domain":"document"})
    current = client.call("automation.pattern.get", {"pattern": 0})
    assert current["data"]["lanes"][0]["points"] == original_points
    removed = client.call("automation.pattern.remove", {"expectedRevision": current["revision"], "lane": lane["id"]})
    client.call("plugin.remove", {"expectedRevision": removed["revision"], "slot": 0})
    print("PASS musical automation socket: envelope copy/flip/ramp/repeat paste, deterministic humanization, exact preview/retry/stale/no-op undo, strict points, stable identity and conflicting-clock guard")


def mixer_tools(client):
    def write(method, params):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **params})
    initial = client.call("mixer.get")
    assert not initial["data"]["active"]
    preview = write("mixer.enable", {"dryRun": True})
    assert not preview["changed"] and preview["data"]["wouldChange"]
    enabled = write("mixer.enable", {})
    buses = client.call("mixer.get")["data"]["buses"]
    assert len(buses) == 9
    first, master = buses[0]["id"], buses[-1]["id"]
    expect_error(-32001, lambda: client.call("mixer.bus.set", {"expectedRevision": initial["revision"], "bus": first, "gainDB": -3}))
    group = write("mixer.bus.add", {"kind": "group", "name": "Rhythm"})["data"]["bus"]
    space = write("mixer.bus.add", {"kind": "return", "name": "Space"})["data"]["bus"]
    write("mixer.bus.set", {"bus": first, "output": group})
    write("mixer.sends.set", {"bus": group, "sends": [{"target": space, "gainDB": -9, "preFader": True}]})
    expect_error(-32602, lambda: write("mixer.bus.set", {"bus": space, "output": group}))
    for params in [{"gainDB": True}, {"width": 3}, {"pan": -2}, {"prePan": -1.01}, {"prePan": True}, {"prePan": None}, {"typo": 2}, {"inserts": ["missing"]}]:
        expect_error(-32602, lambda: write("mixer.bus.set", {"bus": first, **params}))
    before = client.call("mixer.get")
    auditioned = write("mixer.bus.set", {"bus": first, "gainDB": -12, "prePan": -.75, "preview": True})
    assert not auditioned["changed"] and client.call("mixer.get") == before
    params = {"expectedRevision": before["revision"], "bus": first, "gainDB": -12, "prePan": -.75, "pan": .5, "solo": True}
    dry = client.call("mixer.bus.set", {**params, "dryRun": True})
    assert not dry["changed"] and dry["data"]["mixer"]["buses"][0]["prePan"] == -.75 and client.call("mixer.get") == before
    committed = client.call("mixer.bus.set", params, "input-balance-gesture")
    assert committed["changed"]
    assert client.call("mixer.bus.set", params, "input-balance-gesture") == committed
    assert client.call("mixer.get")["data"]["buses"][0]["prePan"] == -.75
    expect_error(-32001, lambda: client.call("mixer.bus.set", params))
    assert not write("mixer.bus.set", {"bus": first, "prePan": -.75})["changed"]
    write("history.undo", {"domain": "document"})
    assert client.call("mixer.get")["data"] == before["data"]
    assert client.call("mixer.meters")["data"]["meters"] == []
    write("mixer.bus.remove", {"bus": group})
    assert client.call("mixer.get")["data"]["buses"][0]["output"] == master
    write("mixer.enable", {"enabled": False})
    assert not client.call("mixer.get")["data"]["active"]
    print("PASS mixer socket: routing, cycle rejection, strict values, input/output balance, previews, stale/retry/no-op gestures, undo and meters")


def automation_target(client):
    def write(method, params):
        return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})
    def read():
        return client.call("automation.target.get", {}, "repeat-target-read")
    before = client.call("document.get")
    original = read()
    assert not original["changed"] and original["revision"] == before["revision"]
    assert client.call("document.get") == before
    expect_error(-32602, lambda: client.call("automation.target.get", {"slot": 0}))
    descriptor = next(p for p in client.call("plugin.discover", {"format": "Built-in"})["data"] if p["classID"] == "resonance.gainer.v1")
    a = write("plugin.add", {"descriptor": descriptor})["data"]["slot"]
    b = write("plugin.add", {"descriptor": descriptor})["data"]["slot"]
    assert read()["data"] == original["data"]
    write("plugin.parameters.set", {"slot": a, "values": [{"id": 1, "value": -12}, {"id": 3, "value": 1}]})
    first = read()["data"]
    assert first["token"] != original["data"]["token"]
    target = first["target"]
    assert target["source"] == "api" and target["parameter"] == 3 and target["slot"] == a
    assert target["available"] and target["value"] == 1 and target["normalizedValue"] == 1
    before = client.call("document.get")
    assert read()["data"] == first and client.call("document.get") == before
    expect_error(-32602, lambda: write("plugin.parameters.set", {"slot": a, "values": [{"id": 1, "value": -6}, {"id": 999999, "value": 1}]}))
    assert read()["data"] == first
    write("plugin.move", {"slot": a, "direction": 1})
    moved = read()["data"]
    assert moved["token"] == first["token"] and moved["target"]["plugin"] == target["plugin"] and moved["target"]["slot"] == b
    write("plugin.remove", {"slot": b})
    missing = read()["data"]
    assert missing["token"] == first["token"] and not missing["target"]["available"] and missing["target"]["slot"] is None
    write("history.undo", {"domain": "plugins"})
    assert read()["data"] == moved
    state = client.call("plugin.state.get", {"slot": b})["data"]["data"]
    write("plugin.state.set", {"slot": b, "data": state})
    assert read()["data"] == moved
    write("automation.pattern.set", {"pattern": 0, "plugin": target["plugin"], "parameter": target["parameter"],
          "points": [{"position": 0, "value": target["normalizedValue"]}]})
    assert read()["data"] == moved
    write("history.undo", {"domain": "document"})
    assert read()["data"] == moved
    write("plugin.remove", {"slot": b}); write("plugin.remove", {"slot": a})
    print("PASS automation target socket: stable identity, live reads, batch order, no implicit lane, reordering/removal/history, rejected edits and strict fields")


def builtin_effects(client):
    inventory = client.call("plugin.discover", {"format": "Built-in"})["data"]
    assert {"resonance.gainer.v1", "resonance.dc-offset.v1", "resonance.stereo-expander.v1",
            "resonance.digital-filter.v1", "resonance.eq5.v1", "resonance.eq10.v1", "resonance.mixer-eq.v1", "resonance.comb-filter.v1", "resonance.distortion.v1", "resonance.lofimat.v1", "resonance.cabinet-simulator.v1", "resonance.compressor.v1", "resonance.gate.v1", "resonance.maximizer.v1", "resonance.bus-compressor.v1"} <= {d["classID"] for d in inventory}
    assert all(d["format"] == "Built-in" for d in inventory)
    expect_error(-32602, lambda: client.call("plugin.discover", {"format": "Unknown"}))
    expect_error(-32602, lambda: client.call("plugin.discover", {"format": "Built-in", "rescan": 1}))
    def write(method, params):
        return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})
    slot = write("plugin.add", {"descriptor": inventory[0]})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert parameters[1]["unitLabel"] == "dB" and parameters[3]["choices"] == ["Normal", "Inverted"]
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": -9}, {"id": 3, "value": 1}]})
    saved = client.call("plugin.state.get", {"slot": slot})["data"]
    assert saved["data"] and saved["descriptor"]["classID"] == "resonance.gainer.v1"
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 0
    write("history.redo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == -9
    expect_error(-32602, lambda: write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 25}]}))
    for descriptor in [{**inventory[0], "classID": "missing"}, {**inventory[0], "isInstrument": True}, {**inventory[0], "path": "/tmp/fake"}]:
        expect_error(-32602, lambda: write("plugin.add", {"descriptor": descriptor}))
    write("plugin.remove", {"slot": slot})

    for identifier in ["resonance.digital-filter.v1", "resonance.eq5.v1", "resonance.eq10.v1", "resonance.mixer-eq.v1"]:
        effect = next(d for d in inventory if d["classID"] == identifier)
        slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
        digital = "digital-filter" in identifier
        parameter_id = 2 if digital else 10
        parameters = {p["id"]: p for p in client.call("plugin.parameters.get", {"slot": slot})["data"]}
        assert parameters[parameter_id]["unitLabel"] == "Hz" and parameters[parameter_id]["displayScale"] == "logarithmic"
        write("plugin.parameters.set", {"slot": slot, "values": [{"id": parameter_id, "value": 1234.5}]})
        assert next(p for p in client.call("plugin.parameters.get", {"slot": slot})["data"] if p["id"] == parameter_id)["value"] == 1234.5
        write("history.undo", {"domain": "plugins"})
        assert next(p for p in client.call("plugin.parameters.get", {"slot": slot})["data"] if p["id"] == parameter_id)["value"] == parameters[parameter_id]["value"]
        write("plugin.remove", {"slot": slot})
    effect = next(d for d in inventory if d["classID"] == "resonance.comb-filter.v1")
    slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert parameters[1]["step"] == 1 and parameters[1]["unitLabel"] == "MIDI"
    assert parameters[5]["displayScale"] == "logarithmic" and parameters[5]["unitLabel"] == "ms"
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 60.7}, {"id": 3, "value": -75}]})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 61
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 69
    write("plugin.remove", {"slot": slot})
    effect = next(d for d in inventory if d["classID"] == "resonance.distortion.v1")
    slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert len(parameters) == 7 and parameters[2]["choices"] == ["Soft clip", "Hard clip", "Fold", "Wrap"]
    assert parameters[1]["unitLabel"] == "dB" and parameters[4]["unitLabel"] == "%"
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 18.5}, {"id": 2, "value": 2}, {"id": 4, "value": 30}]})
    changed = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert changed[1]["value"] == 18.5 and changed[2]["value"] == 2 and changed[4]["value"] == 30
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 6
    write("plugin.remove", {"slot": slot})
    effect = next(d for d in inventory if d["classID"] == "resonance.lofimat.v1")
    slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert len(parameters) == 9 and parameters[1]["unitLabel"] == "bits" and parameters[2]["displayScale"] == "logarithmic"
    assert parameters[8]["step"] == 1 and parameters[8]["unitLabel"] == ""
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 7.25}, {"id": 2, "value": 11025}, {"id": 8, "value": 1234566.7}]})
    changed = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert changed[1]["value"] == 7.25 and changed[2]["value"] == 11025 and changed[8]["value"] == 1234567
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 16
    expect_error(-32602, lambda: write("plugin.parameters.set", {"slot": slot, "values": [{"id": 8, "value": 16777216}]}))
    write("plugin.remove", {"slot": slot})
    effect = next(d for d in inventory if d["classID"] == "resonance.cabinet-simulator.v1")
    slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert len(parameters) == 30 and len(parameters[1]["choices"]) == 18 and len(parameters[2]["choices"]) == 6
    assert parameters[10]["unitLabel"] == "Hz" and parameters[10]["displayScale"] == "logarithmic"
    assert parameters[29]["choices"] == ["Bell", "Low shelf", "High shelf", "Low-pass", "High-pass", "Notch"]
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 6}, {"id": 2, "value": 4}, {"id": 8, "value": 0}, {"id": 29, "value": 5}]})
    changed = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert changed[1]["value"] == 6 and changed[2]["value"] == 4 and changed[8]["value"] == 0 and changed[29]["value"] == 5
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 2
    expect_error(-32602, lambda: write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 18}]}))
    write("plugin.remove", {"slot": slot})
    for identifier in ["resonance.compressor.v1", "resonance.gate.v1", "resonance.bus-compressor.v1"]:
        effect = next(d for d in inventory if d["classID"] == identifier)
        slot = write("plugin.add", {"descriptor": effect})["data"]["slot"]
        parameters = {p["id"]: p for p in client.call("plugin.parameters.get", {"slot": slot})["data"]}
        assert parameters[7]["choices"] == (["Adaptive", "Feedback", "Feedforward"] if "bus-compressor" in identifier else ["Peak", "RMS"]) and parameters[9]["choices"] == ["Internal", "External sidechain"]
        if "gate" in identifier:
            assert parameters[16]["displayScale"] == "linear" and parameters[19]["choices"] == ["Gate", "Duck"]
        buses = client.call("plugin.buses.get", {"slot": slot})["data"]["buses"]
        assert len(buses) == 3 and buses[2]["index"] == 1 and not buses[2]["active"]
        write("plugin.buses.set", {"slot": slot, "inputs": [1]})
        assert client.call("plugin.buses.get", {"slot": slot})["data"]["buses"][2]["active"]
        before = client.call("document.get")["revision"]
        meter = client.call("plugin.meters", {"slot": slot})
        assert meter["revision"] == before and meter["data"]["supported"] and not meter["data"]["active"]
        assert meter["data"]["reductionDB"] == [0, 0] and meter["data"]["detectorDB"] == [-160, -160]
        expect_error(-32602, lambda: client.call("plugin.meters", {"slot": True}))
        expect_error(-32602, lambda: client.call("plugin.meters", {"slot": slot, "reset": True}))
        changes = [{"id": 1, "value": -24}, {"id": 9, "value": 1}, {"id": 12, "value": 1}]
        if "bus-compressor" in identifier:
            changes.append({"id": 7, "value": 2})
        write("plugin.parameters.set", {"slot": slot, "values": changes})
        assert next(p for p in client.call("plugin.parameters.get", {"slot": slot})["data"] if p["id"] == 9)["value"] == 1
        write("history.undo", {"domain": "plugins"})
        assert next(p for p in client.call("plugin.parameters.get", {"slot": slot})["data"] if p["id"] == 9)["value"] == 0
        write("plugin.remove", {"slot": slot})
    limiter = next(d for d in inventory if d["classID"] == "resonance.maximizer.v1")
    slot = write("plugin.add", {"descriptor": limiter})["data"]["slot"]
    parameters = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert len(parameters) == 6 and parameters[3]["unitLabel"] == "ms" and parameters[3]["displayScale"] == "logarithmic"
    assert client.call("plugin.meters", {"slot": slot})["data"]["supported"]
    assert len(client.call("plugin.buses.get", {"slot": slot})["data"]["buses"]) == 2
    write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": 24}, {"id": 2, "value": -12}, {"id": 5, "value": -1}]})
    changed = client.call("plugin.parameters.get", {"slot": slot})["data"]
    assert changed[1]["value"] == 24 and changed[2]["value"] == -12 and changed[5]["value"] == -1
    write("history.undo", {"domain": "plugins"})
    assert client.call("plugin.parameters.get", {"slot": slot})["data"][1]["value"] == 0
    expect_error(-32602, lambda: write("plugin.parameters.set", {"slot": slot, "values": [{"id": 5, "value": 1}]}))
    write("plugin.remove", {"slot": slot})
    print("PASS built-in socket: utilities, Digital Filter, EQ5/EQ10/Mixer EQ, Comb Filter, Distortion, LofiMat, Cabinet Simulator, Compressor/Bus Compressor/Gate/Maximizer/sidechains/meters, units/scales, precise edits and undo")


def plugin_buses(client):
    def write(method, params):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **params})
    plugin = {"format": "VST3", "type": 0, "subtype": 0, "manufacturer": 0,
              "name": "Resonance Test Instrument", "path": str(BUILD / "test-plugins/ResonanceFixture.vst3"),
              "classID": "5245534F4E414E43494E535452550001", "isInstrument": True}
    write("plugin.add", {"descriptor": plugin})
    info = client.call("plugin.buses.get", {"slot": 0})
    assert len(info["data"]["buses"]) == 32
    assert info["data"]["buses"][1]["channels"] == 1 and not info["data"]["buses"][31]["active"]
    for bad in [[0], [32], [True], [1, 1]]:
        expect_error(-32602, lambda: write("plugin.buses.set", {"slot": 0, "outputs": bad}))
    preview = write("plugin.buses.set", {"slot": 0, "outputs": [1, 31], "dryRun": True})
    assert preview["data"]["wouldChange"] and not preview["changed"]
    write("plugin.buses.set", {"slot": 0, "outputs": [1, 31]})
    assert not write("plugin.buses.set", {"slot": 0, "outputs": [31, 1]})["changed"]
    expect_error(-32001, lambda: client.call("plugin.buses.set", {"slot": 0, "outputs": [], "expectedRevision": info["revision"]}))
    write("history.undo", {"domain": "plugins"})
    assert not client.call("plugin.buses.get", {"slot": 0})["data"]["buses"][31]["active"]
    write("history.redo", {"domain": "plugins"})
    write("mixer.enable", {})
    bus = client.call("mixer.get")["data"]["buses"][1]["id"]
    write("mixer.instrument.route", {"plugin": info["data"]["plugin"], "target": bus, "output": 31})
    expect_error(-32602, lambda: write("mixer.instrument.route", {"plugin": info["data"]["plugin"], "target": bus, "output": 2}))
    write("mixer.instrument.route", {"plugin": info["data"]["plugin"], "target": None, "output": 31})
    assert not client.call("mixer.get")["data"]["instruments"]
    effect = {**plugin, "name": "Resonance Test Gain", "classID": "5245534F4E414E434546464543540001", "isInstrument": False}
    write("plugin.add", {"descriptor": effect})
    graph = client.call("mixer.get")["data"]
    effect_id, target = graph["plugins"][1]["id"], graph["buses"][0]["id"]
    write("mixer.bus.set", {"bus": target, "inserts": [effect_id]})
    params = {"plugin": effect_id, "input": 1, "sources": [{"source": bus, "gainDB": -6, "preFader": True}]}
    expect_error(-32602, lambda: write("mixer.sidechains.set", params))
    write("plugin.buses.set", {"slot": 1, "inputs": [1]})
    expect_error(-32602, lambda: write("mixer.sidechains.set", {**params, "sources": [{"source": target}]}))
    assert not write("mixer.sidechains.set", {**params, "dryRun": True})["changed"]
    write("mixer.sidechains.set", params)
    assert client.call("mixer.get")["data"]["sidechains"][0]["source"] == bus
    write("history.undo", {"domain": "document"})
    assert not client.call("mixer.get")["data"]["sidechains"]
    write("history.redo", {"domain": "document"})
    write("plugin.remove", {"slot": 1})
    write("mixer.sidechains.set", {"plugin": effect_id, "input": 1, "sources": []})
    assert not client.call("mixer.get")["data"]["sidechains"]
    write("mixer.enable", {"enabled": False})
    write("plugin.remove", {"slot": 0})
    print("PASS multi-bus socket: native indices, layout discovery, activation, dry run, stale edits, Undo/Redo output routing and sidechain sources")


def precise_notes_and_recording(client):
    def write(method, **params): return client.call(method,{"expectedRevision":client.call("document.get")["revision"],**params})
    def notes(): return client.call("pattern.notes.get",{"pattern":0})
    initial=notes()
    event={"channel":0,"position":1234,"note":61,"instrument":2,"velocity":93}
    params={"pattern":0,"events":[event,{"channel":0,"position":5000,"note":255}]}
    assert not write("pattern.notes.set",**params,dryRun=True)["changed"] and notes()==initial
    write("pattern.notes.set",**params)
    saved=notes();assert saved["data"]["events"][0]==event
    assert not write("pattern.notes.set",**params)["changed"]
    for bad in [{**event,"note":121},{**event,"velocity":0},{**event,"position":-1},{**event,"channel":127}]:
        expect_error(-32602,lambda:write("pattern.notes.set",pattern=0,events=[bad]))
    expect_error(-32602,lambda:write("pattern.notes.set",pattern=0,events=[event,event]))
    write("history.undo",domain="document");assert notes()["data"]==initial["data"]
    write("history.redo",domain="document");assert notes()["data"]==saved["data"]
    write("history.undo",domain="document")
    info=notes()["data"];rows_per_beat=info["rowsPerBeat"]
    assert any(fx["command"]==9 for fx in info["effects"])
    beat_event={"channel":0,"row":1,"offsetBeats":0.5/rows_per_beat,"note":61,"instrument":2,"velocity":60,"effect":9,"parameter":255}
    assert not write("pattern.notes.set",pattern=0,events=[beat_event],dryRun=True)["changed"]
    changed=write("pattern.notes.set",pattern=0,events=[beat_event]);read=notes()["data"]["events"][0]
    assert read=={"channel":0,"position":98304,"note":61,"instrument":2,"velocity":60,"effect":9,"parameter":255}
    assert not write("pattern.notes.set",pattern=0,events=[{**read}])["changed"]
    for bad in [{**beat_event,"offsetBeats":1/rows_per_beat},{**beat_event,"offsetRows":0.5},{**beat_event,"position":0},{**beat_event,"effect":17}]:
        expect_error(-32602,lambda:write("pattern.notes.set",pattern=0,events=[bad]))
    write("history.undo",domain="document");assert notes()["data"]==initial["data"]
    take=write("recording.start",channels=[0,1],instrument=2,quantization=0,latencyMS=1.5)["data"]
    assert take["capturing"] and take["events"]==[] and take["eventCount"]==0
    expect_error(-32602,lambda:write("recording.start",channels=[0],instrument=2))
    expect_error(-32602,lambda:write("recording.stop",take="different-take"))
    expect_error(-32602,lambda:write("recording.capture",take=take["take"],events=[{"timestamp":"18446744073709551616","status":144,"note":60,"velocity":100}]))
    captured=write("recording.capture",take=take["take"],events=[{"timestamp":take["hostTime"],"status":144,"note":60,"velocity":100}])["data"]
    assert captured["missingTime"]==1 and captured["events"]==[] and captured["eventCount"]==0  # No physical playback is opened by this test.
    write("recording.stop",take=take["take"])
    assert not client.call("recording.get")["data"]["capturing"]
    assert not write("recording.commit",take=take["take"],dryRun=True)["changed"]
    assert client.call("recording.get")["data"]["take"]==take["take"]
    write("recording.commit",take=take["take"])
    assert client.call("recording.get")["data"]["take"]==""
    take=write("recording.start",channels=[0],instrument=2)["data"]["take"]
    write("pattern.notes.set",**params);write("recording.stop",take=take)
    expect_error(-32602,lambda:write("recording.commit",take=take))
    assert client.call("recording.get")["data"]["take"]==take
    write("recording.discard",take=take);write("history.undo",domain="document")
    pitch={"pattern":0,"columns":[{"channel":0,"count":1}],"commands":[{"channel":0,"column":0,"position":1234,"duration":70000,"kind":"pitch-slide","value":-3.125,"pitchRange":12}]}
    write("pattern.performance.set",**pitch)
    command=client.call("pattern.performance.get",{"pattern":0})["data"]["commands"][0]
    assert command["value"]==-3.125 and command["pitchRange"]==12 and command["binding"]==0
    write("history.undo",domain="document")
    print("PASS precise-note and recording API: guarded edits, same-row events, preview/no-op/history, strict fields, signed pitch, take identity, timestamp validation, missing clocks and retained stale takes")


def pattern_performance(client):
    def write(method, params):
        return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})
    descriptor = {"type": 0, "subtype": 0, "manufacturer": 0, "name": "Resonance Test Programs", "format": "VST3",
                  "path": str(BUILD / "test-plugins/ResonanceFixture.vst3"), "classID": "5245534F4E414E4350524F4752410001", "isInstrument": False}
    slot = write("plugin.add", {"descriptor": descriptor})["data"]["slot"]
    plugin = client.call("document.get")["data"]["nativePlugins"][slot]["instanceID"]
    initial = client.call("pattern.performance.get", {"pattern": 0})["data"]
    params = {"pattern": 0, "columns": [{"channel": 0, "count": 2}],
              "bindings": [{"id": 255, "plugin": plugin, "parameter": 7, "name": "Agent slide"}],
              "commands": [{"channel": 0, "column": 1, "position": 12345, "duration": 98765,
                            "kind": "parameter-slide", "binding": 255, "value": 0.8123456789}]}
    try:
        assert not write("pattern.performance.set", {**params, "dryRun": True})["changed"]
        assert client.call("pattern.performance.get", {"pattern": 0})["data"] == initial
        applied = write("pattern.performance.set", params)
        data = client.call("pattern.performance.get", {"pattern": 0})["data"]
        assert applied["changed"] and data["unitsPerRow"] == 65536 and data["commands"][0]["position"] == 12345
        assert data["bindings"][-1]["plugin"] == plugin and data["bindings"][-1]["resolved"]
        assert not write("pattern.performance.set", params)["changed"]
        expect_error(-32001, lambda: client.call("pattern.performance.set", {**params, "expectedRevision": "stale"}))
        expect_error(-32602, lambda: write("pattern.performance.set", {"pattern": 0, "removeBindings": [255]}))
        expect_error(-32602, lambda: write("pattern.performance.set", {"pattern": 0, "columns": [{"channel": 0, "count": 1}]}))
        expect_error(-32602, lambda: write("automation.replaceLane", {"slot": slot, "id": 7, "points": [{"frame": 0, "value": 0.5}]}))
        expect_error(-32602, lambda: write("pattern.performance.set", {**params, "commands": [{**params["commands"][0], "duration": 0}]}))
        assert client.call("pattern.performance.get", {"pattern": 0})["data"] == data
        write("history.undo", {"domain": "document"})
        assert client.call("pattern.performance.get", {"pattern": 0})["data"] == initial
        write("history.redo", {"domain": "document"})
        assert client.call("pattern.performance.get", {"pattern": 0})["data"] == data
        write("history.undo", {"domain": "document"})
    finally:
        write("plugin.remove", {"slot": slot})


def plugin_programs(client):
    def write(method, params):
        return client.call(method, {**params, "expectedRevision": client.call("document.get")["revision"]})
    descriptor = {"type": 0, "subtype": 0, "manufacturer": 0, "name": "Resonance Test Programs", "format": "VST3",
                  "path": str(BUILD / "test-plugins/ResonanceFixture.vst3"), "classID": "5245534F4E414E4350524F4752410001", "isInstrument": False}
    added = write("plugin.add", {"descriptor": descriptor})
    slot = added["data"]["slot"]
    identity = client.call("document.get")["data"]["nativePlugins"][slot]["instanceID"]
    try:
        catalog = client.call("plugin.programs.get", {"plugin": identity})["data"]
        assert len(catalog["programs"]) == 6 and catalog["programs"][5]["id"] == "vst3:7:18:2"
        state = lambda: client.call("plugin.state.get", {"slot": slot})["data"]
        before = state()
        params = {"plugin": identity, "program": "vst3:0:17:2", "expectedCatalogRevision": catalog["catalogRevision"],
                  "expectedRevision": client.call("document.get")["revision"]}
        assert not client.call("plugin.programs.load", {**params, "dryRun": True})["changed"] and state() == before
        expect_error(-32001, lambda: client.call("plugin.programs.load", {**params, "expectedCatalogRevision": "stale"}))
        expect_error(-32602, lambda: client.call("plugin.programs.load", {**params, "program": "unknown"}))
        expect_error(-32602, lambda: client.call("plugin.programs.load", {**params, "typo": 1}))
        result = client.call("plugin.programs.load", params, "program-load-retry")
        assert result["changed"] and result["data"]["loaded"] and state() != before
        after = state()
        assert client.call("plugin.programs.load", params, "program-load-retry") == result and state() == after
        expect_error(-32001, lambda: client.call("plugin.programs.load", params))
        write("history.undo", {"domain": "plugins"}); assert state() == before
        write("history.redo", {"domain": "plugins"}); assert state() == after
    finally:
        write("plugin.remove", {"slot": slot})
    expect_error(-32602, lambda: client.call("plugin.programs.get", {"plugin": identity}))


def plugin_presets(client):
    def write(method, params, request_id=None):
        return client.call(method, {"expectedRevision": client.call("document.get")["revision"], **params}, request_id)
    inventory = client.call("plugin.discover", {"format": "Built-in"})["data"]
    gain = next(d for d in inventory if d["classID"] == "resonance.gainer.v1")
    slot = write("plugin.add", {"descriptor": gain})["data"]["slot"]
    identity = client.call("document.get")["data"]["nativePlugins"][slot]["instanceID"]
    def set_gain(value):
        write("plugin.parameters.set", {"slot": slot, "values": [{"id": 1, "value": value}]})
    def value():
        return next(p["value"] for p in client.call("plugin.parameters.get", {"slot": slot})["data"] if p["id"] == 1)
    with tempfile.TemporaryDirectory(prefix="resonance-presets-api-") as folder:
        path = Path(folder) / "Warm gain.resonance-preset"
        set_gain(-9)
        before = client.call("document.get")
        save = {"expectedRevision": before["revision"], "plugin": identity, "path": str(path), "name": "Warm gain"}
        assert not client.call("plugin.preset.save", {**save, "dryRun": True})["data"]["written"] and not path.exists()
        saved = client.call("plugin.preset.save", save, "preset-save-retry")
        assert saved["data"]["written"] and not saved["changed"] and not saved["playbackStopped"]
        original = path.read_bytes()
        assert client.call("plugin.preset.save", save, "preset-save-retry") == saved and path.read_bytes() == original
        assert client.call("document.get") == before
        expect_error(-32602, lambda: client.call("plugin.preset.save", save))
        info = client.call("plugin.preset.inspect", {"path": str(path)}, "preset-inspect-repeat")["data"]
        assert info["name"] == "Warm gain" and info["stateBytes"] > 0 and "state" not in info
        set_gain(-21)
        baseline = client.call("document.get")
        load = {"expectedRevision": baseline["revision"], "plugin": identity, "path": str(path), "expectedPresetRevision": info["presetRevision"]}
        assert not client.call("plugin.preset.load", {**load, "dryRun": True})["changed"] and value() == -21
        loaded = client.call("plugin.preset.load", load, "preset-load-retry")
        assert loaded["data"]["loaded"] and value() == -9
        assert client.call("plugin.preset.load", load, "preset-load-retry") == loaded
        expect_error(-32001, lambda: client.call("plugin.preset.load", load))
        write("history.undo", {"domain": "plugins"}); assert value() == -21
        write("history.redo", {"domain": "plugins"}); assert value() == -9
        root = plistlib.loads(original); root["name"] = "Changed externally"
        path.write_bytes(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))
        fresh = client.call("plugin.preset.inspect", {"path": str(path)}, "preset-inspect-repeat")["data"]
        assert fresh["presetRevision"] != info["presetRevision"] and fresh["name"] == "Changed externally"
        expect_error(-32001, lambda: write("plugin.preset.load", {**load, "expectedRevision": client.call("document.get")["revision"]}))
        # Dry-run understands the host container but does not invoke the vendor
        # state decoder. A real rejected decode must retain the current plugin.
        root["state"] = b"invalid effect state"
        path.write_bytes(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))
        broken = client.call("plugin.preset.inspect", {"path": str(path)})["data"]
        attempt = {"plugin": identity, "path": str(path), "expectedPresetRevision": broken["presetRevision"]}
        assert not write("plugin.preset.load", {**attempt, "dryRun": True})["data"]["loaded"]
        before = client.call("document.get"); state = client.call("plugin.state.get", {"slot": slot})["data"]
        expect_error(-32003, lambda: write("plugin.preset.load", attempt))
        assert client.call("document.get") == before and client.call("plugin.state.get", {"slot": slot})["data"] == state
        for extra in [{"plugin": "missing"}, {"dryRun": 1}, {"typo": 1}]:
            expect_error(-32602, lambda: write("plugin.preset.load", {**attempt, **extra}))
        expect_error(-32602, lambda: client.call("plugin.preset.inspect", {"path": str(path), "typo": 1}))
        root["plugin"] = next(d for d in inventory if d["classID"] != gain["classID"])
        path.write_bytes(plistlib.dumps(root, fmt=plistlib.FMT_BINARY))
        mismatch = client.call("plugin.preset.inspect", {"path": str(path)})["data"]
        expect_error(-32602, lambda: write("plugin.preset.load", {**attempt, "expectedPresetRevision": mismatch["presetRevision"]}))
        write("plugin.preset.save", {"plugin": identity, "path": str(path), "name": "Replaced", "overwrite": True})
        assert client.call("plugin.preset.inspect", {"path": str(path)})["data"]["name"] == "Replaced"
    write("plugin.remove", {"slot": slot})
    print("PASS preset socket: native files, dry runs, baseline state, atomic overwrite, independent file/song revisions, retry/read freshness, type/decoder rejection and Undo/Redo")


def main():
    with tempfile.TemporaryDirectory(prefix="resonance-api-tests-") as tmp:
        directory = Path(tmp)
        with open(directory / "host.log", "w+") as log:
            app_test = "--app" in sys.argv
            command = [str(BUILD / "ScreamSeq.app/Contents/MacOS/ScreamSeq"), "--automation-test"] if app_test else [str(BUILD / "automation-test-host"), str(directory)]
            process = subprocess.Popen(command, stdout=log, stderr=log, env={**os.environ, "RESONANCE_AUTOMATION_TEST_DIRECTORY": str(directory)})
            try:
                deadline = time.monotonic() + 20
                found = []
                while time.monotonic() < deadline:
                    found = endpoints(directory)
                    if found:
                        break
                    if process.poll() is not None:
                        log.seek(0)
                        raise AssertionError(log.read())
                    time.sleep(.05)
                assert len(found) == 1, "Private socket was not published"
                endpoint = found[0]
                assert endpoint["pid"] == process.pid
                assert Path(endpoint["socket"]).stat().st_mode & 0o777 == 0o600
                assert Path(endpoint["socket"]).parent.stat().st_mode & 0o777 == 0o700
                client = Client(endpoint["socket"])
                navigation_tools(client)
                note_track_tools(client)
                plugin_library_tools(client, directory)
                context = client.call("context.get")["data"]
                if app_test:
                    assert context["sampleSelection"] == {"sample": 1, "start": 0, "end": 256, "channels": "both"}
                start, channel = context["row"], context["channel"]
                initial = client.call("pattern.get", {"pattern": 0})
                preview = drum_roll(client)
                assert preview["revision"] == initial["revision"] and not preview["changed"]
                assert client.call("pattern.get", {"pattern": 0}) == initial
                applied = drum_roll(client, apply=True)
                changes = applied["data"]["changes"]
                rows = [change["row"] for change in changes]
                assert rows == list(range(start, initial["data"]["rows"], 2)), rows
                assert changes[0]["after"]["volume"] == 4 and changes[-1]["after"]["volume"] == 64
                volumes = [change["after"]["volume"] for change in changes]
                assert volumes == sorted(volumes)
                assert all(c["channel"] == channel and c["after"]["instrument"] == 2 for c in changes)
                current = client.call("pattern.get", {"pattern": 0})
                if app_test:
                    assert client.call("context.get")["data"]["dirty"] is True, "API edit must mark the native document unsaved"
                for before, after in zip(initial["data"]["cells"], current["data"]["cells"]):
                    if before["row"] not in rows or before["channel"] != channel:
                        assert before == after, "Roll changed an unrelated cell"
                    else:
                        assert before["effect"] == after["effect"] and before["parameter"] == after["parameter"]
                client.call("history.undo", {"expectedRevision": current["revision"], "domain": "document"})
                undone = client.call("pattern.get", {"pattern": 0})
                assert undone["data"] == initial["data"], "One undo must restore entire roll"
                pattern_tools(client)
                row_tools(client)
                saving_tools(client, directory, app_test)
                sample_tools(client)
                clipboard_tools(client)
                drawing_tools(client)
                snapping_tools(client)
                crossfade_tools(client)
                loop_tools(client)
                arrangement_tools(client)
                musical_automation(client)
                mixer_tools(client)
                plugin_buses(client)
                automation_target(client)
                builtin_effects(client)
                plugin_programs(client)
                precise_notes_and_recording(client)
                pattern_performance(client)
                plugin_presets(client)
                song_timing(client)
                plugin_aliases(client)
                instrument_envelopes(client)
                undone = client.call("pattern.get", {"pattern": 0})
                params = {"expectedRevision": undone["revision"], "cells": [{"pattern": 0, "row": 1, "channel": 1, "note": 61}]}
                first = client.call("pattern.apply", params, "idempotent-edit")
                assert client.call("pattern.apply", params, "idempotent-edit") == first
                assert client.call("document.get")["revision"] == first["revision"]
                expect_error(-32600, lambda: client.call("pattern.apply", {**params, "dryRun": True}, "idempotent-edit"))
                expect_error(-32001, lambda: client.call("pattern.apply", params))
                # Two clients reading the same revision cannot silently overwrite one another.
                revision = client.call("document.get")["revision"]
                def race(note):
                    try:
                        return client.call("pattern.apply", {"expectedRevision": revision, "cells": [{"pattern": 0, "row": 2, "channel": 1, "note": note}]})
                    except APIError as error:
                        return error.code
                with ThreadPoolExecutor(2) as pool:
                    results = list(pool.map(race, [62, 63]))
                assert sum(isinstance(r, dict) for r in results) == 1 and -32001 in results
                for bad in [True, -1, 1.5, "5", None, [], {}]:
                    expect_error(-32602, lambda: client.call("pattern.get", {"pattern": bad}))
                expect_error(-32602, lambda: client.call("pattern.get", {"pattern": 0, "typo": 1}))
                expect_error(-32601, lambda: client.call("shell.exec", {"command": "anything"}))
                # An incomplete or invalid JSON request must not reach the document.
                revision = client.call("document.get")["revision"]
                with socket.socket(socket.AF_UNIX) as wire:
                    wire.connect(endpoint["socket"])
                    wire.sendall(b'{"jsonrpc":"2.0","method":"pattern.apply","params":{}}\n')
                    response = json.loads(wire.makefile("rb").readline())
                    assert response["error"]["code"] == -32600
                assert client.call("document.get")["revision"] == revision
                catalog = client.call("pattern.commands")
                assert catalog["revision"] == revision and not catalog["changed"]
                assert catalog["data"]["effect"] and catalog["data"]["volume"]
                names = {c["name"] for c in catalog["data"]["effect"]}
                assert {"Arpeggio", "Note Delay", "Sound Control", "Set Tempo"} <= names
                for column in ["effect", "volume"]:
                    for command in catalog["data"][column]:
                        assert command["minimum"] <= command["suggestedParameter"] <= command["maximum"]
                        assert command["suggestedParameter"] & command["parameterMask"] == command["parameterValue"]
                        assert command["description"] and command["family"]
                expect_error(-32602, lambda: client.call("pattern.commands", {"format": "XM"}))
                assert client.call("document.get")["revision"] == revision
                # Apply a catalog entry using ordinary revision-checked pattern edits.
                effect = next(c for c in catalog["data"]["effect"] if c["name"] == "Note Delay")
                before = client.call("pattern.get", {"pattern": 0, "startRow": 0, "rowCount": 1, "startChannel": 0, "channelCount": 1})["data"]["cells"]
                command_edit = {"expectedRevision":revision, "cells":[{"pattern":0,"row":0,"channel":0,"effect":effect["command"],"parameter":effect["parameterValue"] | 3}]}
                preview = client.call("pattern.apply", {**command_edit,"dryRun":True})
                assert not preview["changed"] and preview["revision"] == revision
                applied = client.call("pattern.apply", command_edit)
                expect_error(-32001, lambda: client.call("pattern.apply", command_edit))
                after = client.call("pattern.get", {"pattern":0,"startRow":0,"rowCount":1,"startChannel":0,"channelCount":1})["data"]["cells"]
                assert all(after[0][key] == before[0][key] for key in ["note","instrument","volumeCommand","volume"])
                client.call("history.undo", {"expectedRevision":applied["revision"],"domain":"document"})
                assert client.call("pattern.get", {"pattern":0,"startRow":0,"rowCount":1,"startChannel":0,"channelCount":1})["data"]["cells"] == before
                print("PASS command catalog socket: format metadata, extended prefixes, strict read, preview, field-preserving apply, stale rejection and undo")
                schema = json.loads((ROOT / "mac/Tools/resonance-api.schema.json").read_text())
                advertised = client.call("api.describe")["data"]
                methods = {entry["properties"]["method"]["const"] for entry in schema["oneOf"]
                           if "--app" in sys.argv or not entry.get("x-application-only")}
                assert methods == set(advertised["reads"] + advertised["writes"])
                navigation_pattern(client)
                print("PASS local API socket: private discovery/permissions, JSON framing, real crescendo-roll client, dry run, one-step undo, preserved cells/effects, retry deduplication, competing writers and method schema; no windows or audio output")
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
                if found:
                    # SIGTERM cannot run AppKit's normal quit callback. Remove only
                    # this test's private socket directory, never another instance.
                    import shutil
                    shutil.rmtree(Path(found[0]["socket"]).parent, ignore_errors=True)


if __name__ == "__main__":
    main()
