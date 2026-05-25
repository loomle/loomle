# LOOMLE 0.6 发版与分发规划

## 目标

LOOMLE 0.6 的分发设计应该让不同渠道都符合各自用户习惯，同时不让
这些渠道把产品切碎。

我们希望形成的结构是：

- 一个 LOOMLE product version
- 一个 canonical release payload model
- 多个 channel-specific setup path
- 清晰的 update ownership
- MCP CLI 与 Unreal Engine bridge 之间有明确的 compatibility 判断

本文档只记录设计方向和待讨论问题，不描述具体实现步骤。

## 产品形态

LOOMLE 由两个协作部分组成：

- LOOMLE MCP CLI：由 Claude Code、Codex 和其他 MCP host 启动的命令。
- LoomleBridge：运行在 Unreal Editor 内、向 CLI 暴露 UE 语义的插件。

这两部分是同一个产品。正常情况下它们应该使用同一个 product version
发布，但可以通过不同 channel 安装和更新。

发版模型需要同时支持两种入口：

- CLI-first：website script、npm、Homebrew、winget 或直接 GitHub 下载先
  安装 CLI，然后 CLI 可以安装或维护项目内插件。
- Fab-first：Fab 先安装 UE plugin，然后插件帮助用户配置内置 Python MCP
  server，或者继续使用已经配置好的 native `loomle mcp`。

## Canonical Payload 方向

优先设计方向是：每个平台只有一种 canonical payload 形态。

概念上类似：

```text
loomle-<version>-<platform>/
  manifest.json
  bin/
    loomle(.exe)
  plugin/
    LoomleBridge/
  docs/
```

不同 channel 可以把这个 payload 放到不同位置，但 payload 本身应该通过
`manifest.json` 自我描述。

具体目录名还没有定案。关键点是：所有 channel 都应该适配同一个 product
payload，而不是各自发明一套内部结构。

## Setup 与 Install

`install` 和 `setup` 应该是两个概念。

`install` 指某个 channel 把 LOOMLE 文件放到某个位置：

- GitHub release archive 解压到用户选择的路径。
- website installer 下载到 LOOMLE global install root。
- npm 安装到 npm global package directory。
- Homebrew 安装到 Cellar prefix。
- winget 安装到 Windows package location。
- Fab 安装 UE plugin 到 Engine 或 project plugin location。

`setup` 指 LOOMLE 为当前用户记录或激活这个 payload：

```text
loomle setup activate --payload-root <path> --channel <channel>
```

命令名还没有定案，但概念很重要：即使不同 channel 的安装机制不同，也可以
使用同一套 activation semantics。

## Global State

LOOMLE 应继续维护用户级 global state：

```text
~/.loomle/
  bin/
  install/
    active.json
  versions/
  state/
    projects/
    runtimes/
  locks/
  logs/
```

Windows 使用等价的用户目录路径。

Global state 仍然由 LOOMLE 拥有。Package manager 可以提供 payload，但不
应该重新定义 runtime state、project discovery、MCP host configuration 或
project registry 的语义。

## Channel Ownership

不同 channel 不能互相抢同一批文件的 ownership。

Active install state 至少应该记录：

- active product version
- channel
- payload root
- CLI path
- bridge/plugin path
- owner 或 manager
- recommended update command

Ownership 示例：

| Channel | Owns CLI payload | Owns UE plugin | Expected update path |
| --- | --- | --- | --- |
| Website installer | LOOMLE | LOOMLE project install | `loomle update` |
| Direct GitHub archive | User / LOOMLE | Optional LOOMLE project install | explicit user action |
| npm | npm | LOOMLE project install | `npm install -g ...` |
| Homebrew | Homebrew | LOOMLE project install | `brew upgrade loomle` |
| winget | winget | LOOMLE project install | `winget upgrade ...` |
| Fab | Fab for plugin, LOOMLE/user for CLI activation | Fab | Fab / Epic Launcher |

如果 LOOMLE 是通过 package manager 安装的，`loomle update` 不应该静默覆盖
package-manager-owned payload，而应该提示用户使用对应 channel 的更新方式。

## CLI-Managed Projects

LOOMLE 应该保留现有的 CLI 管理多个项目插件的能力。

