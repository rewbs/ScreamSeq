import AppKit

enum EditorHistory {
  /// An untouched field must not hide document history just because it received
  /// focus. Real text edits retain AppKit's local Undo/Redo, including sheets.
  static func performTextHistory(in responder: NSResponder?, redo: Bool) -> Bool {
    guard let text = responder as? NSTextView, text.isEditable else { return false }
    // Do not undo the song underneath an unfinished input-method composition.
    if text.hasMarkedText() { return true }
    guard let history = text.undoManager, redo ? history.canRedo : history.canUndo else { return false }
    if redo { history.redo() } else { history.undo() }
    return true
  }
}
