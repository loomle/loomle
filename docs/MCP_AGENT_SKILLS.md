# MCP Agent Skills

## Intent

Loomle Agent Skills are workflow policy for using the Loomle MCP tools. A
Loomle Skill that depends on those tools must not require a second host-specific
installation into Codex, Claude, Copilot, or another agent's filesystem.

The standalone Client owns discovery, loading, and version coherence. Users
configure the Loomle MCP server once. The connected agent then discovers the
resident Skill metadata through the model-controlled `agent_skill` tool and
loads a matching workflow from that same Client process.

Native Agent Skill directories remain a compatible authoring format and a
transparent package artifact. They are not a required runtime installation
surface.

## Canonical Source And Packaging

The repository-root `skills/` directory is the only authoring source. Each
immediate child is one vendor-neutral Agent Skill directory with a required
`SKILL.md` and optional resources.

The Client build embeds each Skill's `SKILL.md` and Markdown reference files in
its self-contained executable. Fab assembly separately copies the same
canonical tree to:

```text
LoomleBridge/Resources/AgentSkills/<skill-name>/
```

The visible package copy supports inspection and future host integrations. The
embedded catalog is the MCP runtime copy. Generation and packaging tests must
prove both derive from the same canonical files.

Skill content therefore shares the Client product version. It has no separate
install, update, or uninstall lifecycle and cannot remain stale after the MCP
Client is replaced.

## Public MCP Tool

`agent_skill` is a local, read-only, idempotent tool. It does not require a
running Unreal Editor or a bound project and never invokes the Bridge.

The tool description contains the complete resident metadata catalog: each
Skill name and description. This is the always-visible discovery layer used by
the model to decide whether to load a Skill.

### Input

```json
{}
```

Lists the resident Skill names and descriptions.

```json
{
  "name": "format-unreal-blueprints"
}
```

Loads one exact Skill. `name` is closed to the names compiled into the current
Client. Additional properties and unknown or empty names are invalid.

### Output

The list form returns one plain-text catalog block. The exact form returns one
plain-text block per embedded Markdown file, beginning with `SKILL.md` and then
the Skill's reference files in deterministic relative-path order. Every block
names its source-relative file before the unmodified file text.

Skill output is workflow guidance, not SAL Result Text. Errors use the normal
local MCP tool-error shape and never fabricate SAL result context.

## Agent Workflow

1. The MCP host lists Loomle tools and exposes the `agent_skill` metadata
   catalog to the model.
2. When a task matches a resident Skill description, the model calls
   `agent_skill` with that exact name before planning the specialized work.
3. The model follows the returned `SKILL.md` and references using the existing
   Loomle tools named by the Skill.
4. The Skill never grants capabilities. Tool schemas, SAL interfaces, live UE
   state, mutation dry run, and user approval remain authoritative.

The resident catalog currently includes:

- `use-unreal-python`, which owns capability selection, live API discovery,
  idempotent execution, continuation recovery, persistence, and verification
  for every unrestricted Python fallback;
- `debug-unreal-pie-with-python`, which guides permission-aware PIE startup,
  runtime World selection, cross-frame calls, UE-owned shutdown, and cleanup
  after the base Python Skill is loaded;
- `format-unreal-blueprints`, which formats Blueprint K2 Graphs from exact live
  geometry.

MCP prompts and resources may later mirror resident Skills for hosts that offer
dedicated user interfaces. They are optional presentation surfaces, not the
portable automatic-discovery contract: prompts are user-controlled and
resource injection is host-controlled, while `agent_skill` is model-controlled.

## Scripts And Assets

The current MCP loader returns instructions and Markdown references. It does
not send executable scripts to the agent or ask the user to install a runtime.
If a future workflow needs deterministic execution, Loomle should implement
that behavior in a reviewed Client or Bridge capability and let the Skill
orchestrate it. Binary assets and host-specific Skill extensions require a
separate explicit contract before becoming MCP runtime content.
