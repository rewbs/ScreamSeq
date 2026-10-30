import AppKit

/// Runs the real session + socket without any windows, activation, or audio output.
@main struct AutomationHost {
  static func main() throws {
    let app = NSApplication.shared
    app.setActivationPolicy(.prohibited)
    let session = TrackerSession()
    let root = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
    var navigation = EditorNavigation(pattern:0,row:7,channel:2,column:0,following:true)
    var admissionAttempts = 0, pendingCalls = 0
    var receiptCalls = [String: Int]()
    var heldReply: AutomationServer.Reply?
    let server = try AutomationServer(discoveryDirectory: root) { method, params, reply in
      // Fault injection belongs only to this private socket host. These names
      // are absent from the application's dispatcher and advertised schema.
      if method == "test.admissionBusy" {
        admissionAttempts += 1
        if admissionAttempts <= 2 { reply(AutomationServer.notAdmittedBusy("Test admission is busy")) }
        else { reply(["result": ["attempts": admissionAttempts]]) }
      } else if method == "test.busyReceipt" {
        let outcome = params["outcome"] as? String ?? "unclassified"
        receiptCalls[outcome, default: 0] += 1
        var data: [String: Any] = ["calls": receiptCalls[outcome]!]
        if outcome != "unclassified" { data["writeOutcome"] = outcome }
        reply(AutomationServer.error(-32002, "Test retained busy receipt", data: data))
      } else if method == "test.pending" {
        pendingCalls += 1
        heldReply = reply
      } else if method == "test.pending.get" {
        reply(["result": ["calls": pendingCalls, "held": heldReply != nil]])
      } else if method == "test.pendingRelease" {
        let finish = heldReply; heldReply = nil
        finish?(["result": ["calls": pendingCalls, "completed": true]])
        reply(["result": ["released": finish != nil]])
      } else if method == "test.nonfinite" {
        // Test-host-only fault injection. Never advertised or dispatched by the app.
        reply(["result":["nested":[["positive":Double.infinity,"negative":-Double.infinity,"nan":Double.nan]]]])
      } else if method == "context.get" || method == "context.set" {
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
          var data:[String:Any] = ["revision":session.automationRevision]
          if let outcome=error?.userInfo["writeOutcome"] as? String {data["writeOutcome"]=outcome}
          reply(
            AutomationServer.error(
              error?.code ?? -32003, error?.localizedDescription ?? "Failure",
              data:data))
        }
      }
    }
    print(server.socketPath)
    fflush(stdout)
    withExtendedLifetime(server) { app.run() }
  }
}
