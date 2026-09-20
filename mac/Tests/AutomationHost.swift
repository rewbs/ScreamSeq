import AppKit

/// Runs the real session + socket without any windows, activation, or audio output.
@main struct AutomationHost {
  static func main() throws {
    let app = NSApplication.shared
    app.setActivationPolicy(.prohibited)
    let session = TrackerSession()
    let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
    var navigation = EditorNavigation(pattern:0,row:7,channel:2,column:0,following:true)
    let server = try AutomationServer(discoveryDirectory: root) { method, params, reply in
      if method == "context.get" || method == "context.set" {
        let previous = navigation
        do {
          if method == "context.get" && !params.isEmpty { throw EditorNavigation.Failure(code:-32602,message:"context.get accepts no parameters") }
          if method == "context.set" {
            let model = session.snapshot(0)
            navigation = try navigation.prepared(params, revision:session.automationRevision,
              contextToken:navigation.token, patterns:model["patterns"] as? [[String:Any]] ?? [], channels:model["channels"] as? Int ?? 0)
          }
        } catch let error as EditorNavigation.Failure { reply(AutomationServer.error(error.code,error.message));return }
        catch { reply(AutomationServer.error(-32602,error.localizedDescription));return }
        var context = navigation.dictionary
        context["instrument"]=2;context["contextRevision"]=navigation.token
        context["playback"]=["playing":false,"pattern":-1,"row":-1]
        reply([
          "result": [
            "revision": session.automationRevision,
            "data": context,"changed":false,"contextChanged":navigation != previous,"playbackStopped":false,
          ]
        ])
      } else {
        var error: NSError?
        let result = session.automationMethod(method, params: params, error: &error)
        if let result {
          reply(["result": result])
        } else {
          reply(
            AutomationServer.error(
              error?.code ?? -32003, error?.localizedDescription ?? "Failure",
              data: ["revision": session.automationRevision]))
        }
      }
    }
    print(server.socketPath)
    fflush(stdout)
    withExtendedLifetime(server) { app.run() }
  }
}
