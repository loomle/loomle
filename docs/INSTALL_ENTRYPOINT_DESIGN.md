# LOOMLE Install Entrypoint Design

## Goal

Design a homepage-driven install flow that works well for both:

- a human opening `loomle.ai`
- an agent receiving a short prompt from that human

The design should stay simple for the user while still giving the agent enough detail to complete installation.

## Core idea

The homepage should be visually minimal for humans, but textually rich enough that an agent can extract installation guidance from the same page.

We do **not** depend on reliably detecting whether the visitor is a human browser or an agent. The same page should work for both.

## Human-facing homepage behavior

The primary visible content should be one short prompt plus a copy button.

Recommended default prompt:

```text
Install LOOMLE from loomle.ai
```

Why this works:

- it is short enough to copy and paste easily
- it does not try to explain the whole product
- it only needs to guide the agent to the homepage
- the real instructions live on the same page below or in machine-readable page content

Alternative acceptable prompt:

```text
Install LOOMLE into this Unreal project from loomle.ai
```

This is slightly more explicit, but the shorter version is preferred if the page itself contains the rest of the guidance.

## Agent-facing content strategy

The page should contain additional install guidance in normal text/HTML, even if it is visually secondary.

That content should explain:

1. what LOOMLE is
2. that the agent should treat `loomle.ai` as the install instruction source
3. how to install into the current Unreal project
4. how to verify the installation
5. what to do on macOS/Linux vs Windows

This lets a human see a simple page while an agent can still read the detailed guidance.

## Recommended homepage structure

### Visible primary section

- Logo / wordmark
- One-line prompt:
  - `Install LOOMLE from loomle.ai`
- Copy button
- Very small hint:
  - `Paste this into your coding agent from the Unreal project root.`

### Secondary page content

This can appear below the fold or in a compact details section, but it should still be present in the page body.

Suggested sections:

#### What LOOMLE installs

- `Plugins/LoomleBridge/`
- `Loomle/`

#### Agent install instructions

- run the temporary `loomle-installer` bootstrap if it is not already available in the current command
- run `loomle-installer install --project-root <ProjectRoot>`
- run `Loomle/loomle doctor`
- use `Loomle/loomle update --apply` when the project is already installed and needs an upgrade

#### Platform notes

- macOS bootstrap path
- Linux currently requires source or local-bundle install
- Windows bootstrap path

#### Verification

- confirm `Plugins/LoomleBridge` exists
- confirm `Loomle/` exists
- run `Loomle/loomle doctor`

## Prompt design rule

The homepage prompt should be intentionally minimal.

It should **not** attempt to encode:

- platform-specific install commands
- long product explanation
- Unreal-specific verification details
- troubleshooting details

Those belong in the detailed instructions on the page.

The prompt only needs to get the agent to the page.

## Why this is better than a long prompt

A longer prompt makes the human copy experience worse and tends to become stale.

A short prompt plus a richer page is better because:

- the human action is trivial
- the instructions can evolve without changing the visible prompt
- the page can carry structured install guidance
- the same entrypoint can improve over time without retraining users

## Bootstrap relationship

This homepage strategy works together with the bootstrap contract.

The page should ultimately guide agents toward:

- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`
- or the project-local `Loomle/loomle update --apply` path once LOOMLE is already installed

But the homepage prompt itself should stay simple and stable.

## Final recommendation

Use this as the visible homepage prompt:

```text
Install LOOMLE from loomle.ai
```

and make the homepage body contain the richer install instructions that the agent can read and follow.
