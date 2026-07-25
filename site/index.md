---
layout: home
title: Loomle
nav_order: 1
description: The operational interface between AI agents and Unreal Engine.
permalink: /
---

<div class="loomle-home">
  <header class="loom-topbar">
    <a class="loom-wordmark" href="/" aria-label="Loomle home">
      <span class="loom-mark" aria-hidden="true"></span>
      <span>LOOMLE</span>
    </a>
    <nav class="loom-nav" aria-label="Home">
      <a href="/quickstart.html">Docs</a>
      <a href="/tools/">Interfaces</a>
      <a href="/workflows/">Workflows</a>
      <a href="https://github.com/loomle/loomle">GitHub ↗</a>
    </nav>
  </header>

  <div class="loom-main">
    <section class="loom-hero">
      <div class="loom-hero-copy">
        <div class="loom-index">
          <span class="loom-index-line"></span>
          ARTIST — AGENT — UNREAL
        </div>
        <h1>Weave new worlds<br>with AI agents.</h1>
        <p class="loom-lede">
          Loomle MCP brings artists and AI agents together in Unreal Engine
          through SAL—a shared language artists can understand, agents can
          author, and Unreal can execute.
        </p>
        <div class="loom-actions">
          <a class="loom-button loom-button-primary" href="https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.3">
            Download 0.7.0-rc.3
          </a>
          <a class="loom-button" href="/quickstart.html">Read the quickstart</a>
        </div>
        <div class="loom-release">
          <span class="loom-status-dot"></span>
          UE 5.7 · Apple Silicon macOS · x64 Windows
        </div>
      </div>

      <div class="loom-machine" role="group" aria-label="Example SAL program">
        <div class="loom-machine-head">
          <span>PROGRAM / BP_DOOR</span>
          <span>DRY RUN</span>
        </div>
        <div class="loom-punch-row" aria-hidden="true">
          <i></i><i class="is-open"></i><i></i><i></i><i class="is-open"></i>
          <i></i><i class="is-open"></i><i></i><i></i><i></i><i class="is-open"></i><i></i>
        </div>
        <pre><code>eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
print = { palette: "returned-palette-entry-id" }
add print</code></pre>
        <div class="loom-machine-foot">
          <span><b>01</b> PARSE</span>
          <span><b>02</b> RESOLVE</span>
          <span><b>03</b> VALIDATE</span>
          <span class="is-ready"><b>04</b> PLAN / VALID</span>
        </div>
      </div>
    </section>

    <section class="loom-principle">
      <div class="loom-section-label">01 / OPERATING PRINCIPLE</div>
      <div class="loom-principle-grid">
        <h2>Read the machine.<br>Author the change.<br>Verify the result.</h2>
        <p>
          A loom turns a stored pattern into repeatable physical work. Loomle
          gives agents the same kind of disciplined operating layer for Unreal:
          explicit targets, reviewable instructions, and results grounded in
          the running Editor.
        </p>
      </div>
    </section>

    <section class="loom-cycle" aria-label="Loomle working cycle">
      <article>
        <div class="loom-step-head"><span>01</span><span>SENSE</span></div>
        <h3>Inspect before acting.</h3>
        <p>
          Begin with editor context or an exact Asset Path. Query only the
          summary, collection, tree, context, or flow the task needs.
        </p>
        <div class="loom-thread" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
      </article>
      <article>
        <div class="loom-step-head"><span>02</span><span>FORM</span></div>
        <h3>Use the pattern Unreal provides.</h3>
        <p>
          Discover exact schema and Palette capabilities in the live target
          context. Stable references keep every follow-up precise.
        </p>
        <div class="loom-thread" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
      </article>
      <article>
        <div class="loom-step-head"><span>03</span><span>APPLY</span></div>
        <h3>Dry-run the real edit path.</h3>
        <p>
          Parse, resolve, validate, and plan before mutation. Apply the same
          authored change, then compile, save, and read back.
        </p>
        <div class="loom-thread" aria-hidden="true"><i></i><i></i><i></i><i></i></div>
      </article>
    </section>

    <section class="loom-interface">
      <div class="loom-section-label">02 / CONTROL SURFACE</div>
      <div class="loom-interface-intro">
        <h2>Six calls.<br>Unreal-scale reach.</h2>
        <p>
          The public surface stays deliberately small. Rich UE behavior lives
          in SAL and in interface cards that agents can discover as needed.
        </p>
      </div>

      <div class="loom-call-grid">
        <a href="/calls/status.html"><span>01</span><strong>status</strong><small>Client, update, session, Bridge</small></a>
        <a href="/calls/project.html"><span>02</span><strong>project</strong><small>Project discovery and binding</small></a>
        <a href="/calls/sal.html"><span>03</span><strong>sal_query</strong><small>Compact, targeted inspection</small></a>
        <a href="/calls/sal.html"><span>04</span><strong>sal_patch</strong><small>Planned and reviewable edits</small></a>
        <a href="/calls/schema.html"><span>05</span><strong>sal_schema</strong><small>Resident guide and interface cards</small></a>
        <a href="/calls/editor-context.html"><span>06</span><strong>editor_context</strong><small>The user’s active UE target</small></a>
      </div>
    </section>

    <section class="loom-native">
      <div class="loom-native-copy">
        <div class="loom-section-label">03 / UE-FAITHFUL BY DESIGN</div>
        <h2>No parallel model between the agent and the Editor.</h2>
        <p>
          Asset Paths, Class Paths, native types, field names, Palette
          capabilities, validation, compiler messages, and editor semantics
          remain Unreal-native.
        </p>
        <a class="loom-text-link" href="/concepts/">Explore the core concepts →</a>
      </div>

      <div class="loom-specimen">
        <div class="loom-specimen-head">
          <span>OUTPUT SPECIMEN</span>
          <span>SAL / RESULT TEXT</span>
        </div>
        <pre><code>result exact_target
target door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}
objects

door = blueprint {
  variables: 3,
  graphs: 2,
  components: 4
}</code></pre>
        <div class="loom-specimen-key">
          <span>CANONICAL TARGET</span>
          <span>ORDERED OBJECT TEXT</span>
          <span>UE-NATIVE IDENTITY</span>
        </div>
      </div>
    </section>

    <section class="loom-start">
      <div>
        <div class="loom-section-label">04 / START THE MACHINE</div>
        <h2>From first connection to a verified Unreal edit.</h2>
      </div>
      <div class="loom-start-actions">
        <a class="loom-button loom-button-light" href="/install.html">Install Loomle</a>
        <a class="loom-button loom-button-outline-light" href="/quickstart.html">Run the quickstart</a>
      </div>
    </section>
  </div>

  <footer class="loom-footer">
    <span>LOOMLE / PROGRAMMABLE LOOM</span>
    <span>Agent-native Unreal Engine tooling</span>
    <span>© 2026</span>
  </footer>
</div>
