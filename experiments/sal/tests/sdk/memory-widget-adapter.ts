import assert from "node:assert/strict";
import {
  createSal,
  createMemoryWidgetAdapter,
  type CreationEntry,
  type WidgetDocument,
} from "../../src/index.js";

const documents: WidgetDocument[] = [
  {
    alias: "menu",
    asset: "/Game/UI/WBP_Menu.WBP_Menu",
    root: "mainCanvas",
    widgets: [
      {
        alias: "mainCanvas",
        class: "CanvasPanel",
      },
      {
        alias: "stack",
        class: "VerticalBox",
        parent: "mainCanvas",
      },
      {
        alias: "title",
        class: "TextBlock",
        parent: "stack",
        properties: { text: "Main Menu", fontSize: 32 },
      },
      {
        alias: "start",
        class: "Button",
        parent: "stack",
        properties: { text: "Start" },
      },
      {
        alias: "quit",
        class: "Button",
        parent: "stack",
        properties: { text: "Quit" },
      },
    ],
  },
];

const paletteEntries: CreationEntry[] = [
  {
    name: "Button",
    constructor: { kind: "call", callee: "Button", args: {} },
    defaults: { text: "" },
    properties: [{ name: "text", type: "string", default: "", writable: true }],
  },
  {
    name: "InventorySlot",
    class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C",
    label: "Inventory Slot",
    category: "User Created",
  },
  {
    name: "PluginFancy",
    palette: { kind: "palette", id: "widget.palette:plugin-fancy" },
    label: "Plugin Fancy",
    category: "Plugin",
  },
];

const sal = createSal({ adapters: [createMemoryWidgetAdapter({ documents, paletteEntries })] });
const header = `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)`;

const treeResult = await sal.query(`${header}
query w
find tree
`);
assert.equal(treeResult.diagnostics.length, 0);
assert.match(treeResult.text ?? "", /menu = widget\(asset: "\/Game\/UI\/WBP_Menu.WBP_Menu", root: mainCanvas\)/);
assert.match(treeResult.text ?? "", /mainCanvas.stack = VerticalBox\(\)/);
assert.match(treeResult.text ?? "", /stack.start = Button\(text: "Start"\)/);
console.log("[PASS] memory widget adapter returns tree");

const widgetResult = await sal.query(`${header}
query w
find widgets "Start"
where type = Button
`);
assert.equal(widgetResult.diagnostics.length, 0);
assert.match(widgetResult.text ?? "", /stack.start = Button\(text: "Start"\)/);
assert.doesNotMatch(widgetResult.text ?? "", /stack.quit/);
assert.doesNotMatch(widgetResult.text ?? "", /title/);
console.log("[PASS] memory widget adapter filters widgets");

const compactButtonPalette = await sal.query(`${header}
query w
find palette entry "Button"
`);
assert.equal(compactButtonPalette.diagnostics.length, 0);
assert.match(compactButtonPalette.text ?? "", /Button = Button\(\)/);
assert.doesNotMatch(compactButtonPalette.text ?? "", /Button = Button\(text: ""\)/);

const buttonPalette = await sal.query(`${header}
query w
find palette entry "Button"
with defaults
`);
assert.equal(buttonPalette.diagnostics.length, 0);
assert.match(buttonPalette.text ?? "", /Button = Button\(text: ""\)/);
assert.doesNotMatch(buttonPalette.text ?? "", /InventorySlot/);
assert.doesNotMatch(buttonPalette.text ?? "", /Button.text = property/);

const buttonProperties = await sal.query(`${header}
query w
find palette entry "Button"
with properties
`);
assert.equal(buttonProperties.diagnostics.length, 0);
assert.match(buttonProperties.text ?? "", /Button = Button\(\)/);
assert.match(buttonProperties.text ?? "", /Button.text = property\(type: string, default: "", writable: true\)/);

