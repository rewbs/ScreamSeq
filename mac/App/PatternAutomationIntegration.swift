import AppKit
extension AppController {
  func automateParameter(plugin: String, parameter: Int) {
    capturePanelReturn("automation")
    openParameterSource(["kind": "envelope", "plugin": plugin, "parameter": parameter, "pattern": model.pattern])
  }
  @objc func showPatternAutomation() {
    guard !busy else { return }
    capturePanelReturn("automation")
    workspace?.show("automation");followWorkspacePanel("automation",force:true)
  }
}
