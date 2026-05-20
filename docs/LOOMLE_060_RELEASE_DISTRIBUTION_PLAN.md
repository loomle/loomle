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
- Fab-first：Fab 先安装 UE plugin，然后插件帮助用户激活或配置 MCP host
  所需的 CLI。

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
3. 现有 LOOMLE bar 或一个小的 setup/status panel 显示 bridge、CLI、
   MCP host 和 project 状态。
4. 如果 CLI 缺失，panel 帮助激活 bundled CLI payload，或者引导用户使用
   website installer。
5. Fab 或 Epic Launcher 拥有 plugin update ownership。

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

设计偏好是：把 CLI payload 表达为 LOOMLE 自己的一等 runtime payload，而
不是一个看起来无关、被隐藏起来的 executable。是否合法、是否符合 Fab review
预期，还需要进一步确认。

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
winget、npm 和 Fab，应该优先使用显式命令、可复制配置片段，或者 setup panel
按钮。

## 已确认方向

- LOOMLE 应支持多个常见分发 channel。
- 所有 channel 都应该适配同一个 product model，而不是创造各自独立的产品语义。
- Package manager 应该拥有它安装的文件的 update ownership。
- `loomle update` 不应该覆盖 package-manager-owned payload。
- CLI 应继续支持 CLI-first 用户的多项目 plugin install/maintenance。
- Fab-managed plugin update 应由 Fab/Epic Launcher 拥有。
- UE 侧 setup/status UI 应保持小而聚焦，只负责 setup、status 和 next actions。
- Product version 和 protocol compatibility 应该是两个独立概念。

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
- Fab 对 first-party bundled CLI 的确切可接受 package layout 是什么？
- 如果 Fab 拒绝 bundled CLI binaries，Fab plugin 应该下载官方 payload、打开
  website installer，还是两者都支持？
- 用户是否需要把项目从 Fab-managed plugin 显式切换到 CLI-managed plugin？
  如果需要，应该如何表达？
- `0.6.x` CLI 与 bridge version 之间应该给出什么 compatibility guarantee？
- 如果部分 channel 发布延迟，例如 Fab review 晚于 GitHub/npm release，发版流程
  应如何表达和处理？
