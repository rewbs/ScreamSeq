import AppKit

// Do not end editing: that can commit a field or discard marked text. Keep the
// existing responder and selection while the worker adopts the prepared song.
final class DocumentDepartureInput {
  private var monitor: Any?
  private(set) var blocked = false
  private(set) var adopted = false
  static let events: NSEvent.EventTypeMask = [
    .keyDown, .keyUp, .flagsChanged, .leftMouseDown, .leftMouseUp,
    .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp,
    .leftMouseDragged, .rightMouseDragged, .otherMouseDragged,
    .scrollWheel, .gesture, .magnify, .swipe, .rotate, .smartMagnify,
    .pressure, .tabletPoint, .tabletProximity,
  ]
  func begin() {
    precondition(Thread.isMainThread && !blocked)
    blocked = true; adopted = false
    monitor = NSEvent.addLocalMonitorForEvents(matching: Self.events) { [weak self] event in
      self?.blocked == true ? nil : event
    }
  }
  func finish(adopted: Bool) {
    precondition(Thread.isMainThread)
    self.adopted = adopted
    if !adopted { release() }
  }
  func refreshed() {
    precondition(Thread.isMainThread)
    if adopted { release() }
  }
  private func release() {
    if let monitor { NSEvent.removeMonitor(monitor) }
    monitor = nil; blocked = false; adopted = false
  }
  deinit { if let monitor { NSEvent.removeMonitor(monitor) } }
}
