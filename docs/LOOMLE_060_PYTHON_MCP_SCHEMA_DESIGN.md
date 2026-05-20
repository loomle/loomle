# LOOMLE 0.6 Python MCP 与 Tool Manifest 设计

## 目标

Python MCP 是 LOOMLE 的 Python 版 MCP server。它可以被 Fab 包内置，也可以
用于开发、调试或其他不方便安装 native binary 的场景。

Fab 是第一阶段最重要的使用场景：用户只安装 Fab 插件后，也应该能在
Claude Code、Codex 或其他 MCP host 中使用 LOOMLE。

但 LOOMLE 不能维护两套工具定义。Rust native CLI 和 Python MCP server
必须共享同一份 tool source of truth。

因此 0.6 应引入一个 JSON tool manifest：

- Rust native CLI 读取它生成 `tools/list` 和 `schema.inspect`。
- Python MCP server 读取它生成同样的 `tools/list` 和 `schema.inspect`。
- 两个 MCP server 都按 manifest 的 dispatch 规则调用同一个 LoomleBridge RPC。
- UE 行为仍然只在 LoomleBridge 中实现。

## 分层

LOOMLE 0.6 的 MCP 相关逻辑应分成三层：

```text
Tool Manifest Layer
  public tool names
  descriptions
  input schemas
  schema.inspect second-level schemas
  dispatch mapping
  native/python availability

MCP Server Layer
  native Rust CLI
  Python MCP server
  MCP protocol handling
  local project/status/setup tools
  Bridge RPC transport

Bridge Layer
  LoomleBridge UE plugin
  UE semantics
  asset/blueprint/material/pcg/widget/editor execution
  runtime RPC dispatch
```

Rust native CLI 和 Python MCP 可以是两种 MCP server，但不能拥有两套 tool
schema。

## Tool Manifest 不是普通 JSON Schema

这里需要的不是一组孤立的 JSON Schema，而是一个 tool manifest。

它至少要描述：

- tool name
- title/description
- availability：`native`、`python`、`preAttach`、`requiresBridge`
- top-level `inputSchema`
- schema.inspect 是否支持
- 二层 operation index
- operation-specific schema/examples/notes/errors
- dispatch target
- 参数 transform
- result transform
- 错误形态

概念示例：

```json
{
  "schemaVersion": 1,
  "product": "loomle",
  "tools": [
    {
      "name": "blueprint.graph.edit",
      "description": "Apply explicit local graph edit commands to a Blueprint graph.",
      "availability": ["native", "python"],
      "requiresBridge": true,
      "inputSchema": { "$ref": "#/$defs/blueprintGraphEditRequest" },
      "schemaInspect": {
        "domain": "blueprint",
        "operations": [
          {
            "name": "addFromPalette",
            "summary": "Execute one selected blueprint.palette entry.",
            "schema": { "$ref": "#/$defs/blueprintGraphEditAddFromPalette" },
            "examples": []
          }
        ]
      },
      "dispatch": {
        "kind": "bridgeRpc",
        "tool": "blueprint.graph.edit",
        "args": "identity",
        "result": "structured"
      }
    }
  ]
}
```

## Manifest 文件布局

建议按 domain 拆分，避免一个超大 JSON 难以维护：

```text
mcp/manifest/
  manifest.json
  defs.json
  tools/
    project.json
    asset.json
    editor.json
    blueprint.json
    material.json
    pcg.json
    widget.json
    diagnostics.json
  schema-inspect/
    blueprint.graph.edit.json
    blueprint.member.edit.json
    blueprint.node.edit.json
    material.graph.edit.json
    pcg.graph.edit.json
    pcg.parameter.edit.json
    widget.tree.edit.json
```

Release 时，这份 manifest 应进入：

- native CLI payload
- Fab plugin `Resources/MCP` 或等价 Python MCP 目录
- docs/API 生成流程

`mcp/manifest` 是源码位置。Fab 包中的 `Resources/MCP/tool-manifest` 应由
packaging/sync 流程复制生成，不应作为手写维护的第二份 manifest。

## Dispatch 设计

Manifest 中的 dispatch 应表达 public tool 到 Bridge RPC 的关系。

最简单的是 identity dispatch：

```json
{
  "kind": "bridgeRpc",
  "tool": "blueprint.graph.edit",
  "args": "identity",
  "result": "structured"
}
```

