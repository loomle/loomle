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
          HUMANS — AGENTS — WORLDS
        </div>
        <h1>Weave new worlds with AI agents.</h1>
        <p class="loom-lede">
          Loomle AI builds SAL, Loomle MCP, and Oasium: the Structured Agent
          Language, an Unreal Engine interface, and an immersive world for
          human–AI creation.
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
        {% highlight sal %}
eventGraph = target {
  domain: graph,
  asset: "/Game/Blueprints/BP_Door.BP_Door",
  id: "22222222-2222-2222-2222-222222222222"
}

patch eventGraph dry run
print = { palette: "returned-palette-entry-id" }
add print
{% endhighlight %}
        <div class="loom-machine-foot">
          <span><b>01</b> PARSE</span>
          <span><b>02</b> RESOLVE</span>
          <span><b>03</b> VALIDATE</span>
          <span class="is-ready"><b>04</b> PLAN / VALID</span>
        </div>
      </div>
    </section>

    <section class="loom-sal">
      <div class="loom-section-label">01 / SAL — STRUCTURED AGENT LANGUAGE</div>
      <div class="loom-sal-intro">
        <h2>
          A shared text language for human–agent–computer collaboration on
          complex non-text objects.
        </h2>
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
delay = { palette: "P_Delay" }
insert @source-node-guid/source-pin-guid -> delay.execute / then -> @target-node-guid/target-pin-guid
{% endhighlight %}
        </article>
      </div>
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
        {% highlight sal %}
result exact_target
target door = target {
  domain: blueprint,
  asset: "/Game/Blueprints/BP_Door.BP_Door"
}
objects

door = blueprint {
  variables: 3,
  graphs: 2,
  components: 4
}
{% endhighlight %}
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
