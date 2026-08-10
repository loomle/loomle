import assert from "node:assert/strict";
import {
  objectResultToTextResult,
  parseSalResultText,
  validateObjectResult,
  type ExactQueryResult,
  type ObjectResult,
} from "../../src/index.js";

const graphTarget = {
  alias: "g",
  target: {
    kind: "target" as const,
    domain: "graph" as const,
    asset: "/Game/BP_Door.BP_Door",
    blueprintId: "11111111-1111-1111-1111-111111111111",
    id: "22222222-2222-2222-2222-222222222222",
  },
};
const blueprintTarget = {
  alias: "bp",
  target: {
    kind: "target" as const,
    domain: "blueprint" as const,
    asset: "/Game/BP_Door.BP_Door",
    id: "11111111-1111-1111-1111-111111111111",
  },
};
const actorId = "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
const levelTarget = {
  alias: "arena",
  target: {
    kind: "target" as const,
    domain: "level" as const,
    asset: "/Game/Maps/Arena.Arena",
    type: "/Script/Engine.World",
  },
};
const nativePcgComponentTarget = {
  alias: "forestComponent",
  target: {
    kind: "target" as const,
    domain: "pcg_component" as const,
    asset: "/Game/Maps/Arena.Arena",
    actorId,
    source: "native" as const,
    id: "PCGComponent",
    type: "/Script/PCG.PCGComponent",
  },
};
const instancePcgComponentTarget = {
  alias: "instanceForestComponent",
  target: {
    ...nativePcgComponentTarget.target,
    source: "instance" as const,
  },
};
const secondNativePcgComponentTarget = {
  alias: "secondNativeForestComponent",
  target: {
    ...nativePcgComponentTarget.target,
    id: "PCGComponent_2",
  },
};

const result: ExactQueryResult = {
  targetContext: "exact_target",
  target: graphTarget,
  object: {
    statements: [{
      target: { kind: "local", name: "branch" },
      value: {
        kind: "object",
        semanticTag: "node",
        fields: { id: "N1", title: "Branch" },
      },
    }],
  },
  diagnostics: [],
  page: { next: "cursor:2" },
};

const converted = await objectResultToTextResult(result);
assert.deepEqual(converted.diagnostics, []);
assert.equal(
  converted.text,
  `result exact_target
target g = target {domain: graph, asset: "/Game/BP_Door.BP_Door", blueprintId: "11111111-1111-1111-1111-111111111111", id: "22222222-2222-2222-2222-222222222222"}
objects
branch = node {id: "N1", title: "Branch"}`,
);
assert.equal(converted.page?.next, "cursor:2");
assert.equal(converted.targetContext, "exact_target");
assert.deepEqual(converted.target, graphTarget);
assert.deepEqual(parseSalResultText(converted.text ?? "").diagnostics, []);
console.log("[PASS] public result conversion emits the complete Result Text envelope");

const mutation = {
  targetContext: "exact_target" as const,
  target: graphTarget,
  diagnostics: [],
  isError: false,
  dryRun: true,
  valid: true,
  applied: false,
  operation: "patch",
  planned: { operations: 1 },
};
const convertedMutation = await objectResultToTextResult(mutation);
assert.deepEqual(convertedMutation.diagnostics, []);
assert.match(convertedMutation.text ?? "", /no_objects$/);
assert.equal(convertedMutation.dryRun, true);
assert.equal(convertedMutation.applied, false);
assert.deepEqual(convertedMutation.planned, { operations: 1 });
console.log("[PASS] public result conversion preserves mutation execution fields");

const validRelated: ObjectResult = {
  ...result,
  relatedTargets: [blueprintTarget],
  handoffs: [{
    kind: "target_handoff",
    purpose: "compile owner",
    target: { kind: "local", name: "bp" },
  }],
};
const relatedConversion = await objectResultToTextResult(validRelated);
assert.deepEqual(relatedConversion.diagnostics, []);
assert.match(relatedConversion.text ?? "", /^related bp = target/m);
assert.match(relatedConversion.text ?? "", /^handoff "compile owner" to bp/m);

