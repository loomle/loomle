import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import test from "node:test";
import { agentSkills } from "../src/generated/agent-skills.js";

test("embeds the canonical Agent Skill Markdown files without drift", () => {
  assert.deepEqual(agentSkills.map(({ name }) => name), [
    "debug-unreal-pie-with-python",
    "format-unreal-blueprints",
  ]);
  for (const skill of agentSkills) {
    assert.match(skill.name, /^[a-z0-9]+(?:-[a-z0-9]+)*$/);
    assert.ok(skill.description.length > 0);
    assert.ok(skill.files.length > 0);
    assert.equal(skill.files[0]?.path, "SKILL.md");
    for (const file of skill.files) {
      assert.equal(
        file.text,
        readFileSync(resolve("..", "skills", skill.name, file.path), "utf8")
          .replace(/\r\n?/g, "\n"),
        `${skill.name}/${file.path} drifted from the canonical Skill source`,
      );
    }
  }
});