这仍然是 CLI-first 安装的正确模型：

```text
global CLI
  owns plugin payload/cache
  project.install copies LoomleBridge into Project/Plugins/LoomleBridge
  loomle update can sync registered offline CLI-managed projects
```

Fab 会增加另一种有效的 plugin source：

```text
Fab / Engine Plugin
  Fab installs or updates LoomleBridge
  the plugin reports runtime state
  the CLI attaches to the project
  the CLI does not overwrite the Fab-managed plugin by default
```

Project record 后续应该区分 plugin source 和 ownership，例如：

```json
{
  "pluginSource": "cliProjectInstall | fabEngine | engine | manualProject",
  "managedBy": "loomle | fab | user",
  "canUpdatePlugin": true
}
```

具体字段名还没有定案。

## Fab 方向

Fab 应被视为 Unreal-native discovery and installation channel，而不是另
一个 zip 镜像。

我们希望的 Fab 体验是：

1. 用户从 Fab 安装 LoomleBridge。
2. 在 Unreal project 中启用插件。
3. 现有 LOOMLE bar 升级为小的 setup/status panel，显示 bridge、MCP host、
   project registry 和 plugin ownership 状态。
4. 如果检测到 native `loomle mcp` 已配置，panel 默认保持 native，不再配置
   Fab Python MCP。
5. 如果没有 native 配置，但检测到 Codex/Claude config，用户打开 panel 时自动
   backup 并 merge Fab Python MCP entry。
6. 如果无法检测到 config，panel 只提供 copy setup prompt 和 setup docs 链接，
   不展示大段配置片段。
7. Fab 或 Epic Launcher 拥有 plugin update ownership。

具体 Fab package layout 仍未定案。

候选形态：

```text
LoomleBridge/
  Source/
  Config/
  Resources/
  Loomle/
    manifest.json
    bin/
```

或：

```text
LoomleBridge/
  Source/
  Config/
  Payload/
    manifest.json
    bin/
```

或：

```text
LoomleBridge/
  Source/
    LoomleBridge/
    ThirdParty/
      LoomlePayload/
```

这条 native CLI payload 路线暂时不作为 0.6 Fab 第一版目标。当前更稳的方向
是：Fab package 内置 Python MCP server，而不是 native CLI payload。native
CLI 仍然是官网/完整能力入口；Fab Python MCP 是轻量 MCP runtime，读取同一套
`~/.loomle/state` registry 并转发到同一个 Bridge RPC。

## Ownership Compatibility

MCP server ownership 和 plugin ownership 可以不同，但不能互相抢更新权。

规则：

- Fab setup 遇到 native MCP：保持 native，提示 native `loomle mcp` 可以连接
  Fab plugin，不再配置 Fab Python MCP。
- Native `project.install` 遇到 Fab/Engine-managed plugin：跳过 plugin copy，
  返回清晰的 skipped 状态；native MCP 仍可通过 `project.attach` 使用它。
- 只有 native/project-local plugin 才由 native `project.install` 和 update sync
  维护。
- Fab/Engine plugin 更新由 Fab/Epic Launcher 维护。

Bridge runtime registry 必须写入真实 plugin path 和 ownership：

```json
{
  "pluginPath": "<actual plugin base dir>",
  "pluginInstallScope": "project|engine",
  "pluginManagedBy": "native|fab|external|unknown"
}
```

## Unified Versioning

LOOMLE 应继续发布一个 product version：

```text
v0.6.0
```

这个版本应该贯穿：

- GitHub Release
- website installer manifest
- npm package
- Homebrew formula 或 tap
- winget manifest
- Fab package

GitHub Release 应保持为 built artifacts 和 checksums 的 canonical source
of truth，即使其他 channel 会发布自己的 metadata。

我们希望各 channel 尽量同步，但不能假设它们会瞬间同步。Fab review、winget
PR review、Homebrew publishing 和 npm publishing 都可能有不同延迟。

Tag release 的自动产物应包括：

- `loomle-darwin.zip`
- `loomle-windows.zip`
- `loomle-manifest-darwin.json`
- `loomle-manifest-windows.json`
- `loomle-manifest.json`
- `loomle-fab-plugin.zip`

