import AppKit

extension InterfaceTests {
  static func patternSampleRenderChecks() throws {
    var target = PatternSampleTarget(context: .init(document: "song", revision: "song:1"),
      pattern: 2, firstRow: 4, lastRow: 7, firstChannel: 1, lastChannel: 2)
    let view = PatternSampleRenderView()
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void](), added = [(Int, Int)]()
    view.onTarget = { target }
    view.onRequest = { calls.append(($0, $1)); replies.append($2) }
    view.onAdded = { added.append(($0, $1)) }
    func receipt(_ params: [String: Any], document: String = "song", revision: String = "song:1") -> [String: Any] {
      var data = params
      data["sample"] = 3; data["instrument"] = params["createInstrument"] as? Bool == true ? 2 : 0
      data["frames"] = params["dryRun"] as? Bool == true ? 0 : 1024
      return ["result": ["documentId": document, "revision": revision, "data": data]]
    }
    func catalogue(document: String = "song", revision: String = "song:2", malformed: Bool = false) -> [String: Any] {
      ["result": ["documentId": document, "revision": revision, "data": [
        "samples": [["id": "sample-1", "index": malformed ? true : 3] as [String: Any]],
        "instruments": [["id": "instrument-1", "index": 2]]]]]
    }
    func move(_ revision: String, row: Int = 10, document: String = "song") {
      target = .init(context: .init(document: document, revision: revision), pattern: 9,
        firstRow: row, lastRow: row, firstChannel: 0, lastChannel: 0)
      view.documentChanged()
    }
    view.open(instrument: true)
    try require(!view.needsDocumentCompletion && view.target?.pattern == 2, "Opening captures the selection without creating a draft")
    move("song:1")
    view.name.stringValue = "Render draft"; view.tail.stringValue = "0.25"
    view.open(instrument: false)
    try require(view.target?.pattern == 2 && view.instrument.state == .on && view.needsDocumentCompletion,
      "Reopen keeps the captured range, output and raw draft after cursor movement")
    for invalid in ["", "NaN", "inf", "-1", "60.1"] {
      view.tail.stringValue = invalid; view.apply(dryRun: true)
    }
    try require(calls.isEmpty, "Invalid raw tails never dispatch")
    view.tail.stringValue = "0.25"; view.apply(dryRun: true)
    let checked = calls.last!.1
    try require(calls.count == 1 && checked["pattern"] as? Int == 2 && checked["firstRow"] as? Int == 4 &&
      checked["expectedRevision"] as? String == "song:1" && checked["dryRun"] as? Bool == true,
      "Check uses the original captured target and revision")
    view.apply(dryRun: false)
    try require(calls.count == 1 && view.needsDocumentCompletion, "A pending check prevents a concurrent render and document replacement")
    replies.removeFirst()(receipt(checked))
    try require(view.operation == nil && view.status.stringValue.contains("validated") && added.isEmpty, "Check validates without selecting or creating an asset")
    move("song:2"); view.apply(dryRun: false)
    try require(calls.count == 1 && view.target?.context.revision == "song:1", "Song edits require explicit recapture and never rebase the retained selection")
    view.captureSelection(); view.apply(dryRun: false)
    let submitted = calls.last!.1
    view.name.stringValue = "Newer raw name"; view.optionsChanged()
    move("song:3")
    replies.removeFirst()(receipt(submitted, revision: "song:3"))
    try require(added.count == 1 && added[0].0 == 3 && added[0].1 == 2 && view.name.stringValue == "Newer raw name" &&
      view.needsDocumentCompletion && view.operation == nil && !view.current,
      "A matching receipt reveals its assets without discarding newer input; another render needs explicit capture")
    view.discardOptions(); view.captureSelection(); view.apply(dryRun: false)
    let previousSong = calls.last!.1
    move("replacement:1", document: "replacement")
    replies.removeFirst()(receipt(previousSong, revision: "song:4"))
    try require(added.count == 1 && view.operation == nil && view.status.stringValue.contains("selection kept"),
      "A completed render cannot select equal-numbered slots in a replacement song")
    move("song:4"); view.captureSelection(); view.apply(dryRun: false)
    replies.removeFirst()(["error": ["code": -32002, "message": "Not admitted", "data": ["writeOutcome": "notCommitted"]]])
    try require(view.operation == nil && !view.pending, "Only explicit non-commit evidence releases a refused write")
    view.apply(dryRun: false)
    replies.removeFirst()(["error": ["code": -32002, "message": "Unclassified busy"]])
    let retained = view.operation
    let writes = calls.count
    view.apply(dryRun: false); view.captureSelection(); view.discardOptions()
    try require(view.operation === retained && calls.count == writes && view.needsDocumentCompletion,
      "An uncertain result survives Render, recapture, discard and hidden document census without replay")
    view.reviewResult()
    try require(calls.last?.0 == "document.get", "Review reads current assets instead of submitting another render")
    replies.removeFirst()(catalogue(revision: "song:4", malformed: true))
    try require(view.operation === retained && view.operation?.observation == nil, "Malformed observed catalogue cannot authorize acknowledgement")
    view.reviewResult(); replies.removeFirst()(catalogue(revision: "song:4"))
    try require(view.accept.isEnabled && view.operation?.observation != nil, "Validated current catalogue makes explicit acceptance available")
    move("song:5"); view.acceptObservation()
    try require(view.operation === retained && view.operation?.observation == nil, "Accept refuses a changed document revision")
    view.reviewResult(); replies.removeFirst()(catalogue(revision: "song:5"))
    view.name.stringValue = "New input after review"; view.acceptObservation()
    try require(view.operation === retained && view.operation?.observation == nil, "Accept detects raw changes even before a field notification")
    view.reviewResult(); replies.removeFirst()(catalogue(revision: "song:5"))
    let beforeAccept = calls.count
    view.acceptObservation()
    try require(view.operation == nil && !view.current && calls.count == beforeAccept && added.count == 1 &&
      view.name.stringValue == "New input after review" && view.needsDocumentCompletion,
      "Acceptance preserves raw input, never claims authorship or selects a guessed asset, and requires recapture")
    view.discardOptions()
    try require(!view.needsDocumentCompletion, "Explicit local discard releases an acknowledged options draft")

