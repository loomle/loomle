import assert from "node:assert/strict";
import {
  createSal,
  createMemoryBlueprintAdapter,
  type Blueprint,
} from "../../src/index.js";

const blueprints: Blueprint[] = [
  {
    alias: "door",
    asset: "/Game/BP_Door.BP_Door",
    parent: "/Script/Engine.Actor",
    members: [
      {
        kind: "variable",
        name: "Health",
        type: "float",
        default: 100,
        category: "Stats",
      },
      {
        kind: "function",
        name: "OpenDoor",
        inputs: { speed: "float" },
        outputs: { success: "bool" },
        pure: false,
      },
    ],
    components: [
      {
        name: "Root",
        class: "/Script/Engine.SceneComponent",
        parent: null,
      },
      {
        name: "Mesh",
        class: "/Script/Engine.StaticMeshComponent",
        parent: "Root",
      },
      {
        name: "Trigger",
        class: "/Script/Engine.BoxComponent",
        parent: "Root",
        properties: { boxExtent: [100, 100, 200] },
      },
    ],
  },
];

const sal = createSal({ adapters: [createMemoryBlueprintAdapter({ blueprints })] });
const header = `bpAsset = asset(path: "/Game/BP_Door.BP_Door", type: blueprint)
bp = blueprint(asset: bpAsset)`;

const memberResult = await sal.query(`${header}
query bp
find members "Health"
where kind = variable
page limit 10
`);
assert.equal(memberResult.diagnostics.length, 0);
assert.match(memberResult.text ?? "", /door = blueprint\(asset: "\/Game\/BP_Door.BP_Door"/);
assert.match(memberResult.text ?? "", /door.Health = variable\(type: float, default: 100, category: "Stats"\)/);
assert.doesNotMatch(memberResult.text ?? "", /OpenDoor/);
console.log("[PASS] memory blueprint adapter filters members");

const componentResult = await sal.query(`${header}
query bp
find components "Trigger"
where parent = Root
`);
assert.equal(componentResult.diagnostics.length, 0);
assert.match(componentResult.text ?? "", /Root.Trigger = component\(class: "\/Script\/Engine.BoxComponent", boxExtent: \[100, 100, 200\]\)/);
assert.doesNotMatch(componentResult.text ?? "", /Root = component/);
console.log("[PASS] memory blueprint adapter filters components");

const missingBlueprint = await createSal({ adapters: [createMemoryBlueprintAdapter({ blueprints: [] })] }).query(`${header}
query bp
find members
`);
assert.equal(missingBlueprint.text, undefined);
assert.equal(missingBlueprint.diagnostics[0]?.code, "blueprint_not_found");
console.log("[PASS] memory blueprint adapter reports missing blueprint");

const dryRunPatch = await sal.patch(`${header}
patch bp dry run

bp.MaxHealth = variable(type: float, default: 250, category: "Stats")
add bp.MaxHealth
set bp.parent = "/Script/Engine.Character"
remove bp.Health
`);
assert.equal(dryRunPatch.diagnostics.length, 0);
assert.match(dryRunPatch.text ?? "", /door = blueprint\(asset: "\/Game\/BP_Door.BP_Door", parent: "\/Script\/Engine.Character"/);
assert.match(dryRunPatch.text ?? "", /door.MaxHealth = variable\(type: float, default: 250, category: "Stats"\)/);

const afterDryRun = createMemoryBlueprintAdapter({ blueprints }).getBlueprints()[0];
assert.equal(afterDryRun.parent, "/Script/Engine.Actor");
assert.equal(afterDryRun.members?.some((member) => member.name === "MaxHealth"), false);
console.log("[PASS] memory blueprint adapter dry run computes without mutating");

const patchAdapter = createMemoryBlueprintAdapter({ blueprints });
const patchSal = createSal({ adapters: [patchAdapter] });
const applyPatch = await patchSal.patch(`${header}
patch bp

bp.MaxHealth = variable(type: float, default: 250, category: "Stats")
add bp.MaxHealth
set bp.parent = "/Script/Engine.Character"
rename bp.Health to OldHealth
move Root.Trigger before Root.Mesh
remove bp.OldHealth
`);
assert.equal(applyPatch.diagnostics.length, 0);
const afterApply = patchAdapter.getBlueprints()[0];
assert.equal(afterApply.parent, "/Script/Engine.Character");
assert.equal(afterApply.members?.some((member) => member.name === "Health"), false);
assert.equal(afterApply.members?.some((member) => member.name === "MaxHealth"), true);
const meshIndex = afterApply.components?.findIndex((component) => component.name === "Mesh") ?? -1;
const triggerIndex = afterApply.components?.findIndex((component) => component.name === "Trigger") ?? -1;
assert.equal(triggerIndex >= 0 && meshIndex >= 0 && triggerIndex < meshIndex, true);
console.log("[PASS] memory blueprint adapter applies member patch");

const atomicFailure = await patchSal.patch(`${header}
patch bp

set bp.parent = "/Script/Engine.Pawn"
remove bp.DoesNotExist
`);
assert.equal(atomicFailure.text, undefined);
assert.equal(atomicFailure.diagnostics[0]?.code, "unknown_blueprint_remove_target");
assert.equal(patchAdapter.getBlueprints()[0].parent, "/Script/Engine.Character");
console.log("[PASS] memory blueprint adapter reports patch validation diagnostics");