但当前 Rust CLI 里有不少 public tool 会转发到旧的 Bridge tool 名，或者做轻量
参数/结果整形。例如：

- `widget.tree.inspect` -> `widget.query`
- `widget.tree.edit` -> `widget.mutate`
- `widget.compile` -> `widget.verify`
- `material.graph.inspect` -> `material.query`
- `pcg.compile` -> `pcg.verify`
- `blueprint.compile` -> `blueprint.verify`

因此 manifest 需要支持：

```json
{
  "dispatch": {
    "kind": "bridgeRpc",
    "tool": "widget.query",
    "args": {
      "transform": "widget.tree.inspect.args.v1"
    },
    "result": {
      "transform": "widget.tree.inspect.result.v1"
    }
  }
}
```

## Transform 规则

为了避免 Rust/Python 各写一套复杂逻辑，transform 应尽量收敛。

优先级：

1. 新工具尽量让 Bridge 直接支持 public tool name 和 public args。
2. 现有旧 Bridge tool 通过 manifest 声明简单 transform。
3. 复杂 transform 应逐步移动到 Bridge，或作为共享 data-driven transform 表达。
4. 不应该在 Rust 和 Python 中复制业务含义相同但代码不同的 transform。

0.6 Python MCP 的第一版不应该追求覆盖所有 native CLI 的 project/update
能力。它应优先覆盖 Fab 场景和轻量 Python 使用场景所需的 Bridge tools。

## Python MCP 的能力边界

Python MCP 是轻量入口，不是 native CLI 的完整替代。Fab-first 是它的第一
个重点分发场景。

它应该支持：

- MCP stdio server
- `tools/list`
- `schema.inspect`
- 基础 `loomle.status` 或 `loomle`
- `project.list`
- `project.attach`
- session-level auto attach
- 转发 Blueprint、Material、PCG、Widget、Asset、Editor、Diagnostics 等 Bridge
  tools

它不应该支持：

- `project.install`
- `loomle update`
- CLI-managed 多项目插件维护
- 覆盖 Fab-managed plugin

Python MCP 遇到这些 native-only 能力时，应返回明确错误并引导用户安装
native `loomle` CLI。

当前最小实现状态：

- 已使用官方 Python MCP SDK。
- 已从 `mcp/manifest` 生成 `tools/list`。
- 已由 manifest 驱动 `schema.inspect`。
- 已支持 `loomle.status`、`project.list`、`project.attach`。
- 已实现 Unix socket `rpc.health` client，并让 `loomle` 工具读取 attached
  project 的 Bridge health。
- 已实现 manifest-driven 通用 `bridgeRpc` forwarding，当前支持 `args:
  "identity"` 和少量 manifest 声明的公开语义 transform。
- Python MCP 的公开工具面已覆盖 Rust runtime tools/list 中除 native-only
  `project.install` 之外的全部工具。
- 已接入 manifest-driven dispatch transform，用于表达 Rust public tool 到
  Bridge tool 的轻量映射，例如 `asset.create` 的 kind 分派、
  `blueprint.graph.edit` command -> ops、`material.graph.edit` command -> ops、
  `pcg.graph.edit` command -> ops、`widget.tree.edit` command -> ops。
- 已接入二层 edit schema：`blueprint.graph.edit`、
  `blueprint.member.edit`、`blueprint.node.edit`、`material.graph.edit`、
  `pcg.graph.edit`、`pcg.parameter.edit`、`widget.tree.edit`。顶层 schema 保持
  command/operation envelope，并明确引导 `schema.inspect` 读取二层 schema。
- 二层 operation-specific schema 已从 Rust `schema_inspect.rs` 迁入 manifest，
  包括 Blueprint member/node args、Blueprint/Material/PCG graph command
  schemas、PCG parameter operation args、WidgetTree command schemas。
- Windows named pipe transport 待实现。

## Tool Availability

Manifest 中每个 tool 应声明 availability。

示例：

```json
{
  "name": "project.install",
  "availability": ["native"],
  "unavailableIn": {
    "python": {
      "code": "NATIVE_CLI_REQUIRED",
      "message": "project.install is only available in the native LOOMLE CLI. Fab-managed plugins are updated by Fab/Epic Launcher."
    }
  }
}
```

Python MCP 的 `tools/list` 可以选择：

