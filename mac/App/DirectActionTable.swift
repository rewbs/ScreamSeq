import AppKit

/// A keyboard-accessible object list: Return/double-click act on the object,
/// Delete removes it, and a context click selects the object under the pointer.
final class DirectActionTable: NSTableView {
  var activate: (() -> Void)?, remove: (() -> Void)?
  var actions: (() -> NSMenu)?
  override init(frame: NSRect) {
    super.init(frame: frame); target = self; doubleAction = #selector(openSelected)
  }
  required init?(coder: NSCoder) { fatalError() }
  @objc private func openSelected() { activate?() }
  override func keyDown(with event: NSEvent) {
    if event.modifierFlags.intersection([.command, .control, .option]).isEmpty {
      if event.keyCode == 36 { activate?(); return }
      if event.keyCode == 51 || event.keyCode == 117, let remove { remove(); return }
    }
    super.keyDown(with: event)
  }
  override func menu(for event: NSEvent) -> NSMenu? {
    let row = row(at: convert(event.locationInWindow, from: nil))
    guard row >= 0 else { return nil }
    selectRowIndexes(IndexSet(integer: row), byExtendingSelection: false)
    return actions?()
  }
}
