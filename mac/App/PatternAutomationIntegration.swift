import AppKit
extension AppController {
  @objc func showPatternAutomation() {
    guard !busy else { return }
    workspaceReturnPoints["automation"]=patternView.navigation
    workspace?.show("automation");followWorkspacePanel("automation",force:true)
  }
}
