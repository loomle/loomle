import assert from "node:assert/strict";
import {
  createLgl,
  createMemoryWidgetAdapter,
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

const lgl = createLgl({ adapters: [createMemoryWidgetAdapter({ documents })] });
const header = `widgetAsset = asset(path: "/Game/UI/WBP_Menu.WBP_Menu", type: widget)
w = widget(asset: widgetAsset, root: mainCanvas)`;

const treeResult = await lgl.query(`${header}
query w
find tree
`);
assert.equal(treeResult.diagnostics.length, 0);
assert.match(treeResult.text ?? "", /menu = widget\(asset: "\/Game\/UI\/WBP_Menu.WBP_Menu", root: mainCanvas\)/);
assert.match(treeResult.text ?? "", /mainCanvas.stack = VerticalBox\(\)/);
assert.match(treeResult.text ?? "", /stack.start = Button\(text: "Start"\)/);
console.log("[PASS] memory widget adapter returns tree");

const widgetResult = await lgl.query(`${header}
query w
find widgets "Start"
where type = Button
`);
assert.equal(widgetResult.diagnostics.length, 0);
assert.match(widgetResult.text ?? "", /stack.start = Button\(text: "Start"\)/);
assert.doesNotMatch(widgetResult.text ?? "", /stack.quit/);
assert.doesNotMatch(widgetResult.text ?? "", /title/);
console.log("[PASS] memory widget adapter filters widgets");

const missingWidget = await createLgl({ adapters: [createMemoryWidgetAdapter({ documents: [] })] }).query(`${header}
query w
find tree
`);
assert.equal(missingWidget.text, undefined);
assert.equal(missingWidget.diagnostics[0]?.code, "widget_not_found");
console.log("[PASS] memory widget adapter reports missing widget document");

const dryRunPatch = await lgl.patch(`${header}
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
const patchLgl = createLgl({ adapters: [patchAdapter] });
const applyPatch = await patchLgl.patch(`${header}
patch w

stack.help = Button(text: "Help")
add stack.help
set title.text = "Main Menu"
move help before start
remove quit
`);
assert.equal(applyPatch.diagnostics.length, 0);
const afterApply = patchAdapter.getDocuments()[0];
assert.equal(afterApply.widgets.some((widget) => widget.alias === "help"), true);
assert.equal(afterApply.widgets.some((widget) => widget.alias === "quit"), false);
assert.equal(afterApply.widgets.find((widget) => widget.alias === "title")?.properties?.text, "Main Menu");
const helpIndex = afterApply.widgets.findIndex((widget) => widget.alias === "help");
const startIndex = afterApply.widgets.findIndex((widget) => widget.alias === "start");
assert.equal(helpIndex >= 0 && startIndex >= 0 && helpIndex < startIndex, true);
console.log("[PASS] memory widget adapter applies tree patch");

const atomicFailure = await patchLgl.patch(`${header}
patch w

set title.text = "ShouldNotApply"
remove missing
`);
assert.equal(atomicFailure.text, undefined);
assert.equal(atomicFailure.diagnostics[0]?.code, "unknown_widget");
assert.equal(patchAdapter.getDocuments()[0].widgets.find((widget) => widget.alias === "title")?.properties?.text, "Main Menu");
console.log("[PASS] memory widget adapter reports patch validation diagnostics");
