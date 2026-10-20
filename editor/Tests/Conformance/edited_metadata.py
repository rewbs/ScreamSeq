"""Compare identical native edits with and without opaque F04 extensions.

The clean leg supplies the exact expected known wire representation; stable-ID
extensions are then added to that expected tree, never stripped from output.
Every stage also checks its intended edit/history result. Mac uses private
recovery admission: native Open and reciprocal exchange remain separate gates.
"""
import copy
import datetime
from pathlib import Path
import plistlib
import struct

from fixture_loader import FixtureLoader
from native_roundtrip import write_json
from project_tree import CORPUS, EvidenceError, differences, digest, read_bytes, read_project, sha256


def editable_copy(value):
    """Copy the strict reader's duplicate-key-guarded dictionaries for an oracle edit."""
    if isinstance(value, dict):
        return {key: editable_copy(child) for key, child in value.items()}
    if isinstance(value, list):
        return [editable_copy(child) for child in value]
    return copy.deepcopy(value)


def run(client, output):
    output.mkdir(exist_ok=False)
    source = CORPUS / "fixtures/04-complete-reference.screamseq"
    original_bytes, original = read_bytes(source), read_project(source)
    graph_id = original["native"]["signalGraph"]["library"][0]["id"]
    node_id = original["native"]["signalGraph"]["library"][0]["nodes"][2]["id"]
    track_id = original["native"]["tracks"][0][1]["id"]
    plugin_id = original["plugins"][0]["instanceID"]
    plugin_ids = [p["instanceID"] for p in original["plugins"]]
    title, label = "Edited opaque metadata acceptance", "Retained processor label"
    sentinel = dict(integer=9007199254740993, real=1.0, signedZero=-0.0,
                    bytes=b"\x00\xffopaque edit\x00\x80", text="Music \U0001f3b5 e\u0301",
                    date=datetime.datetime(2024, 2, 29, 12, 34, 56),
                    uid=plistlib.UID(281474976710657), boolean=True)
    key = "parityEditedOpaque"
    report = dict(format="screamseq-edited-metadata-v1", passed=False, parityComplete=False,
                  qualificationScope="Paired native edits and exact typed persistence; no reciprocal or foreground claim",
                  sourceSHA256=sha256(original_bytes), toolSHA256=sha256(read_bytes(Path(__file__))),
                  fixtureLoads=[], requests=[], stages=[])
    immutable, clean = {source: sha256(original_bytes)}, {}

    def extended(tree):
        tree = copy.deepcopy(tree)
        # Inject only into identities that survive in the clean expected tree.
        # This checks deletion without resurrecting data and Undo restoration.
        targets = [("root", tree), ("native", tree["native"])]
        targets += [(track_id, item) for _, item in tree["native"]["tracks"] if item["id"] == track_id]
        targets += [(node_id, node) for graph in tree["native"]["signalGraph"]["library"]
                    if graph["id"] == graph_id for node in graph["nodes"] if node["id"] == node_id]
        targets += [(plugin_id, item) for item in tree["plugins"] if item["instanceID"] == plugin_id]
        for identity, item in targets:
            if key in item:
                raise EvidenceError("Opaque probe collides with an existing fixture field")
            item[key] = dict(sentinel, identity=identity)
        return tree

    def call(method, params=None):
        row = dict(method=method, params=params or {})
        report["requests"].append(row)
        try:
            row["result"] = client.call(method, params)
            return row["result"]
        except Exception as error:
            row["error"] = dict(type=type(error).__name__, message=str(error),
                                code=getattr(error, "code", None), data=getattr(error, "data", None))
            raise  # Never retry uncertain writes or continue document replacement.

    def write(method, **params):
        return call(method, dict(expectedRevision=call("document.get")["revision"], **params))

    loader = FixtureLoader(client, call, report["fixtureLoads"])

    def require(condition, message):
        if not condition:
            raise EvidenceError(message)

    def save(leg, stage):
        path = output / (leg + "-" + stage + ".screamseq")
        write("document.save", path=str(path), overwrite=False)
        tree = read_project(path)
        immutable[path] = sha256(read_bytes(path))
        expected = clean[stage] if leg == "clean" else extended(clean[stage])
        comparison = differences(expected, tree)
        report["stages"].append(dict(leg=leg, stage=stage, file=path.name,
                                     sha256=immutable[path], typedSHA256=digest(tree), comparison=comparison))
        require(comparison["equal"], f"{leg}/{stage}: unexpected typed metadata change")
        return path, tree

    try:
        probe = output / "opaque-input.screamseq"
        expected_input = extended(original)
        # plistlib interns floats by Python equality: -0.0 aliases +0.0 and
        # can change every existing zero in F04 before the app even sees it.
        # Use a unique real object, then replace only its encoded payload.
        # Verify the entire generated tree against the intended input.
        sentinel["signedZero"] = -987654321.125
        encoded = plistlib.dumps(extended(original), fmt=plistlib.FMT_BINARY, sort_keys=False)
        sentinel["signedZero"] = -0.0
        marker = b"\x23" + struct.pack(">d", -987654321.125)
        require(encoded.count(marker) == 1, "Signed-zero fixture marker is not unique")
        encoded = encoded.replace(marker, b"\x23" + struct.pack(">d", -0.0))
        require(differences(expected_input, plistlib.loads(encoded))["equal"],
                "Generated probe changed more than the intended opaque extensions")
        with probe.open("xb") as stream:
            stream.write(encoded)
        immutable[probe] = sha256(read_bytes(probe))
        for leg, input_path in (("clean", source), ("opaque", probe)):
            loader.load(input_path)
            prior = {}
            for stage in ("baseline", "title", "node-label", "rack-move", "rack-remove",
                          "undo-remove", "redo-remove", "restore-remove"):
                if stage == "title":
                    write("document.patch", title=title)
                    require(call("document.get")["data"]["title"] == title, "Title edit did not apply")
                elif stage == "node-label":
                    definition = next(g for g in call("graph.get")["data"]["library"] if g["id"] == graph_id)
                    next(n for n in definition["nodes"] if n["id"] == node_id)["name"] = label
                    write("graph.update", definition=definition)
                elif stage == "rack-move":
                    write("plugin.move", plugin=plugin_id, position=len(plugin_ids) - 1)
                elif stage == "rack-remove":
                    write("plugin.remove", plugins=[plugin_id])
                elif stage in ("undo-remove", "restore-remove"):
                    write("history.undo")
                elif stage == "redo-remove":
                    write("history.redo")
                # Save clean output once to establish its exact wire encoding.
                # Explicit assertions below prevent a no-op or wrong edit from
                # becoming a permissive expected baseline.
                if leg == "clean":
                    path = output / ("clean-" + stage + ".screamseq")
                    write("document.save", path=str(path), overwrite=False)
                    tree = read_project(path)
                    clean[stage] = tree
                    immutable[path] = sha256(read_bytes(path))
                    report["stages"].append(dict(leg=leg, stage=stage, file=path.name,
                                                 sha256=immutable[path], typedSHA256=digest(tree)))
                else:
                    path, tree = save(leg, stage)
                ids = [p["instanceID"] for p in tree["plugins"]]
                if stage == "baseline":
                    require(differences(original if leg == "clean" else extended(original), tree)["equal"],
                            "Initial save changed the input fixture")
                elif stage == "title":
                    expected = editable_copy(prior["baseline"])
                    expected["module"] = tree["module"]
                    require(differences(expected, tree)["equal"], "Title edit changed unrelated project metadata")
                    require(tree["module"] != prior["baseline"]["module"], "Title edit was not persisted")
                elif stage == "node-label":
                    expected = editable_copy(prior["title"])
                    node = next(n for g in expected["native"]["signalGraph"]["library"] if g["id"] == graph_id
                                for n in g["nodes"] if n["id"] == node_id)
                    node["name"] = label
                    require(differences(expected, tree)["equal"], "Graph label edit changed unrelated project data")
                elif stage in ("rack-move", "undo-remove", "restore-remove"):
                    require(ids == plugin_ids[1:] + plugin_ids[:1], "Rack move or Undo changed stable order")
                    if stage == "rack-move":
                        expected = editable_copy(prior["node-label"])
                        expected["plugins"] = expected["plugins"][1:] + expected["plugins"][:1]
                        for point in expected["automation"]:
                            point[0] = len(plugin_ids) - 1 if point[0] == 0 else point[0] - 1
                        require(differences(expected, tree)["equal"],
                                "Rack move changed more than plugin order and indexed automation")
                elif stage in ("rack-remove", "redo-remove"):
                    require(ids == plugin_ids[1:], "Rack removal or Redo changed surviving identities")
                if stage in ("undo-remove", "restore-remove"):
                    require(differences(prior["rack-move"], tree)["equal"], "Undo did not restore the exact prior tree")
                elif stage == "redo-remove":
                    require(differences(prior["rack-remove"], tree)["equal"], "Redo did not restore the exact removed tree")
                prior[stage] = tree
            loader.load(path)
            if leg == "clean":
                clean["reopened"] = clean["restore-remove"]
            path, _ = save(leg, "reopened")
        report["exchangeInput"] = path.name
        report["passed"] = True
        print("EDITED METADATA: title, graph label, rack move/remove, Undo/Redo and reopen retained exactly", flush=True)
        return report
    except Exception as error:
        report["error"] = dict(type=type(error).__name__, message=str(error))
        raise
    finally:
        report["inputsAndOutputsUnchanged"] = all(sha256(read_bytes(p)) == value for p, value in immutable.items())
        if not report["inputsAndOutputsUnchanged"]:
            report["passed"] = False
        write_json(output / "report.json", report)