const buttonDefaultsAndProperties = await sal.query(`${header}
query w
find palette entry "Button"
with defaults, properties
`);
assert.equal(buttonDefaultsAndProperties.diagnostics.length, 0);
assert.match(buttonDefaultsAndProperties.text ?? "", /Button = Button\(text: ""\)/);
assert.match(buttonDefaultsAndProperties.text ?? "", /Button.text = property\(type: string, default: "", writable: true\)/);

const classPalette = await sal.query(`${header}
query w
find palette entry "Inventory"
`);
assert.equal(classPalette.diagnostics.length, 0);
assert.match(classPalette.text ?? "", /InventorySlot = widget\(class: "\/Game\/UI\/WBP_InventorySlot.WBP_InventorySlot_C"\)/);

const fallbackPalette = await sal.query(`${header}
query w
find palette entry "Plugin"
`);
assert.equal(fallbackPalette.diagnostics.length, 0);
assert.match(fallbackPalette.text ?? "", /PluginFancy = widget\(palette: "widget.palette:plugin-fancy"\)/);
console.log("[PASS] memory widget adapter returns copyable palette entries");

const missingWidget = await createSal({ adapters: [createMemoryWidgetAdapter({ documents: [] })] }).query(`${header}
query w
find tree
`);
assert.equal(missingWidget.text, undefined);
assert.equal(missingWidget.diagnostics[0]?.code, "widget_not_found");
console.log("[PASS] memory widget adapter reports missing widget document");

const dryRunPatch = await sal.patch(`${header}
patch w dry run

stack.help = Button(text: "Help")
add stack.help
set title.text = "Main Menu"
move help before start
remove quit
`);
assert.equal(dryRunPatch.diagnostics.length, 0);
assert.match(dryRunPatch.text ?? "", /stack.help = Button\(text: "Help"\)/);
assert.match(dryRunPatch.text ?? "", /stack.title = TextBlock\(text: "Main Menu", fontSize: 32\)/);

const dryRunAdapter = createMemoryWidgetAdapter({ documents });
assert.equal(dryRunAdapter.getDocuments()[0].widgets.some((widget) => widget.alias === "help"), false);
console.log("[PASS] memory widget adapter dry run computes without mutating");

const patchAdapter = createMemoryWidgetAdapter({ documents });
const patchSal = createSal({ adapters: [patchAdapter] });
const applyPatch = await patchSal.patch(`${header}
patch w

stack.help = Button(text: "Help")
stack.slot = widget(class: "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C")
stack.fancy = widget(palette: "widget.palette:plugin-fancy")
add stack.help
add stack.slot
add stack.fancy
set title.text = "Main Menu"
move help before start
remove quit
`);
assert.equal(applyPatch.diagnostics.length, 0);
const afterApply = patchAdapter.getDocuments()[0];
assert.equal(afterApply.widgets.some((widget) => widget.alias === "help"), true);
assert.equal(afterApply.widgets.find((widget) => widget.alias === "slot")?.class, "/Game/UI/WBP_InventorySlot.WBP_InventorySlot_C");
assert.equal(afterApply.widgets.find((widget) => widget.alias === "fancy")?.class, "widget.palette:plugin-fancy");
assert.equal(afterApply.widgets.some((widget) => widget.alias === "quit"), false);
assert.equal(afterApply.widgets.find((widget) => widget.alias === "title")?.properties?.text, "Main Menu");
const helpIndex = afterApply.widgets.findIndex((widget) => widget.alias === "help");
const startIndex = afterApply.widgets.findIndex((widget) => widget.alias === "start");
assert.equal(helpIndex >= 0 && startIndex >= 0 && helpIndex < startIndex, true);
console.log("[PASS] memory widget adapter applies tree patch");

const atomicFailure = await patchSal.patch(`${header}
patch w

set title.text = "ShouldNotApply"
remove missing
`);
assert.equal(atomicFailure.text, undefined);
assert.equal(atomicFailure.diagnostics[0]?.code, "unknown_widget");
assert.equal(patchAdapter.getDocuments()[0].widgets.find((widget) => widget.alias === "title")?.properties?.text, "Main Menu");
console.log("[PASS] memory widget adapter reports patch validation diagnostics");
