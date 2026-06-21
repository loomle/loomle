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
