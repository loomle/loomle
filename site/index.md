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
        <h1>Weave New Worlds with AI Agents.</h1>
        <p class="loom-lede">
          Loomle AI builds SAL, Loomle MCP, and OASIUM—Structured Agent
          Language, an Unreal Engine interface, and an immersive world where
          humans and AI agents create together.
        </p>
        <div class="loom-actions">
          <a class="loom-button loom-button-primary" href="/install.html">
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
            <span class="loom-heading-description">Three core calls. Every supported Unreal object.</span>
          </h2>
          <a class="loom-text-link loom-text-link-dark" href="/calls/">Explore Loomle MCP →</a>
        </div>
        <p>
          SAL gives agents one compact object interface to Unreal Engine
          instead of adding another MCP tool for every capability.
          <code>sal_schema</code>, <code>sal_query</code>, and
          <code>sal_patch</code> carry discovery, reading, and editing across
          every supported Unreal object.
        </p>
      </div>

      <div class="loom-call-grid">
        <a href="/calls/schema.html"><span>01</span><strong>sal_schema</strong><small>Discover the object model, domain guidance, and exact capabilities available in the live Editor.</small></a>
        <a href="/calls/sal.html"><span>02</span><strong>sal_query</strong><small>Read live Unreal state, structure, relationships, execution flow, and precise layout geometry.</small></a>
        <a href="/calls/sal.html"><span>03</span><strong>sal_patch</strong><small>Compose ordered Unreal edits, dry-run the plan, validate it, and apply it as one coherent change.</small></a>
      </div>
    </section>

    <section class="loom-skills" id="skills">
      <div class="loom-section-label">03 / RESIDENT AGENT SKILLS</div>
      <div class="loom-skills-intro">
        <div class="loom-heading-block">
          <h2>
            <span class="loom-heading-title">Agent Skills</span>
            <span class="loom-heading-description">Domain expertise, loaded on demand.</span>
          </h2>
          <a class="loom-text-link" href="/calls/agent-skill.html">Explore Resident Skills →</a>
        </div>
        <p>
          Resident Skills are discovered and loaded through MCP. They teach
          agents how to combine the three core calls with domain-specific
          judgment—from reading exact pin geometry to dry-running, moving, and
          verifying a Blueprint graph. The comparison below uses the same nodes
          and links; only their layout changes.
        </p>
      </div>

      <div class="loom-skill-proofs" aria-label="Blueprint layout before and after">
        <figure class="loom-skill-proof">
          <figcaption><span>BEFORE</span><strong>Syntax-valid, visually loose.</strong></figcaption>
          <div class="loom-skill-proof-frame">
            <img src="/assets/images/blueprint-layout-before.png" alt="Blueprint damage handling branch before applying the layout skill" width="1393" height="776" loading="lazy">
          </div>
        </figure>
        <figure class="loom-skill-proof loom-skill-proof-after">
          <figcaption><span>AFTER</span><strong>Structured by the Blueprint layout skill.</strong></figcaption>
          <div class="loom-skill-proof-frame">
            <img src="/assets/images/blueprint-layout-after.png" alt="The same Blueprint damage handling branch after applying the layout skill" width="1393" height="776" loading="lazy">
          </div>
        </figure>
      </div>
    </section>

    <section class="loom-oasium">
      <div class="loom-oasium-copy">
        <div class="loom-section-label">04 / PLAYERS–AGENTS–WORLDS</div>
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
        <div class="loom-section-label">05 / START CREATING</div>
        <h2>Bring AI agents into your Unreal workflow.</h2>
      </div>
      <div class="loom-start-actions">
        <a class="loom-button loom-button-light" href="/install.html">Download Loomle MCP</a>
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
