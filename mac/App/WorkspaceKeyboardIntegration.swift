import AppKit

extension AppController {
  var transportCommand: WorkspaceCommandPalette.Entry? {commandPalette.entries.first(where:WorkspaceCommandPalette.isTransport)}
  var keyboardState:[String:Any] {KeyboardSettings.snapshot(keys:transportCommand.map{commandPalette.shortcutKeys($0)} ?? KeyboardSettings.legacyPlayStopKeys)}
  func installKeyboardPreferences() {
    KeyboardSettings.playStopKeysProvider = { [weak self] in
      guard let self,let command=self.transportCommand else{return KeyboardSettings.legacyPlayStopKeys}
      return self.commandPalette.shortcutKeys(command)
    }
    keyboardSettings.persistPreferences = !inspectionTest && !automationTest
    keyboardSettings.onRead = { [weak self] in
      guard let self else{return [:]};self.commandPalette.collect();return self.keyboardState
    }
    keyboardSettings.onWrite = { [weak self] params in
      guard let self else{throw KeyboardSettings.Failure(code:-32001,message:"Application closed")}
      return try self.applyKeyboardPreferences(params)
    }
  }
  func applyKeyboardPreferences(_ params:[String:Any]) throws -> [String:Any] {
    guard Set(params.keys)==["expectedKeyboard","lower","upper","playStopKeys"],
      let expected=params["expectedKeyboard"] as? String,let lower=params["lower"] as? String,
      let upper=params["upper"] as? String,let keys=params["playStopKeys"] as? [String],keys.count<=4 else {
      throw KeyboardSettings.Failure(code:-32602,message:"Supply expectedKeyboard, lower, upper and playStopKeys")
    }
    guard expected==keyboardState["revision"] as? String else {throw KeyboardSettings.Failure(code:-32001,message:"Keyboard settings changed; reload before applying this draft")}
    let mapping=try KeyboardSettings.validated(lower,upper)
    guard let command=transportCommand else {throw KeyboardSettings.Failure(code:-32001,message:"The transport command is not available")}
    if let error=commandPalette.validateShortcut(command.id,keys:keys) {throw KeyboardSettings.Failure(code:-32602,message:error)}
    // All admission is complete before publishing either part. Existing held
    // notes keep the pitch/asset captured at key-down in each editor's owner.
    if let error=commandPalette.setShortcut(command.id,keys:keys) {throw KeyboardSettings.Failure(code:-32602,message:error)}
    if mapping.lower != KeyboardSettings.lowKeys || mapping.upper != KeyboardSettings.highKeys {
      KeyboardSettings.publish(mapping,persist:!inspectionTest && !automationTest)
    }
    return keyboardState
  }
}
