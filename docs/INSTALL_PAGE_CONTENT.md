# LOOMLE Install Page Content

## Purpose

This document turns the install-entrypoint strategy into a concrete page content design for `loomle.ai` or `loomle.ai/i`.

It is intended to be implementation-ready for a landing page or static site.

## Page objective

The page must work for two readers at once:

1. a human who wants an extremely simple action
2. an agent that needs enough detail to install LOOMLE correctly

The page should therefore be:

- visually minimal above the fold
- textually rich in the page body
- stable enough that the visible prompt rarely changes

## Above-the-fold design

### Visible primary line

```text
Install LOOMLE from loomle.ai
```

This is the sentence the human should copy into their coding agent.

### Visible supporting hint

```text
Paste this into your coding agent from the Unreal project root.
```

### Visible controls

- Copy button
- Optional secondary button:
  - `Manual install`

The primary action should remain the copy button.

## Page body requirements

The body must contain enough plain text that an agent can read and follow the installation flow without needing an extra hidden API.

Recommended sections:

### 1. What LOOMLE installs

Suggested copy:

```text
LOOMLE installs two things into an Unreal project:
- Plugins/LoomleBridge
- Loomle/
```

This tells the agent what to expect after installation.

### 2. Agent install instructions

Suggested copy:

```text
If LOOMLE is not yet available on this machine, run the temporary installer bootstrap first.
Then install LOOMLE into the current Unreal project by running:

loomle-installer install --project-root <ProjectRoot>
```

This should be plain text, not only a styled code widget.

### 3. Bootstrap instructions

Suggested copy:

```text
If the machine does not yet have the installer:

macOS:
curl -fsSL https://loomle.ai/install.sh | sh -s -- install --project-root /path/to/MyProject

Linux:
Bootstrap install is not published yet.
Build from source or use a local release bundle for now.

Windows PowerShell:
& ([scriptblock]::Create((irm https://loomle.ai/install.ps1))) install --project-root C:\Path\To\MyProject
```

### 4. Verification

Suggested copy:

```text
After installation, verify the setup:

Loomle/loomle doctor

Expected project layout:
- Plugins/LoomleBridge/
- Loomle/
```

### 5. Agent operating note

Suggested copy:

```text
Agents should treat this page as the canonical install instruction source for LOOMLE.
If LOOMLE is already installed, the same command path should be used for upgrade or repair.
```

## Recommended page order

Use this order:

1. Wordmark / title
2. One-line copyable prompt
3. Small supporting hint
4. What LOOMLE installs
5. Agent install instructions
6. Bootstrap instructions
7. Verification

This keeps the human experience simple without depriving the agent of detail.

## Recommended HTML structure

The exact styling is flexible, but the content should be represented in normal page text.

Suggested structure:

```html
<main>
  <section id="hero">
    <h1>Install LOOMLE from loomle.ai</h1>
    <p>Paste this into your coding agent from the Unreal project root.</p>
    <button>Copy</button>
  </section>

  <section id="what-gets-installed">
    <h2>What LOOMLE installs</h2>
    <p>LOOMLE installs Plugins/LoomleBridge and Loomle/ into your Unreal project.</p>
  </section>

  <section id="agent-install">
    <h2>Agent install instructions</h2>
    <pre><code>loomle install --project-root &lt;ProjectRoot&gt;</code></pre>
  </section>

  <section id="bootstrap">
    <h2>Bootstrap</h2>
    <pre><code>curl -fsSL https://loomle.ai/install.sh | sh</code></pre>
    <pre><code>irm https://loomle.ai/install.ps1 | iex</code></pre>
  </section>

  <section id="verification">
    <h2>Verify</h2>
    <pre><code>loomle doctor</code></pre>
  </section>
</main>
```

## Machine-readable friendliness rules

To keep the page agent-friendly:

- keep key install commands in literal text, not only images
- do not hide the important instructions behind JavaScript-only interactions
- do not put the critical instructions only in screenshots
- keep the install commands present in the initial HTML response if possible

## Minimal copy set

If the page must be extremely simple, the minimum body text should still include:

```text
Install LOOMLE from loomle.ai

Paste this into your coding agent from the Unreal project root.

If loomle is not installed on this machine:
- macOS / Linux: curl -fsSL https://loomle.ai/install.sh | sh
- Windows PowerShell: irm https://loomle.ai/install.ps1 | iex

Then install LOOMLE into the current Unreal project:
- loomle install --project-root <ProjectRoot>

Verify with:
- loomle doctor
```

## Final recommendation

Keep the visible page almost trivial for humans, but make the HTML body explicit enough that an agent can complete the install flow from the same page without guessing.
