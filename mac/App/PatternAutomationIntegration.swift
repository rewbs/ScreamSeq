import AppKit
extension AppController {
  func automateParameter(plugin: String, parameter: Int) {
    workspaceReturnPoints["automation"] = patternView.navigation
    openParameterSource(["kind": "envelope", "plugin": plugin, "parameter": parameter, "pattern": model.pattern])
  }
  @objc func showPatternAutomation() {
    guard !busy else { return }
    workspaceReturnPoints["automation"]=patternView.navigation
    workspace?.show("automation");followWorkspacePanel("automation",force:true)
  }
}