const pcgComponentRelatedResult: ExactQueryResult = {
  targetContext: "exact_target",
  target: levelTarget,
  relatedTargets: [nativePcgComponentTarget],
  handoffs: [{
    kind: "target_handoff",
    purpose: "inspect generated points",
    target: { kind: "local", name: "forestComponent" },
  }],
  object: {
    statements: [{
      target: { kind: "local", name: "componentSettings" },
      value: {
        kind: "scoped_stable_ref",
        target: { kind: "local", name: "forestComponent" },
        reference: { kind: "stable_ref", identityPath: ["Settings", "Seed"] },
      },
    }],
  },
  diagnostics: [],
};
assert.equal(await validateObjectResult(pcgComponentRelatedResult), undefined);
const pcgComponentRelatedConversion = await objectResultToTextResult(pcgComponentRelatedResult);
assert.deepEqual(pcgComponentRelatedConversion.diagnostics, []);
assert.match(
  pcgComponentRelatedConversion.text ?? "",
  /^related forestComponent = target \{domain: pcg_component,/m,
);
assert.match(
  pcgComponentRelatedConversion.text ?? "",
  /^handoff "inspect generated points" to forestComponent$/m,
);
const parsedPcgComponentRelated = parseSalResultText(pcgComponentRelatedConversion.text ?? "");
assert.deepEqual(parsedPcgComponentRelated.diagnostics, []);
assert.equal(parsedPcgComponentRelated.result?.targetContext, "exact_target");
assert.deepEqual(
  parsedPcgComponentRelated.result && "relatedTargets" in parsedPcgComponentRelated.result
    ? parsedPcgComponentRelated.result.relatedTargets
    : undefined,
  pcgComponentRelatedResult.relatedTargets,
);
assert.deepEqual(parsedPcgComponentRelated.result?.handoffs, pcgComponentRelatedResult.handoffs);
assert.deepEqual(parsedPcgComponentRelated.result?.object, pcgComponentRelatedResult.object);

const duplicatePcgComponentTarget: ExactQueryResult = {
  targetContext: "exact_target",
  target: levelTarget,
  relatedTargets: [
    nativePcgComponentTarget,
    {
      alias: "duplicateForestComponent",
      target: { ...nativePcgComponentTarget.target },
    },
  ],
  handoffs: [
    {
      kind: "target_handoff",
      purpose: "inspect native component",
      target: { kind: "local", name: "forestComponent" },
    },
    {
      kind: "target_handoff",
      purpose: "inspect duplicate component",
      target: { kind: "local", name: "duplicateForestComponent" },
    },
  ],
  diagnostics: [],
};
assert.equal(
  (await validateObjectResult(duplicatePcgComponentTarget))?.code,
  "language.invalid_result_shape",
);

const distinctPcgComponentTargets: ExactQueryResult = {
  targetContext: "exact_target",
  target: levelTarget,
  relatedTargets: [
    nativePcgComponentTarget,
    instancePcgComponentTarget,
    secondNativePcgComponentTarget,
  ],
  handoffs: [
    {
      kind: "target_handoff",
      purpose: "inspect native component",
      target: { kind: "local", name: "forestComponent" },
    },
    {
      kind: "target_handoff",
      purpose: "inspect instance component",
      target: { kind: "local", name: "instanceForestComponent" },
    },
    {
      kind: "target_handoff",
      purpose: "inspect second native component",
      target: { kind: "local", name: "secondNativeForestComponent" },
    },
  ],
  diagnostics: [],
};
assert.equal(await validateObjectResult(distinctPcgComponentTargets), undefined);

const semanticallyInvalid: unknown[] = [
  {
    ...result,
    relatedTargets: [{ ...blueprintTarget, alias: "g" }],
    handoffs: [{ kind: "target_handoff", purpose: "compile", target: { kind: "local", name: "g" } }],
  },
  {
    ...result,
    relatedTargets: [{ ...graphTarget, alias: "other" }],
    handoffs: [{ kind: "target_handoff", purpose: "inspect", target: { kind: "local", name: "other" } }],
  },
  {
    ...result,
    relatedTargets: [blueprintTarget],
  },
  {
    ...result,
    relatedTargets: [blueprintTarget],
    handoffs: [{ kind: "target_handoff", purpose: "wrong", target: { kind: "local", name: "g" } }],
  },
  {
    ...result,
    object: {
      statements: [{
        target: { kind: "local", name: "foreign" },
        value: {
          kind: "scoped_stable_ref",
          target: { kind: "local", name: "missing" },
          reference: { kind: "stable_ref", identityPath: ["N"] },
        },
      }],
    },
  },
];

for (const invalid of semanticallyInvalid) {
  const convertedInvalid = await objectResultToTextResult(invalid);
  assert.equal(convertedInvalid.diagnostics[0]?.code, "language.invalid_result_shape");
}
console.log("[PASS] Target table aliases, deduplication, handoffs, scopes, and related usage are enforced");

const domainRootWithUnqualifiedRef = await objectResultToTextResult({
  targetContext: "domain_root",
  target: { alias: "catalog", target: { kind: "target", domain: "asset" } },
  object: {
    statements: [{
      target: { kind: "local", name: "item" },
      value: { kind: "stable_ref", identityPath: ["N"] },
    }],
  },
  diagnostics: [],
});
assert.equal(domainRootWithUnqualifiedRef.diagnostics[0]?.code, "language.invalid_result_shape");

const invalidShape = await objectResultToTextResult({ diagnostics: "not-an-array" });
assert.equal(invalidShape.text, "result unresolved_target\nno_objects");
assert.deepEqual(parseSalResultText(invalidShape.text).diagnostics, []);
assert.equal(invalidShape.diagnostics[0]?.code, "language.invalid_result_shape");
console.log("[PASS] public result conversion rejects malformed and context-unsafe RPC values");
