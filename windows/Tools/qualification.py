"""Fail-closed checks; accepting a report does not authenticate its provenance.

Gate: >=60 seconds of visible 60 Hz presentation, p99 interval <=18.5 ms,
maximum <=33.5 ms, plus >=60 seconds of audited live audio with no reported
faults. No physical-loopback or commercial-plugin qualification is implied.
"""
import math
import re


def validate(report):
    failures = []
    if not isinstance(report, dict):
        report = {}
    for key, length in (("sourceCommit", 40), ("executableSha256", 64)):
        if not isinstance(report.get(key), str) or not re.fullmatch(
            "[0-9a-f]{%d}" % length, report[key]
        ):
            failures.append(key + " is missing or malformed")
    if not isinstance(report.get("workload"), str) or not report["workload"].strip():
        failures.append("workload is missing")

    presentation = report.get("presentation")
    if not isinstance(presentation, dict):
        failures.append("presentation measurement is missing")
    else:
        if presentation.get("source") not in ("dxgi-frame-statistics", "presentmon-displayed"):
            failures.append("presentation requires displayed-frame timing, not CPU submission")
        for key in ("measurementStarted", "visibleThroughout"):
            if presentation.get(key) is not True:
                failures.append("presentation " + key + " is not true")
        if type(presentation.get("occludedFrames")) is not int or presentation["occludedFrames"] != 0:
            failures.append("presentation includes occlusion or lacks its counter")
        intervals = presentation.get("intervalsMs")
        if not isinstance(intervals, list) or not intervals or not all(
            type(value) in (int, float) and math.isfinite(value) and value > 0
            for value in intervals
        ):
            failures.append("presentation intervals must be finite positive measurements")
        else:
            duration = math.fsum(intervals)
            ordered = sorted(intervals)
            if duration < 60000 - 1e-6:
                failures.append("presentation measured less than 60 seconds")
            if len(intervals) * 1000 / duration < 59:
                failures.append("presentation average is below 59 fps")
            if ordered[math.ceil(len(ordered) * 0.99) - 1] > 18.5 or ordered[-1] > 33.5:
                failures.append("presentation frame-pacing limit exceeded")

    audio = report.get("audio")
    if not isinstance(audio, dict):
        failures.append("audio measurement is missing")
    else:
        if audio.get("source") != "wasapi-event":
            failures.append("audio is not a live WASAPI event measurement")
        numbers = ("sampleRate", "periodFrames", "durationSeconds", "callbackCount",
                   "renderedFrames", "maxCallbackMicros")
        valid = all(type(audio.get(key)) in (int, float) and math.isfinite(audio[key])
                    and audio[key] > 0 for key in numbers)
        if not valid:
            failures.append("audio measurement fields must be finite positive numbers")
        else:
            if audio["durationSeconds"] < 60:
                failures.append("audio measured less than 60 seconds")
            expected = audio["sampleRate"] * audio["durationSeconds"]
            if abs(audio["renderedFrames"] - expected) > 2 * audio["periodFrames"]:
                failures.append("audio rendered frames disagree with measurement duration")
            if audio["maxCallbackMicros"] > 1e6 * audio["periodFrames"] / audio["sampleRate"]:
                failures.append("audio callback exceeded the negotiated period")
        for key in ("deadlineOverruns", "starvations", "deviceErrors"):
            if type(audio.get(key)) is not int or audio[key] != 0:
                failures.append("audio " + key + " is nonzero or missing")
        if audio.get("realtimeAuditPassed") is not True:
            failures.append("audio realtime allocation/free/lock audit has not passed")
    return failures
