import AppKit
extension InterfaceTests {
  static func browserFixture() -> [String:Any] {
    let plugins = (0..<4).map { i -> [String:Any] in
      let descriptor: [String:Any] = ["name":i<2 ? "Same name" : "Écho \(i)","format":i==0 ? "AU" : "VST3","isInstrument":i==1,"path":"/fixture/\(i).vst3"]
      return descriptor.merging(["descriptor":descriptor,"catalogID":"plugin-\(i)","favorite":i==0,"hidden":i==3,"category":i==2 ? "All categories" : (i==1 ? "Instruments" : "Effects"),"customCategory":i==2 ? "All categories" : ""]){_,new in new}
    }
    return ["plugins":plugins,"libraryRevision":"library:first"]
  }
  static func pluginBrowserChecks() throws {
    let browser = PluginBrowser();browser.update(browserFixture())
    try require(browser.visible.count==3 && !browser.addButton.isEnabled,"Hidden entries are omitted and Add requires a selection")
    browser.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false)
    var chosen:[String:Any]=[:];browser.onChoose={chosen=$0};browser.choose()
    try require(chosen["format"] as? String == "VST3" && chosen["isInstrument"] as? Bool == true && chosen["catalogID"]==nil,"Same-name plugins select the correct clean descriptor")
    browser.search.stringValue="echo";browser.filterChanged();try require(browser.visible.count==1,"Search ignores case and diacritics")
    browser.showHidden.state = .on;browser.filterChanged();try require(browser.visible.count==2,"Hidden entries can be found and restored")
    browser.search.stringValue="";browser.showHidden.state = .off;browser.favoritesOnly.state = .on;browser.filterChanged()
    try require(browser.visible.count==1 && browser.visible[0]["catalogID"] as? String == "plugin-0","Favorites filter")
    browser.favoritesOnly.state = .off;browser.category.selectItem(at:1);browser.filterChanged()
    try require(browser.category.titleOfSelectedItem=="All categories" && browser.visible.count==1,"A user category may share the All categories label without replacing the sentinel")
    browser.category.selectItem(at:0);browser.filterChanged();browser.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    browser.hiddenToggle.state = .on;browser.categoryField.stringValue="Delay"
    var requests=[(String,[String:Any])](),callbacks=[([String:Any])->Void]()
    browser.onRequest={method,params,reply in requests.append((method,params));callbacks.append(reply)}
    browser.savePreferences();browser.savePreferences()
    try require(requests.count==1 && !browser.addButton.isEnabled && requests[0].1["expectedLibraryRevision"] as? String == "library:first" && requests[0].1["expectedRevision"]==nil,"Preference edits use one pinned library request independent of the song")
    browser.table.selectRowIndexes(IndexSet(integer:1),byExtendingSelection:false)
    callbacks[0](["result":["data":["libraryRevision":"library:next","preferences":["favorite":true,"hidden":true,"category":"Delay"]]]])
    try require(browser.plugins[0]["hidden"] as? Bool == true && browser.plugins[1]["hidden"] as? Bool == false && browser.selected?["catalogID"] as? String == "plugin-1","Delayed preference response cannot retarget a changed selection")
    browser.savePreferences();callbacks[1](["error":["message":"Plugin library changed"]])
    try require(browser.status.stringValue.contains("Reload") && browser.libraryRevision=="library:next","Stale library responses preserve preferences and offer reload")
    var unavailable=browserFixture();unavailable["libraryRevision"]="";unavailable["warning"]="Saved preferences unavailable"
    browser.update(unavailable);browser.table.selectRowIndexes(IndexSet(integer:0),byExtendingSelection:false)
    try require(browser.addButton.isEnabled && !browser.saveButton.isEnabled && browser.status.stringValue.contains("unavailable"),"Unavailable preferences keep plugin addition usable and disable preference writes")
    let large = PluginBrowser()
    let template = (browserFixture()["plugins"] as! [[String:Any]])[0]
    let many = (0..<4096).map { i -> [String:Any] in var entry=template;entry["catalogID"]="large-\(i)";entry["name"]="Unique \(i)";return entry }
    let start=CFAbsoluteTimeGetCurrent()
    large.update(["plugins":many,"libraryRevision":"large"])
    try require(large.visible.count==4096 && large.table.view(atColumn:0,row:4095,makeIfNecessary:false)==nil,"Large browser catalogs retain virtualized offscreen rows")
    large.search.stringValue="Unique 4095";large.filterChanged()
    try require(large.visible.count==1 && large.visible[0]["catalogID"] as? String == "large-4095","Search reaches the final catalog entry")
    print(String(format:"Plugin browser 4096-entry load and search: %.2f ms (offscreen diagnostic, not a presentation gate)",(CFAbsoluteTimeGetCurrent()-start)*1000))
    let builtins=PluginBrowser(builtInOnly:true);var load:[String:Any]=[:]
    builtins.onRequest={_,params,_ in load=params};builtins.load()
    try require(load["format"] as? String == "Built-in" && load["includeHidden"] as? Bool == true,"Built-in browser loads no external inventory")
  }
}