    // A malformed receipt is an uncertain append, even when the envelope is a success.
    for corruption in ["missing", "wrong-document", "wrong-range", "wrong-tail-type", "wrong-output", "wrong-dry-run"] {
      view.captureSelection(); view.apply(dryRun: false)
      var response = receipt(calls.last!.1, revision: "song:6")
      var result = response["result"] as! [String: Any], data = result["data"] as! [String: Any]
      switch corruption {
      case "missing": response = [:]
      case "wrong-document": result["documentId"] = "different"
      case "wrong-range": data["firstRow"] = 999
      case "wrong-tail-type": data["tailSeconds"] = false
      case "wrong-output": data["instrument"] = 7
      default: data["dryRun"] = true
      }
      if corruption != "missing" { result["data"] = data; response["result"] = result }
      replies.removeFirst()(response)
      try require(view.operation != nil && added.count == 1, "Invalid \(corruption) receipt retains ownership")
      view.reviewResult(); replies.removeFirst()(catalogue(revision: "song:5")); view.acceptObservation()
    }
    try patternSampleDirectChecks()
    try patternSampleKeyboardChecks()
  }
  static func patternSampleDirectChecks() throws {
    let direct = PatternSampleRenderView(direct: true), options = PatternSampleRenderView()
    let target = PatternSampleTarget(context: .init(document: "song", revision: "song:1"), pattern: 0,
      firstRow: 3, lastRow: 3, firstChannel: 0, lastChannel: 0)
    var calls = [(String, [String: Any])](), replies = [([String: Any]) -> Void]()
    for view in [direct, options] {
      view.onTarget = { target }; view.onRequest = { calls.append(($0, $1)); replies.append($2) }
    }
    direct.otherHasUnresolvedResult = { options.hasUnresolvedResult }
    options.otherHasUnresolvedResult = { direct.hasUnresolvedResult }
    options.open(instrument: false); options.name.stringValue = "Kept options"
    direct.runDirect(instrument: true)
    try require(calls.count == 1 && calls[0].1["name"] as? String == "Rendered selection" &&
      calls[0].1["tailSeconds"] as? Double == 0 && calls[0].1["createInstrument"] as? Bool == true &&
      calls[0].1["firstRow"] as? Int == 3 && calls[0].1["lastRow"] as? Int == 3,
      "Quick action renders the cursor row with fixed defaults independently of an options draft")
    options.apply(dryRun: false)
    try require(calls.count == 1 && options.name.stringValue == "Kept options", "Options cannot race an unresolved quick render")
    replies.removeFirst()([:]); direct.runDirect(instrument: false)
    try require(calls.count == 2 && calls.last?.0 == "document.get" && direct.operation?.options.instrument == true,
      "Repeating the quick action reviews the original submission instead of rendering a different output")
    replies.removeFirst()(["result": ["documentId": "song", "revision": "song:1", "data": ["samples": [], "instruments": []]]])
    direct.acceptObservation(); options.apply(dryRun: false)
    let count = calls.count; direct.runDirect(instrument: false)
    try require(options.pending && calls.count == count, "Quick actions cannot race an unresolved options render")
    replies.removeFirst()(["error": ["data": ["writeOutcome": "notCommitted"]]])
  }
  static func patternSampleKeyboardChecks() throws {
    let view = PatternSampleRenderView()
    let target = PatternSampleTarget(context: .init(document: "song", revision: "song:1"), pattern: 0,
      firstRow: 0, lastRow: 0, firstChannel: 0, lastChannel: 0)
    view.onTarget = { target }; view.open(instrument: false)
    let window = PatternSampleRenderWindow(contentRect: NSRect(x: 0, y: 0, width: 690, height: 490),
      styleMask: [.titled, .closable], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false; window.contentView = view; window.delegate = view
    window.autorecalculatesKeyViewLoop = false; window.makeFirstResponder(view.name)
    let editor = view.name.currentEditor() as! NSTextView
    editor.string = "Active field text"; editor.setSelectedRange(NSRange(location: 2, length: 3))
    view.documentChanged(); view.open(instrument: true)
    try require(view.raw.name == "Active field text" && view.name.currentEditor() === editor &&
      editor.selectedRange() == NSRange(location: 2, length: 3) && view.needsDocumentCompletion,
      "Refresh and reopen preserve active field text, caret and ownership")
    let tab = NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: [], timestamp: 0,
      windowNumber: window.windowNumber, context: nil, characters: "\t", charactersIgnoringModifiers: "\t", isARepeat: false, keyCode: 48)!
    window.sendEvent(tab)
    try require(view.tail.currentEditor() != nil, "Tab moves from name to tail")
    window.sendEvent(tab)
    try require(window.firstResponder === view.instrument, "Tab includes the output checkbox regardless of system keyboard navigation")
    window.sendEvent(tab)
    try require(window.firstResponder === view.check, "Tab reaches Check")
    var reply: (([String: Any]) -> Void)?, count = 0
    view.onRequest = { _, _, callback in count += 1; reply = callback }
    view.check.performClick(nil); view.check.performClick(nil)
    try require(count == 1 && window.firstResponder === view.check, "Pending Check keeps keyboard focus and refuses duplicate activation")
    reply?(["error": ["message": "Check unavailable"]])
    try require(window.firstResponder === view.check, "Check completion preserves its focused action")
    window.sendEvent(tab)
    try require(window.firstResponder === view.render, "Tab advances from completed Check to Render")
    view.render.performClick(nil); view.render.performClick(nil)
    try require(count == 2 && window.firstResponder === view.render, "Pending Render retains focus and cannot send twice")
    reply?([:])
    try require(window.firstResponder === view.render && view.operation != nil, "An uncertain result keeps focus on Review")
    view.render.performClick(nil)
    try require(count == 3 && window.firstResponder === view.render, "Review also preserves its button while reading")
    reply?(["result": ["documentId": "song", "revision": "song:1", "data": ["samples": [], "instruments": []]]])
    window.sendEvent(tab); view.accept.performClick(nil)
    try require(window.firstResponder === view.useSelection && view.operation == nil, "Accept moves its retiring button to the required recapture action")
    window.makeFirstResponder(view.name)
    let composing = view.name.currentEditor() as! NSTextView
    composing.setMarkedText("draft", selectedRange: NSRange(location: 5, length: 0), replacementRange: NSRange(location: 0, length: composing.string.count))
    try require(!view.advanceKeyboardFocus(reverse: false) && view.needsDocumentCompletion && composing.hasMarkedText(),
      "Keyboard traversal and document admission respect IME composition")
    composing.unmarkText(); window.makeFirstResponder(nil)
    view.pending = true
    try require(!view.windowShouldClose(window), "A pending request cannot be hidden by closing its window")
    view.pending = false; view.discardOptions()
    try require(!view.needsDocumentCompletion && !view.current, "Discard leaves an explicit recapture requirement")
  }
}
