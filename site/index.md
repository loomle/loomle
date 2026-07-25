---
layout: home
title: Loomle AI
nav_order: 1
description: Loomle AI builds SAL, Loomle MCP, and OASIUM for human–AI creation.
permalink: /
---

<div class="loomle-home">
  <header class="loom-topbar">
    <a class="loom-wordmark" href="/" aria-label="Loomle home">
      <span class="loom-mark" aria-hidden="true"></span>
      <span>LOOMLE</span>
    </a>
    <nav class="loom-nav" aria-label="Primary">
      <a href="/">Home</a>
      <a href="/#sal">SAL</a>
      <a href="/#loomle-mcp">Loomle MCP</a>
      <a href="https://oasium.io/">OASIUM ↗</a>
      <a href="/quickstart.html">Docs</a>
      <a href="https://github.com/loomle/loomle">GitHub ↗</a>
    </nav>
    <details class="loom-mobile-menu">
      <summary aria-label="Toggle navigation">
        <span class="loom-mobile-menu-icon" aria-hidden="true">
          <i></i><i></i><i></i>
        </span>
      </summary>
      <nav class="loom-mobile-menu-nav" aria-label="Mobile">
        <a href="/">Home</a>
        <a href="/#sal">SAL</a>
        <a href="/#loomle-mcp">Loomle MCP</a>
        <a href="https://oasium.io/">OASIUM ↗</a>
        <a href="/quickstart.html">Docs</a>
        <a href="https://github.com/loomle/loomle">GitHub ↗</a>
      </nav>
    </details>
  </header>

  <div class="loom-main">
    <section class="loom-hero">
      <div class="loom-hero-copy">
        <div class="loom-index">
          <span class="loom-index-line"></span>
          HUMANS — AGENTS — WORLDS
        </div>
        <h1>Weave new worlds with AI agents.</h1>
        <p class="loom-lede">
          Loomle AI builds SAL, Loomle MCP, and OASIUM—Structured Agent
          Language, an Unreal Engine interface, and an immersive world where
          humans and AI agents create together.
        </p>
        <div class="loom-actions">
          <a class="loom-button loom-button-primary" href="https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.3">
            Download Loomle MCP
          </a>
          <a class="loom-button" href="/quickstart.html">Read the Docs</a>
        </div>
        <div class="loom-release">
          <span class="loom-status-dot"></span>
          UE 5.7 · Apple Silicon macOS · x64 Windows
        </div>
      </div>

      <div class="loom-machine" role="group" aria-label="Complete SAL Patch Text">
        <div class="loom-machine-head">
          <span>SAL / PATCH TEXT</span>
          <span>DRY RUN</span>
        </div>
        <div class="loom-punch-row" aria-hidden="true">
          <i></i><i class="is-open"></i><i></i><i></i><i class="is-open"></i>
          <i></i><i class="is-open"></i><i></i><i></i><i></i><i class="is-open"></i><i></i>
        </div>
        {% highlight sal %}
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  blueprintId: "7c3f91a4-2d68-4f0e-b517-9a42e6c8d35b",
  id: "e2a56b9d-73c1-48f4-8d20-c6b91a374ef2"
}

patch eventGraph dry run
print = { palette: "returned-palette-entry-id" }
add print @source-node-guid/source-pin-guid -> print.execute
move print to (640, 320)
{% endhighlight %}
        <div class="loom-machine-foot">
          <span><b>01</b> TARGET</span>
          <span><b>02</b> BIND</span>
          <span><b>03</b> ADD / CONNECT</span>
          <span class="is-ready"><b>04</b> PLAN / VALID</span>
        </div>
      </div>
    </section>

    <section class="loom-sal" id="sal">
      <div class="loom-section-label">01 / HUMAN–AGENT–COMPUTER</div>
      <div class="loom-sal-intro">
        <div class="loom-heading-block">
          <h2>
            <span class="loom-heading-title">SAL — Structured Agent Language</span>
            <span class="loom-heading-description">
              A shared text language for human–agent–computer collaboration on
              complex non-text objects.
            </span>
          </h2>
          <a class="loom-text-link" href="/concepts/sal.html">Explore SAL →</a>
        </div>
        <p>
          Structured Agent Language (SAL) expresses object structure,
          relationships, capabilities, and edits as compact, ordered, copyable
          text while preserving native names, values, identities, and
          semantics. Humans and agents can inspect, exchange, and modify the
          same text; computers can validate and execute it. Its compact form
          reduces total token cost across the full agent loop—from discovery
          and reading through reasoning, modification, and verification.
        </p>
      </div>

      <div class="loom-sal-examples">
        <article class="loom-sal-example loom-sal-object">
          <div class="loom-sal-example-head">
            <span>01 / OBJECT TEXT</span>
            <span>STRUCTURE + RELATIONSHIPS</span>
          </div>
          {% highlight sal %}
beginPlay = node {
  id: "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa",
  type: "/Script/BlueprintGraph.K2Node_Event"
}

