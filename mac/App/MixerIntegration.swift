import AppKit
extension AppController {
  @objc func showMixer() {
    guard !busy else { return }
    workspace?.show("mixer");followWorkspacePanel("mixer",force:true)
  }
}