`loomle-latest` 仍然只指向 website/native installer 使用的 manifest 与 bundle。
Fab 包上传到同一个 GitHub tag release，作为 Fab submission/review 的输入，但不
进入 `loomle-latest`。如果 Fab review 发现需要改代码，应该发布下一个 patch
version，而不是修改同一个 version 的语义。

## Compatibility Versioning

Product version 和 protocol compatibility 应该分开。

Product version 回答：

```text
Which LOOMLE release is this?
```

Protocol version 回答：

```text
Can this CLI safely talk to this LoomleBridge runtime?
```

这很重要，因为不同 channel 可能短暂不同步：

```text
CLI version: 0.6.1
Bridge version: 0.6.0
Compatibility: OK
```

或：

```text
CLI version: 0.6.2
Bridge version: 0.6.0
Compatibility: update required
```

`loomle doctor`、`loomle.status` 和 project status 应该根据 ownership 解释
正确的更新路径。

## MCP Host Configuration

MCP host configuration 应该变得显式，并且容易检查。

潜在用户命令：

```bash
loomle setup configure codex
loomle setup configure claude
loomle setup print-config
loomle doctor
```

Website install 可以继续提供自动配置。更受控的 channel，例如 Homebrew、
winget、npm 和 Fab，应该优先使用显式命令、setup prompt，或者由 setup panel
在安全条件满足时自动完成。

## Setup Status Model

Fab setup panel 应先建立一个清晰的 `setup.status` 契约。MCP 工具侧仍保持
read-only status 加显式 `setup.configure`；UE panel 侧可以把“打开 setup panel”
视为用户发起的 setup action。

`setup.status` 负责回答：

- 当前 Bridge 是否 ready，以及 runtime/project registry 是否写入。
- 当前插件真实路径、版本、install scope 和 ownership。
- native `loomle` 是否存在、版本是什么、是否已经被 Codex/Claude 使用。
- Fab Python MCP 是否存在，以及推荐的 MCP host config entry。
- Codex/Claude 配置文件是否可检测，已有 `loomle` entry 属于 native、Fab、
  manual 还是 unknown。
- 当前推荐动作是 keep native、configure Fab Python、show manual config、
  fix bridge，还是 no action。

边界：

- MCP 侧 `setup.status` 必须只读，不能写用户配置。
- MCP 侧写 Codex/Claude 配置必须通过单独 `setup.configure` action。
- UE panel 侧不再提供 `Connect Codex` / `Connect Claude` 按钮；检测到 host 且
  安全时直接配置，检测不到时只提供 setup prompt 和文档链接。
- 自动写配置只能在 host 和 config path 明确时发生。
- 写配置前必须 backup，写入时只 merge LOOMLE entry，不能删除其他 MCP server。
- 如果检测到 native `loomle mcp`，Fab 默认建议保留 native，不主动替换成
  Fab Python MCP。

## Setup Configure Model

`setup.configure` 是 `setup.status` 之后的显式 mutation。它只做 MCP host
配置，不安装插件，不更新 CLI，不启动 Unreal。

输入应保持很小：

```json
{
  "host": "codex|claude",
  "server": "auto|fabPython|native"
}
```

默认 `server=auto`。

选择规则：

- 如果 native `loomle mcp` 已配置，保持 native，返回 `NATIVE_CONFIGURED` 或
  no-op 结果，不替换成 Fab Python。
- 如果没有 native 配置，且 Fab Python MCP 可用，选择 Fab Python。
- 如果没有 Fab Python MCP，但 native CLI 可用，选择 native。
- 如果 host config path、Claude CLI 或目标 server 都不明确，返回 manual
  config snippet，不写文件。

写入规则：

- 写之前重新读取 `setup.status`。
- 只有 `hosts[].canAutoConfigure=true` 时才写。
- 第一版不提供 `force` 或 overwrite。
- 已存在任何 `loomle` entry 时不覆盖。
- 写之前在原文件旁创建 timestamped backup。
- 只 merge `loomle` MCP server entry，不删除其他 server 或用户设置。
- 返回 `configured`、`host`、`serverOwner`、`configPath`、`backupPath`、
  `changed` 和 `message`。

Host-specific 行为：