beginPlay.then -> validate.execute / then -> unlock.execute / then -> open.execute / then -> notify.execute

# excerpt: Pin and intermediate Node bindings omitted
{% endhighlight %}
        </article>

        <article class="loom-sal-example">
          <div class="loom-sal-example-head">
            <span>02 / QUERY TEXT</span>
            <span>DISCOVER + INSPECT</span>
          </div>
          {% highlight sal %}
query eventGraph
exec flow from pin @node-guid/pin-guid depth 8
{% endhighlight %}
        </article>

        <article class="loom-sal-example">
          <div class="loom-sal-example-head">
            <span>03 / PATCH TEXT</span>
            <span>MODIFY + VERIFY</span>
          </div>
          {% highlight sal %}
patch eventGraph dry run
print = { palette: "returned-palette-entry-id" }
add print @source-node-guid/source-pin-guid -> print.execute
move print to (640, 320)
{% endhighlight %}
        </article>
      </div>
    </section>

    <section class="loom-interface" id="loomle-mcp">
      <div class="loom-section-label">02 / UNREAL ENGINE MCP</div>
      <div class="loom-interface-intro">
        <div class="loom-heading-block">
          <h2>
            <span class="loom-heading-title">Loomle MCP</span>
            <span class="loom-heading-description">Six calls. Unreal-scale reach.</span>
          </h2>
          <a class="loom-text-link loom-text-link-dark" href="/calls/">Explore Loomle MCP →</a>
        </div>
        <p>
          Loomle MCP connects agents to the live Unreal Editor through a
          bundled local Client and native Loomle Bridge. Six stable calls
          handle project binding, editor context, discovery, reading, and
          editing, while SAL carries the depth of Unreal without turning every
          capability into another tool.
        </p>
      </div>

      <div class="loom-call-grid">
        <a href="/calls/status.html"><span>01</span><strong>status</strong><small>Inspect the Client, updates, session state, and Loomle Bridge health.</small></a>
        <a href="/calls/project.html"><span>02</span><strong>project</strong><small>Work safely across multiple Unreal projects through explicit, sticky per-session binding.</small></a>
        <a href="/calls/editor-context.html"><span>03</span><strong>editor_context</strong><small>Share the artist’s current Editor focus and selection with an agent, so both can continue creating from the same context.</small></a>
        <a href="/calls/schema.html"><span>04</span><strong>sal_schema</strong><small>Guide agents from broad discovery to exact action through three schema layers: the resident guide, Domain cards, and exact live capabilities discovered with <code>with schema</code>.</small></a>
        <a href="/calls/sal.html"><span>05</span><strong>sal_query</strong><small>SAL Query Text lets agents discover objects and inspect live Unreal state, structure, relationships, and execution flow with precise control over scope, depth, and detail.</small></a>
        <a href="/calls/sal.html"><span>06</span><strong>sal_patch</strong><small>SAL Patch Text lets agents compose complex, interdependent Unreal edits into one ordered batch—ready to dry-run, validate, and apply.</small></a>
      </div>
    </section>

    <section class="loom-oasium">
      <div class="loom-oasium-copy">
        <div class="loom-section-label">03 / PLAYERS–AGENTS–WORLDS</div>
        <h2>
          <span class="loom-heading-title">OASIUM</span>
          <span class="loom-heading-description">
            An immersive social world where players and AI agents explore,
            play, and create together.
          </span>
        </h2>
        <a class="loom-text-link" href="https://oasium.io/" target="_blank" rel="noreferrer">Enter OASIUM ↗</a>
      </div>

      <a class="loom-oasium-visual" href="https://oasium.io/" target="_blank" rel="noreferrer" aria-label="Visit the OASIUM website">
        <img src="/assets/images/oasium-cover.webp" alt="OASIUM immersive social world collage" width="2560" height="1440" loading="lazy">
      </a>
    </section>

    <section class="loom-start">
      <div>
        <div class="loom-section-label">04 / START CREATING</div>
        <h2>Bring AI agents into your Unreal workflow.</h2>
      </div>
      <div class="loom-start-actions">
        <a class="loom-button loom-button-light" href="https://github.com/loomle/loomle/releases/tag/v0.7.0-rc.3">Download Loomle MCP</a>
        <a class="loom-button loom-button-outline-light" href="/quickstart.html">Read the Docs</a>
      </div>
    </section>
  </div>

  <footer class="loom-footer">
    <span>© 2026 LOOMLE AI</span>
    <nav class="loom-footer-products" aria-label="Projects">
      <a href="/#sal">SAL</a>
      <span aria-hidden="true">·</span>
      <a href="/#loomle-mcp">LOOMLE MCP</a>
      <span aria-hidden="true">·</span>
      <a href="https://oasium.io/">OASIUM</a>
    </nav>
    <span>LOOMLE AI / AGENTIC LOOM</span>
  </footer>
</div>

<script>
  document.querySelectorAll(".loom-mobile-menu a").forEach(function (link) {
    link.addEventListener("click", function () {
      link.closest("details").removeAttribute("open");
    });
  });
</script>
