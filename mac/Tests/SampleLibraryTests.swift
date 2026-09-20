import Foundation
import AVFoundation

@main struct SampleLibraryTests {
  static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
    if !condition() { throw NSError(domain: "SampleLibraryTests", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
  }
  static func wait(_ condition: () -> Bool, seconds: Double = 10) throws {
    let end = Date().addingTimeInterval(seconds)
    while !condition() && Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
    try require(condition(), "Asynchronous operation completed")
  }
  static func main() throws {
    try multisampleChecks()
    let manager = FileManager.default, root = manager.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try manager.createDirectory(at: root, withIntermediateDirectories: true); defer { try? manager.removeItem(at: root) }
    let library = root.appendingPathComponent("Packs"), cache = root.appendingPathComponent("Cache")
    for path in ["808 From Mars/WAV/01. Kicks/BD Smooth 01.wav", "808 From Mars/Maschine/01. Kicks/BD Smooth 01.wav",
                 "808 From Mars/WAV/Snares/SD Snappy.wav", "Junos From Mars/WAV/05. Chords/Éclair C4.aiff",
                 "WAV/WAV/Hi Hat.wav", "__MACOSX/._hidden.wav", ".private/hidden.wav", "not audio.txt"] {
      let file = library.appendingPathComponent(path); try manager.createDirectory(at: file.deletingLastPathComponent(), withIntermediateDirectories: true)
      try Data([1, 2, 3]).write(to: file)
    }
    try manager.createSymbolicLink(atPath: library.appendingPathComponent("cycle").path, withDestinationPath: library.path)
    let index = try SampleLibraryIndex.scan(roots: [library.path, library.appendingPathComponent("808 From Mars").path])
    try require(index.snapshot.entries.count == 5, "Hidden metadata, non-audio, symlink cycles and overlapping roots do not duplicate results")
    try require(index.search(SampleLibraryQuery(text: "808 kicks")).total == 2, "Ancestor directory names participate in all-term search")
    try require(index.search(SampleLibraryQuery(text: "808 kicks -maschine")).total == 1, "Exclusion terms can remove duplicate pack formats")
    try require(index.search(SampleLibraryQuery(text: "\"Junos From Mars\" eclair")).total == 1, "Quoted pack phrases and diacritics are searchable")
    try require(index.search(SampleLibraryQuery(tags: ["808 From Mars", "WAV"])).total == 2, "Multiple inherited folder tags intersect")
    try require(index.search(SampleLibraryQuery(root: library.appendingPathComponent("808 From Mars").path)).total == 3, "Overlapping roots remain independently searchable without duplicate entries")
    let all = index.search(SampleLibraryQuery())
    try require(all.tags.first(where: { $0.0 == "WAV" })?.1 == 4, "Repeated folder names count once per sample")
    let page = index.search(SampleLibraryQuery(offset: 1, limit: 2))
    try require(page.items == Array(all.items[1..<3]) && page.total == 5, "Stable pagination retains the full match count")
    let service = SampleLibrary(directory: cache)
    service.load(defaultRoots: [library.path]); try wait { service.index != nil && !service.indexing }
    let before = service.revision
    try service.setRoots([library.path]); try wait { service.index != nil && !service.indexing }
    try require(service.revision != before, "Rescans publish a new library revision")
    let reloaded = SampleLibrary(directory: cache); reloaded.load(defaultRoots: [])
    try wait { reloaded.index != nil && !reloaded.indexing }
    try require(reloaded.index?.snapshot.indexedAt == service.index?.snapshot.indexedAt, "Reopening uses the persisted index without rescanning")
    let extra = library.appendingPathComponent("new.wav"); try Data([0]).write(to: extra)
    reloaded.rescan(); try wait { reloaded.index?.snapshot.entries.count == 6 && !reloaded.indexing }
    try require(FileManager.default.fileExists(atPath: extra.path), "Indexing does not alter sample files")
    try reloaded.setRoots([]); try wait { !reloaded.indexing }; try require(reloaded.index?.snapshot.entries.isEmpty == true, "Removing a root only clears its index")

    let audioFile = root.appendingPathComponent("preview.wav"); try Data([0]).write(to: audioFile)
    let frames = 4800, rate = 48000.0
    let pcm = (0..<frames).map { Float(sin(Double($0) * 2 * .pi * 440 / rate) * 0.5) }
    let bytes = pcm.withUnsafeBufferPointer { Data(buffer: $0) }
    let data: [String: Any] = ["path": audioFile.path, "rate": rate, "channels": 1, "previewFrames": frames, "frames": frames, "pcm": bytes, "peaks": [0.0, 0.5]]
    let decoded = try SampleAuditionData(data), buffer = try decoded.buffer()
    try require(buffer.frameLength == frames && abs(buffer.floatChannelData![0][1000] - pcm[1000]) < 1e-7, "Audition PCM keeps source amplitude after its edge fade")
    let engine = AVAudioEngine()
    try engine.enableManualRenderingMode(.offline, format: buffer.format, maximumFrameCount: 512)
    let player = SampleAuditionPlayer(makeEngine: { engine }); player.volume = 0.5
    try player.play(buffer)
    let out = AVAudioPCMBuffer(pcmFormat: engine.manualRenderingFormat, frameCapacity: 512)!
    var rendered = [Float]()
    while rendered.count < frames {
      let result = try engine.renderOffline(AVAudioFrameCount(min(512, frames - rendered.count)), to: out)
      try require(result == .success, "Offline preview renderer produces audio")
      rendered += Array(UnsafeBufferPointer(start: out.floatChannelData![0], count: Int(out.frameLength)))
    }
    player.stop()
    try require(rendered.contains { abs($0) > 0.24 } && rendered.allSatisfy(\.isFinite), "Actual preview player renders finite audio at its selected gain without hardware")
    try require(abs(rendered[1000] - pcm[1000] * 0.5) < 1e-6, "Preview output matches the source signal and independent gain")
    let stereo = AVAudioPCMBuffer(pcmFormat: AVAudioFormat(standardFormatWithSampleRate: 44100, channels: 2)!, frameCapacity: 4410)!
    stereo.frameLength = 4410
    for channel in 0..<2 { for frame in 0..<4410 { stereo.floatChannelData![channel][frame] = 0.2 } }
    try player.play(stereo)
    let switched = try engine.renderOffline(512, to: out)
    try require(switched == .success, "Rapid audition can switch rate and channel layout")
    try require(out.floatChannelData![0][400].isFinite && out.floatChannelData![0][400] > 0.05, "Resampled preview remains audible")
    player.stop(); try require(!player.playing && !engine.isRunning, "Stop releases the preview renderer")
    let audition = SampleAudition(); var oldCancelled = false, currentReady = false
    audition.prepare(path: audioFile.path, audible: false, decoder: { _ in data }) { result in if case .failure = result { oldCancelled = true } }
    audition.prepare(path: audioFile.path, audible: false, decoder: { _ in data }) { result in if case .success = result { currentReady = true } }
    try wait { oldCancelled && currentReady }
    try require(!audition.playing, "Stale preview requests complete as cancelled and tests never open hardware")
    audition.stop()
    if let option = CommandLine.arguments.firstIndex(of: "--library"), CommandLine.arguments.indices.contains(option + 1) {
      let start = Date(); let real = try SampleLibraryIndex.scan(roots: [CommandLine.arguments[option + 1]])
      print(String(format: "Real library: %d samples indexed in %.3f s", real.snapshot.entries.count, Date().timeIntervalSince(start)))
      if let entry = real.snapshot.entries.first(where: { $0.name == "077 Clav Junos F4.wav" }), let group = real.multisample(for: entry.path) {
        try require(group.members.count == 109 && group.suggestedOctaveShift == 2, "Actual Clav family detects all 109 roots and its numbered octave convention")
        let sources = try group.sources(octaveShift: group.suggestedOctaveShift)
        try require(sources.first?["rootNote"] as? Int == 1 && sources.last?["rootNote"] as? Int == 109, "Clav mapping spans tracker C-0 to C-9")
        print("PASS actual Clav family: 109 samples, numbered-key +2 octave alignment, C-0…C-9")
      }
      for query in ["", "808 \"bass drum\"", "Junos chords", "snare -maschine", "\"From Mars\" WAV"] {
        let start = Date(), result = real.search(SampleLibraryQuery(text: query))
        print(String(format: "Search '%@': %d matches in %.2f ms", query, result.total, Date().timeIntervalSince(start) * 1000))
      }
    }
    print("PASS sample library: inherited tags, search, pagination, hidden files, roots, cache, rescan, preview cancellation and offline audio")
  }
  static func multisampleChecks() throws {
    func entry(_ name: String, folder: String = "/Packs/Keys") -> SampleLibraryEntry {
      SampleLibraryEntry(path: folder + "/" + name, root: "/Packs", name: name, folders: ["Packs", "Keys"], bytes: 1, modified: 0)
    }
    try require(SampleFilenameNote.parse("077 Clav Junos F4.wav")?.semitone == 53, "Filename note parsing strips ordinal prefixes")
    try require(SampleFilenameNote.parse("Piano_Db4.wav")?.semitone == 49 && SampleFilenameNote.parse("Piano C♯4.wav")?.semitone == 49, "Flats and Unicode accidentals are recognized")
    try require(SampleFilenameNote.parse("000 Clav C-2.wav")?.semitone == -24, "Signed octaves are preserved before mapping")
    for name in ["Kick 808.wav", "AC4unit.wav", "Chord C4 E4.wav", "Lead C400.wav"] { try require(SampleFilenameNote.parse(name) == nil, "Non-note and ambiguous samples do not become families") }
    let bare = MultisampleGroup.detect([entry("C4.wav"),entry("D4.wav")])
    try require(bare.count == 1 && bare[0].name == "Keys", "Note-only filenames use their containing folder as the instrument name")
    let entries = [entry("077 Clav Junos F4.wav"),entry("078 Clav Junos F#4.wav"),entry("079 Clav Junos G4.wav"),
      entry("080 Other G#4.wav"),entry("077 Clav Junos F4.wav",folder:"/Packs/Other")]
    let groups = MultisampleGroup.detect(entries)
    try require(groups.count == 1 && groups[0].members.count == 3 && groups[0].suggestedOctaveShift == 2, "Grouping stays within one folder, family and extension; serials suggest an octave only when consistent")
    let sources = try groups[0].sources(octaveShift: 2)
    try require(sources[0]["rootNote"] as? Int == 78, "Numbered key 77 maps to tracker F-6 / one-based note 78")
    let variants = MultisampleGroup.detect([entry("Keys_C4_v1.wav"),entry("Keys_D4_v1.wav"),entry("Keys_C4_v2.wav"),entry("Keys_D4_v2.wav")])
    try require(variants.count == 2, "Velocity and round-robin suffixes are not discarded")
    let duplicate = MultisampleGroup.detect([entry("Keys A#4.wav"),entry("Keys Bb4.wav"),entry("Keys C5.wav")])[0]
    do { _ = try duplicate.sources(octaveShift: 0); throw NSError(domain:"Tests",code:99) }
    catch { try require((error as NSError).code == -32602, "Enharmonic duplicate roots block import") }
    let negative = MultisampleGroup.detect([entry("Keys C-2.wav"),entry("Keys D-2.wav")])[0]
    do { _ = try negative.sources(octaveShift: 0); throw NSError(domain:"Tests",code:99) }
    catch { try require((error as NSError).code == -32602, "Out-of-range roots require an explicit octave correction") }
    print("PASS filename multisamples: notes/accidentals/octaves, family isolation, ordinal alignment, variants and duplicate/root bounds")
  }
}