- 不暴露 native-only tools；或
- 暴露但调用时返回 `NATIVE_CLI_REQUIRED`。

推荐第一版不暴露 native-only tools，减少 agent 误用。

## 多项目附着语义

Fab-first 仍然需要多项目 attach 语义。

原因：

- MCP host 通常只配置一个 `loomle` server。
- Fab 插件安装在 Engine 或 Fab Library 侧，不天然绑定某一个项目。
- 多个 Unreal project 可以同时加载 LoomleBridge。
- agent 需要在同一个 MCP session 中明确选择要操作哪个在线项目。

因此 Python MCP 不应被设计成“只服务当前项目”的 server。它应该复用
native CLI 的 session attach 模型：

```text
MCP host
  starts one loomle Python MCP server
    project.list discovers online runtimes from ~/.loomle/state/runtimes
    project.attach selects one runtime for this MCP session
    bridge tools are forwarded to the selected runtime
```

### Runtime Discovery

LoomleBridge 启动时已经写入：

```text
~/.loomle/state/runtimes/<projectId>.json
~/.loomle/state/projects/<projectId>.json
```

Runtime record 包含：

- `runtimeId`
- `projectId`
- `name`
- `projectRoot`
- `uproject`
- `endpoint`
- `platform`
- `pid`
- `pluginVersion`
- `protocolVersion`

Python MCP 应读取同一套 runtime registry，而不是发明新的发现机制。

### Auto Attach

Python MCP 可以保留 native CLI 的 auto attach 便利语义：

- 如果启动时传入 `--project-root`，它只是一个 attach hint，用于初始化当前 MCP
  session 的默认项目。
- 如果 MCP 启动目录属于某个在线项目，则自动 attach 该项目。
- 如果只有一个在线项目，则可以自动 attach。
- 如果有多个在线项目且无法从 cwd 判断，应保持 unattached，并引导 agent 调用
  `project.list` 和 `project.attach`。

这保证同一个 MCP 配置在单项目和多项目场景下都能工作。

### dev_verify 验收语义

`tools/dev_verify.py` 不应继续把 native Rust CLI 视为“项目专用 MCP server”。
Rust 和 Python MCP 都应被验收为同一类 server runtime：

```text
start MCP server
  tools/list
  project.attach(projectRoot)
  loomle/runtime health
  Bridge smoke/regression tools
```

因此 dev verify 可以提供 `--mcp-server rust|python`，但两种模式都必须通过
`project.attach` 选择项目。`--project-root` 可以继续作为 server 启动 hint 保留，
但不能替代 `project.attach` 成为测试主路径。

### project.list

Python MCP 应暴露 `project.list`，但能力范围比 native CLI 小：

- 支持列出 online/all/offline 项目。
- 对 online 项目返回 endpoint、pluginVersion、protocolVersion、attachable。
- 对 Fab-managed project 标记 `managedBy: "fab"` 或等价字段。
- 不承诺能安装或更新 offline project 的插件。

### project.attach

Python MCP 应暴露 `project.attach`：

- 输入 `projectId` 或 `projectRoot`。
- 只允许 attach online 且 endpoint 可访问的项目。
- 当输入 `projectRoot` 时，应优先匹配 runtime registry；如果 registry 尚未写入但
  `<Project>/Intermediate/loomle.sock` 或平台等价 endpoint 已存在，也可以按
  project-local endpoint 规则 attach。这样 dev verify 和刚启动的 Editor 不会因为
  registry 延迟而误判失败。
- attach 只影响当前 MCP session。
- attach 后所有 Bridge tools 都转发到该 runtime。

### project.install

Python MCP 不应暴露 `project.install`。

如果 agent 需要安装项目插件，应安装 native `loomle` CLI，或让用户通过
Fab/Epic Launcher 安装并启用插件。

这条边界很重要：Python MCP 负责 attach，不负责 ownership mutation。

## schema.inspect

`schema.inspect` 应完全由 manifest 驱动。

顶层 schema 只告诉 agent：

- 支持哪些 domain
- 哪些 tool 有二层 schema
- 如何传 `operation`
- 可选 `include`

二层返回内容从 manifest 中读取：

- operation index
- operation summary
- operation schema
- examples
- notes
- errors

这样 Rust 和 Python 的 `schema.inspect` 响应保持一致。

