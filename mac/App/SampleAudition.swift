import AVFoundation

struct SampleAuditionData {
  let path: String, rate: Double, channels: Int, frames: Int, totalFrames: Int, peaks: [Float], pcm: Data
  init(_ data: [String: Any]) throws {
    guard let path = data["path"] as? String, let rate = data["rate"] as? Double, rate.isFinite, (100...768000).contains(rate),
      let channels = data["channels"] as? Int, (1...2).contains(channels), let frames = data["previewFrames"] as? Int,
      frames > 0, frames <= 2_097_152, let pcm = data["pcm"] as? Data, pcm.count == frames * channels * 4 else {
      throw NSError(domain: "SampleAudition", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid decoded preview audio"])
    }
    self.path = path; self.rate = rate; self.channels = channels; self.frames = frames; self.pcm = pcm
    totalFrames = data["frames"] as? Int ?? frames; peaks = (data["peaks"] as? [NSNumber] ?? []).map(\.floatValue)
  }
  var seconds: Double { Double(totalFrames) / rate }
  var previewSeconds: Double { Double(frames) / rate }
  var dictionary: [String: Any] { ["path": path, "rate": rate, "channels": channels, "frames": totalFrames,
    "previewFrames": frames, "seconds": seconds, "previewSeconds": previewSeconds, "peaks": peaks] }
  func buffer() throws -> AVAudioPCMBuffer {
    guard let format = AVAudioFormat(standardFormatWithSampleRate: rate, channels: AVAudioChannelCount(channels)),
      let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frames)), let output = buffer.floatChannelData else {
      throw NSError(domain: "SampleAudition", code: 2, userInfo: [NSLocalizedDescriptionKey: "Cannot prepare preview audio"])
    }
    buffer.frameLength = AVAudioFrameCount(frames)
    pcm.withUnsafeBytes { bytes in
      for frame in 0..<frames { for channel in 0..<channels {
        let value = bytes.loadUnaligned(fromByteOffset: (frame * channels + channel) * 4, as: Float.self)
        // A short edge fade suppresses clicks while rapidly auditioning hits.
        let fade = min(1, Double(min(frame + 1, frames - frame)) / max(1, rate * 0.002))
        output[channel][frame] = value.isFinite ? max(-1, min(1, value)) * Float(fade) : 0
      } }
    }
    return buffer
  }
}

/// A dedicated preview player. It never changes the song's transport, mixer,
/// plugin rack or undo history. Construction alone never opens an audio device.
final class SampleAuditionPlayer {
  private let makeEngine: () throws -> AVAudioEngine
  private var engine: AVAudioEngine?, node: AVAudioPlayerNode?
  private(set) var playing = false
  private var generation = 0
  var volume: Float = 0.25 { didSet { node?.volume = max(0, min(1, volume)) } }
  init(makeEngine: @escaping () throws -> AVAudioEngine = { AVAudioEngine() }) { self.makeEngine = makeEngine }
  func play(_ buffer: AVAudioPCMBuffer, completion: @escaping () -> Void = {}) throws {
    stop(); generation += 1; let generation = generation
    let engine = try self.engine ?? makeEngine(), node = self.node ?? AVAudioPlayerNode()
    if self.engine == nil { engine.attach(node); self.engine = engine; self.node = node }
    engine.stop(); engine.disconnectNodeOutput(node)
    engine.connect(node, to: engine.mainMixerNode, format: buffer.format)
    node.volume = volume
    node.scheduleBuffer(buffer, completionCallbackType: .dataPlayedBack) { [weak self] _ in
      DispatchQueue.main.async {
        guard let self, self.generation == generation else { return }
        self.node?.stop(); self.engine?.stop(); self.playing = false; completion()
      }
    }
    try engine.start(); node.play(); playing = true
  }
  func stop() { generation += 1; node?.stop(); engine?.stop(); playing = false }
}

/// Cancellable-by-generation decoding, a bounded cache, and a single audible
/// voice: a late result can never play over a newer selection or after Stop.
final class SampleAudition {
  typealias Decoder = (String) throws -> [String: Any]
  private let queue = DispatchQueue(label: "org.resonance.sample-preview", qos: .userInitiated)
  private var work: DispatchWorkItem?, generation = 0
  private var cache = [String: (Double, SampleAuditionData, AVAudioPCMBuffer)](), order = [String]()
  private let player = SampleAuditionPlayer()
  private var pending: ((Result<SampleAuditionData, Error>) -> Void)?
  private(set) var path: String?, playing = false
  var volume: Float = 0.25 { didSet { player.volume = volume } }
  var onFinish: (() -> Void)?
  func stop() {
    generation += 1; work?.cancel(); player.stop(); path = nil; playing = false
    let cancelled = pending; pending = nil; cancelled?(.failure(CocoaError(.userCancelled))); onFinish?()
  }
  func prepare(path: String, audible: Bool, decoder: @escaping Decoder, completion: @escaping (Result<SampleAuditionData, Error>) -> Void) {
    stop(); self.path = path; let generation = generation
    pending = completion
    let work = DispatchWorkItem { [weak self] in
      guard let self else { return }
      do {
        let modified = try URL(fileURLWithPath: path).resourceValues(forKeys: [.contentModificationDateKey]).contentModificationDate?.timeIntervalSince1970 ?? 0
        let data: SampleAuditionData, buffer: AVAudioPCMBuffer
        if let cached = self.cache[path], cached.0 == modified { data = cached.1; buffer = cached.2 }
        else { data = try SampleAuditionData(decoder(path)); buffer = try data.buffer() }
        self.cache[path] = (modified, data, buffer); self.order.removeAll { $0 == path }; self.order.append(path)
        while self.order.count > 8 || self.cache.values.reduce(0, { $0 + $1.1.pcm.count * 2 }) > 32 * 1024 * 1024 {
          self.cache.removeValue(forKey: self.order.removeFirst())
        }
        DispatchQueue.main.async {
          guard self.generation == generation else { return }
          do {
            if audible { try self.player.play(buffer) { [weak self] in self?.playing = false; self?.onFinish?() }; self.playing = true }
            self.pending = nil; completion(.success(data))
          } catch { self.pending = nil; self.playing = false; completion(.failure(error)) }
        }
      } catch { DispatchQueue.main.async { guard self.generation == generation else { return }; self.pending = nil; completion(.failure(error)) } }
    }
    self.work = work; queue.async(execute: work)
  }
}
