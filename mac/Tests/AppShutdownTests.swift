import AppKit
import Darwin

private var lifecycle: (@convention(c) (Int32) -> Int32)?
private var documentDrained = false, recoveryDrained = false
private var heldController: AppController?

/// Exercise the real AppController / NSApplication termination path. The
/// controller remains retained through exit; ordinary ARC cleanup cannot pass.
enum AppShutdownTest {
  static func require(_ value: Bool, _ message: String) {
    if !value { fputs("FAIL \(message)\n", stderr); abort() }
  }
  static func start(_ controller: AppController) {
    heldController = controller
    require(controller.automationTest, "Use the silent --automation-test launch mode")
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { prepare(controller) }
    DispatchQueue.global().asyncAfter(deadline: .now() + 20) {
      fputs("FAIL application shutdown timed out\n", stderr); abort()
    }
  }
  static func prepare(_ controller: AppController) {
    guard controller.uiReady && !controller.busy else {
      DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { prepare(controller) }
      return
    }
    do {
      // Keep periodic inspector refreshes from starting an unrelated mutation
      // between the busy-refusal check and the intentionally queued read below.
      controller.tickTimer?.invalidate()
      let fixture = ProcessInfo.processInfo.environment["SCREAMSEQ_SHUTDOWN_FIXTURE"]!
      let handle = dlopen(fixture + "/Contents/MacOS/ResonanceFixture", RTLD_NOW | RTLD_LOCAL)
      require(handle != nil, "Load fixture diagnostics")
      let symbol = dlsym(handle, "ResonanceFixtureLifecycle")
      require(symbol != nil, "Find fixture diagnostics")
      lifecycle = unsafeBitCast(symbol!, to: (@convention(c) (Int32) -> Int32).self)
      let descriptor: [String: Any] = ["type": 0, "subtype": 0, "manufacturer": 0,
        "format": "VST3", "path": fixture, "classID": "5245534F4E414E434546464543540001",
        "name": "Resonance Test Gain", "isInstrument": false]
      try controller.session.addPlugin(descriptor)
      try controller.session.addPlugin(descriptor)
      require(lifecycle!(0) == 2, "Application retains two processors before Quit")
      controller.busy = true
      require(controller.applicationShouldTerminate(NSApp) == .terminateCancel,
        "An active document mutation must reject Quit")
      controller.busy = false
      // Simulate queued inspector reads that call a plugin on the main thread.
      // A synchronous main-thread wait for the worker would deadlock this case.
      let releaseRead = DispatchSemaphore(value: 0)
      controller.worker.async {
        releaseRead.wait()
        DispatchQueue.main.sync {
          _ = controller.session.pluginParameters(0)
          documentDrained = true
        }
      }
      controller.recoveryWriter.async {
        Thread.sleep(forTimeInterval: 0.1)
        recoveryDrained = true
      }
      atexit {
        AppShutdownTest.require(documentDrained && recoveryDrained, "Quit must finish queued document work and recovery writes")
        AppShutdownTest.require(heldController != nil, "Controller remains alive during process termination")
        AppShutdownTest.require((0...3).allSatisfy { lifecycle!($0) == 0 },
          "Quit must release every plugin and module entry in order before static destructors")
        print("PASS actual NSApplication Quit: pending main-thread plugin work, recovery drain, retained-session teardown")
        fflush(stdout)
      }
      // AppKit enters a nested modal loop for terminateLater. Invoke Quit from
      // the run loop, like a menu event, not inside a main-dispatch block that
      // would prevent that serial queue from serving the worker's sync call.
      Timer.scheduledTimer(withTimeInterval: 0.01, repeats: false) { _ in
        AppShutdownTest.require(!controller.busy, "Quit scenario starts without an unrelated mutation")
        DispatchQueue.global().asyncAfter(deadline: .now() + 0.05) { releaseRead.signal() }
        NSApp.terminate(nil)
      }
    } catch {
      fputs("FAIL \(error)\n", stderr); abort()
    }
  }
}