- Codex：写 `~/.codex/config.toml` 的 `[mcp_servers.loomle]` entry。
- Claude native：第一版返回
  `claude mcp add --scope user loomle -- <loomle> mcp` 手动命令，不在 MCP
  进程中直接运行外部 CLI。
- Claude Fab Python：只有 Desktop JSON 路径明确时才 merge JSON，否则返回
  setup prompt 和文档链接。

## Unreal Setup Panel

UE 侧第一版不做大型 dock tab，而是升级现有 LOOMLE toolbar badge。

行为：

- toolbar badge 继续显示 Ready、Starting、PIE、Degraded、Offline。
- 点击 badge 打开一个小 popup panel。
- panel 主文案只展示一句 Bridge 状态和一句 MCP/AI client 状态；Plugin、
  endpoint、config path、last activity 等诊断信息只放在 `Advanced details`。
- panel 打开时，如果检测到 Codex/Claude 且不存在 `loomle` entry，就自动 backup
  并 merge 推荐 MCP entry。
- 如果已有 native 或其他 `loomle` entry，panel 保持现有配置，不替换为 Fab
  Python MCP。
- panel 最多只显示一个 action：`Copy Setup Prompt`。它只在完全检测不到
  Codex/Claude MCP setup/config 时出现，用来服务初始化。只要检测到任何 host
  config 或已有 `loomle` entry，panel 就只显示状态，不再提示用户复制 prompt。
- panel 必须区分三个概念：MCP host config 已存在、MCP host 当前是否真的连接、
  最近一次 tool activity。配置存在不等于当前连接在线。
- 当前连接状态只来自 Bridge active connection count。最近一次 `rpc.health` 或
  tool 调用只能作为 last activity 展示，不能被称为 connected。
- 主文案避免解释 MCP host 生命周期细节；某些 MCP host 按需启动和回收 MCP
  server 进程时，panel 只显示 setup found + not connected，last activity 留在
  advanced details。

原因：

- Fab 用户需要在 Unreal 内看到“现在是什么状态、下一步做什么”。
- MCP host 配置属于用户全局配置，所以自动写入必须受限：只在用户打开 panel 后、
  host 明确、路径明确、无已有 `loomle` entry、可 backup 时发生。
- 这样 UE panel、native MCP、Python MCP 三者可以共享同一套语义，但不强行共享
  同一段 UI/配置写入代码。

## 已确认方向

- LOOMLE 应支持多个常见分发 channel。
- 所有 channel 都应该适配同一个 product model，而不是创造各自独立的产品语义。
- Package manager 应该拥有它安装的文件的 update ownership。
- `loomle update` 不应该覆盖 package-manager-owned payload。
- CLI 应继续支持 CLI-first 用户的多项目 plugin install/maintenance。
- Fab-managed plugin update 应由 Fab/Epic Launcher 拥有。
- UE 侧 setup/status UI 应保持小而聚焦，只负责 setup、status 和 next actions。
- Product version 和 protocol compatibility 应该是两个独立概念。
- Fab 包必须由 tag workflow 自动生成，不能依赖本地手工打包。

## 待讨论问题

- 最终 canonical payload directory shape 是什么？
- Payload 中应该用 `plugin/`、`plugin-cache/`，还是另一个名字放
  LoomleBridge？
- `setup activate` 应该把 payload 复制进 `~/.loomle`，引用
  package-manager-owned payload，还是两者都支持？
- `active.json` 的精确 schema 应该是什么？
- 0.6 中哪些 channel 应该是 first-class，哪些只作为未来方向写入文档？
- npm 应该内嵌平台 payload、使用 optional platform packages，还是安装时下载
  GitHub release artifacts？
- Homebrew 初期只做官方 tap，还是后续目标进入 homebrew-core？
- winget 初期是否使用 portable zip，还是等小型 signed installer？
- Fab 对内置 Python MCP runtime 和 `Resources/MCP` package layout 的确切
  review 要求是什么？
- native CLI payload 是否值得作为未来 Fab 高级能力重新评估？
- 用户是否需要把项目从 Fab-managed plugin 显式切换到 CLI-managed plugin？
  如果需要，应该如何表达？
- `0.6.x` CLI 与 bridge version 之间应该给出什么 compatibility guarantee？
- npm/Homebrew/winget 何时进入第一版 release automation？
