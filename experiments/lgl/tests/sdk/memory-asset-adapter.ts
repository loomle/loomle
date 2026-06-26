import assert from "node:assert/strict";
import {
  createLgl,
  createMemoryAssetAdapter,
  type Asset,
} from "../../src/index.js";

const assets: Asset[] = [
  {
    alias: "door",
    path: "/Game/Blueprints/BP_Door.BP_Door",
    type: "blueprint",
    class: "/Script/Engine.Blueprint",
    domains: ["asset", "blueprint"],
    loaded: false,
    registryTags: { ParentClass: "/Script/Engine.Actor" },
    score: 98,
  },
  {
    alias: "doorFrame",
    path: "/Game/Blueprints/BP_DoorFrame.BP_DoorFrame",
    type: "blueprint",
    class: "/Script/Engine.Blueprint",
    domains: ["asset", "blueprint"],
    loaded: false,
    registryTags: { ParentClass: "/Script/Engine.Actor" },
    score: 81,
  },
  {
    alias: "menu",
    path: "/Game/UI/WBP_Menu.WBP_Menu",
    type: "widget",
    class: "/Script/UMG.WidgetBlueprint",
    domains: ["asset", "widget"],
    loaded: true,
    registryTags: { ParentClass: "/Script/UMG.UserWidget" },
    score: 72,
  },
];

const lgl = createLgl({ adapters: [createMemoryAssetAdapter({ assets })] });

const doorQuery = await lgl.query(`query asset
find assets "door"
where root = "/Game" and type = blueprint
with registryTags
order by score desc, path asc
page limit 10
`);
assert.equal(doorQuery.diagnostics.length, 0);
assert.match(doorQuery.text ?? "", /door = asset\(path: "\/Game\/Blueprints\/BP_Door.BP_Door"/);
assert.match(doorQuery.text ?? "", /registryTags: \{ParentClass: "\/Script\/Engine.Actor"\}/);
assert.match(doorQuery.text ?? "", /doorFrame = asset/);
assert.doesNotMatch(doorQuery.text ?? "", /menu = asset/);
console.log("[PASS] memory asset adapter filters and formats assets");

const firstPage = await lgl.query(`query asset
find assets
order by path asc
page limit 1
`);
assert.equal(firstPage.diagnostics.length, 0);
assert.match(firstPage.text ?? "", /door = asset/);
assert.equal(firstPage.page?.next, "offset:1");

const secondPage = await lgl.query(`query asset
find assets
order by path asc
page limit 1
page after "${firstPage.page?.next}"
`);
assert.equal(secondPage.diagnostics.length, 0);
assert.match(secondPage.text ?? "", /doorFrame = asset/);
assert.equal(secondPage.page?.next, "offset:2");
console.log("[PASS] memory asset adapter paginates assets");

const withoutTags = await lgl.query(`query asset
find assets "menu"
`);
assert.equal(withoutTags.diagnostics.length, 0);
assert.match(withoutTags.text ?? "", /menu = asset/);
assert.doesNotMatch(withoutTags.text ?? "", /registryTags/);
console.log("[PASS] memory asset adapter respects registryTags expansion");

const unsupportedDetail = await lgl.query(`query asset
find assets
with pins
`);
assert.equal(unsupportedDetail.text, undefined);
assert.equal(unsupportedDetail.diagnostics[0]?.code, "capability.unsupported_detail");

const unsupportedWhere = await lgl.query(`query asset
find assets
where modifiedTime = 10
`);
assert.equal(unsupportedWhere.text, undefined);
assert.equal(unsupportedWhere.diagnostics[0]?.code, "capability.unsupported_where_field");

const unsupportedOrder = await lgl.query(`query asset
find assets
order by modifiedTime asc
`);
assert.equal(unsupportedOrder.text, undefined);
assert.equal(unsupportedOrder.diagnostics[0]?.code, "capability.unsupported_order_key");
console.log("[PASS] memory asset adapter reports query capability diagnostics");
