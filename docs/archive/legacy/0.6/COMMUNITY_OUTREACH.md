# Community Outreach

This file tracks short, repeatable outreach loops. The goal is not generic
promotion; it is to find developers who will try LOOMLE on a real Unreal
project and report what worked or failed.

## Seven-Day Goal

Get one real external user to install or try LOOMLE with an Unreal project and
leave verifiable feedback.

## Daily Loop

1. Pick one narrow audience.
2. Send the smallest relevant pitch.
3. Ask for one concrete action or one concrete objection.
4. Collect replies, silence, install failures, and tool confusion.
5. Fix the highest-friction repo, docs, or product issue.
6. Update the next day's pitch with what was learned.

## Day 1 Audience

Start with 5-10 people who are likely to understand either Unreal automation or
MCP/agent tooling:

- Unreal Engine tool/plugin developers.
- Developers already experimenting with AI coding agents.
- Blueprint, Material, PCG, or UMG workflow-heavy developers.
- MCP server/client builders who can evaluate the interface shape.

## Day 1 Pitch

Short version:

> I’m building LOOMLE: agent-native Unreal Engine tooling through MCP. It lets
> an AI coding agent attach to a live UE project, read editor context, inspect
> Blueprint/Material/PCG/Widget assets, use UE palettes for creation, and apply
> semantic edits instead of guessing internal node classes.
>
> I’m looking for the first Unreal developer willing to try it on a real
> project. The smallest useful test is install -> project_attach -> context ->
> one inspect tool. If it fails or feels unclear, that feedback is exactly what
> I need.
>
> Repo: https://github.com/loomle/loomle
> Quickstart: https://loomle.ai/quickstart.html
> Feedback: https://github.com/loomle/loomle/issues/new?template=first-user-feedback.yml

## One-Question Feedback Prompt

Use this when someone is not ready to install:

> In 30 seconds, can you tell what LOOMLE would help you do in Unreal Engine? If
> not, what phrase or concept is unclear?

## First Trial Task

Ask testers to do only this:

1. Install LOOMLE.
2. Open an Unreal project.
3. Attach the MCP session with `project_list` and `project_attach`.
4. Call `context`.
5. Run one inspect tool for an asset they already have open.

Success is not required. A clear failure report is a useful result.