## Rust Native CLI 的角色变化

Rust native CLI 不再手写公开 tool schema。

它仍然负责：

- global install
- project discovery
- project.attach
- project.install
- update
- doctor
- native MCP server
- Bridge RPC transport

但公开 tool declarations 和 schema.inspect 应来自 manifest。

Rust 里仍可保留 native-only local tools 的实现逻辑，但这些工具的 schema 也应来自
manifest。

## Python MCP 的结构

Python MCP 必须使用官方 Python MCP SDK，不手写 MCP JSON-RPC 协议。

当前设计使用官方 SDK 的 low-level `Server` API，而不是 FastMCP decorator。
原因是 LOOMLE 的工具来自 `mcp/manifest`，需要动态生成 `tools/list` 和统一处理
`tools/call` dispatch。

官方 SDK：

- <https://github.com/modelcontextprotocol/python-sdk>
- <https://py.sdk.modelcontextprotocol.io/>

Fab 包内可放：

```text
LoomleBridge/
  Resources/
    MCP/
      pyproject.toml
      loomle_fab_server.py
      loomle_mcp/
        __init__.py
        manifest.py
        server.py
        bridge_rpc.py
        transforms.py
      tool-manifest/
        manifest.json
        tools/
        schema-inspect/
```

源码位置应是：

```text
mcp/python/
  pyproject.toml
  loomle_mcp_server.py
  loomle_mcp/
    __init__.py
    manifest.py
    server.py
    bridge_rpc.py
    project_registry.py
    transforms.py
```

Fab 包内的 `Resources/MCP` 是 packaging/sync 结果，不是源码维护位置。

MCP host 配置类似：

```json
{
  "mcpServers": {
    "loomle": {
      "command": "uv",
      "args": [
        "--directory",
        "<Project>/Plugins/LoomleBridge/Resources/MCP",
        "run",
        "loomle_fab_server.py"
      ]
    }
  }
}
```

如果用户不想使用 `uv`，也可以提供普通 Python 运行说明，但 Fab-first 推荐 `uv`
来解决依赖和 isolated environment。

## Bridge RPC 要求

为了让 Python 和 Rust 行为一致，Bridge RPC 应保持稳定：

```json
{
  "method": "rpc.invoke",
  "params": {
    "tool": "blueprint.graph.edit",
    "args": {}
  }
}
```

Python MCP 不应该绕过 Bridge RPC 直接调用 UE Python 或编辑资产。UE 行为只有
LoomleBridge 拥有。

## 设计判断

这个方向成立，但有一个重要前提：

单一 JSON manifest 能统一 schema 和 dispatch，但不能神奇地消除已有 Rust 中
所有参数转换代码。

因此 0.6 最优雅的目标应该是：

- tool/schema source of truth 先统一到 manifest
- Python MCP 只实现 manifest interpreter + Bridge RPC client
- Rust 逐步改成从 manifest 读 schema
- 复杂 public-to-Bridge transform 逐步下沉到 Bridge 或数据化

不要在第一步要求 Python 复刻 Rust 的全部 project/update 行为。

## 已确认方向

- 需要 Python MCP server。
- Python MCP server 第一阶段服务 Fab-first 场景，但目录和命名不应绑定 Fab。
- Native `loomle` binary 仍然是官网和完整能力入口。
- Rust 和 Python 不应维护两套 tool schema。
- 需要一份 JSON tool manifest，而不是只有 JSON Schema。
- `schema.inspect` 应由 manifest 驱动。
- Python MCP 不应管理 Fab plugin 更新。
- UE 行为仍只由 LoomleBridge 实现。

## 待讨论问题

- Manifest 顶层 schema 具体如何设计？
- Tool manifest 是否使用单文件，还是 domain 分文件？
- Python MCP 的第一版是否只暴露 Bridge tools，不暴露 `project.*`？
- 哪些现有 Rust transform 必须在 0.6 第一版数据化？
- 哪些 public tool 应改成 Bridge 直接支持，避免 transform？
- Python MCP 是否必须依赖 `uv`，还是同时支持裸 Python？
- Python MCP 如何定位当前项目的 runtime endpoint？
- `tools/list` 是否根据 Bridge online/offline 状态动态裁剪？
- Native-only tool 在 Python 中是隐藏，还是暴露并返回引导错误？
