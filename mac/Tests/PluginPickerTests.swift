import AppKit

@main struct PluginPickerTests {
  static func main() throws {
    NSApplication.shared.setActivationPolicy(.prohibited)
    let (empty, _) = PluginPicker.make([])
    precondition(!empty.buttons[0].isEnabled && empty.buttons[2].isEnabled)
    let (_, duplicates) = PluginPicker.make([
      ["name": "Same name", "format": "VST3"], ["name": "Same name", "format": "VST3"],
    ])
    precondition(duplicates.numberOfItems == 2)
    let (native, builtins) = PluginPicker.make([["name": "Gainer", "format": "Built-in"]], builtInOnly: true)
    precondition(native.buttons.map(\.title) == ["Add", "Cancel"] && builtins.numberOfItems == 1)
    precondition(builtins.titleOfSelectedItem == "Gainer · Built-in Effect")
    let (alert, picker) = PluginPicker.make([
      ["name": "Native Instruments: Battery 4", "format": "AU", "isInstrument": true],
      ["name": "Battery 4", "format": "VST3", "isInstrument": true],
    ])
    precondition(alert.buttons.map(\.title) == ["Add", "Cancel", "Rescan"])
    precondition(alert.buttons[0].isEnabled && picker.numberOfItems == 2)
    precondition(picker.titleOfSelectedItem == "Native Instruments: Battery 4 · AU Instrument")
    picker.selectItem(at: 1)
    precondition(picker.titleOfSelectedItem == "Battery 4 · VST3 Instrument")
    alert.layout()
    precondition(!alert.window.isVisible)
    let view = alert.window.contentView!
    precondition(view.bounds.contains(view.convert(picker.bounds, from: picker)))
    for button in alert.buttons {
      precondition(view.bounds.contains(view.convert(button.bounds, from: button)))
    }
    precondition(!alert.window.isVisible)
    print(
      "PASS plugin picker: cached-list explanation, AU/VST3 labels and selection, Rescan, empty-list Add disabled, controls fit; layout checked offscreen without focus or audio"
    )
  }
}
