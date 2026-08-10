# Agent Channel Packages

This directory derives MCP Registry and Claude distribution candidates plus an
internal Codex compatibility package from the same verified Loomle Client
bundle. It does not maintain a second Client implementation and it does not
publish any store automatically.

Build the Client first, then generate all three candidates:

```sh
npm run build --workspace @loomle/client
npm run assemble:agents
```

The default output is `.tmp/agent-packages/`:

```text
registry/
  loomle-mcp-registry-<version>.mcpb
  server.json
claude/
  loomle-claude-<version>.mcpb
codex-marketplace/
  .agents/plugins/marketplace.json
  plugins/loomle/
codex/
  loomle-codex-marketplace-<version>.zip
build.json
```

The Registry and Claude files are separate MCPB archives because their
publication and update authorities are independent. Their
`server/loomle.cjs` files remain byte-identical. The Codex plugin copies the
same bundle to `mcp/loomle.cjs`. `build.json` records the common Client digest
and rejects generation if any copy differs.

Each MCPB also contains the public privacy policy, support, installation, and
usage guidance in `README.md`, plus the official 512x512 Loomle icon required
for a directory-ready presentation. These files do not alter the shared Client
bytes.

Public candidates set only their distribution identity:

- MCP Registry: `mcp_registry`;
- Claude: `claude`;

The internal Codex package uses `github`. It exists to verify that Loomle can
be packaged for Codex without creating another public version or update
authority.

This identity changes update discovery and user guidance only. The MCP tools,
SAL behavior, Bridge protocol, and Client bytes are shared.

The generated Codex tree is used only for compatibility validation and is not
uploaded to GitHub Releases or promoted to a branch. The generated Registry
`server.json` references the exact versioned GitHub Release MCPB and records
its SHA-256. Claude submission remains a separate human review step.

Run the focused assembler tests with:

```sh
npm run test:agents
```

Generating candidates does not advance the MCP Registry or Claude channel
version. A channel becomes current only after its own promotion or review
succeeds.
