---
layout: loomle-shell
title: Blog
description: Technical notes on SAL, Loomle MCP, Unreal Engine, and building better working environments for AI agents.
permalink: /blog/
nav_exclude: true
---

<section class="loom-blog-index-hero">
  <h1>Blog</h1>
</section>

<section class="loom-blog-list" aria-label="Articles">
  {% for post in site.posts %}
    <article class="loom-blog-card">
      <a class="loom-blog-card-cover" href="{{ post.url }}" aria-label="Read {{ post.title }}">
        <img src="{{ post.cover }}" alt="{{ post.cover_alt }}" width="1600" height="900"{% unless forloop.first %} loading="lazy"{% endunless %}>
      </a>
      <div class="loom-blog-card-copy">
        <div class="loom-blog-card-meta">
          <span>{{ post.kicker | default: "ENGINEERING" }}</span>
          <time datetime="{{ post.date | date_to_xmlschema }}">{{ post.date | date: "%B %-d, %Y" }}</time>
        </div>
        <h2><a href="{{ post.url }}">{{ post.title }}</a></h2>
        <p>{{ post.summary }}</p>
        <a class="loom-blog-read" href="{{ post.url }}">Read article →</a>
      </div>
    </article>
  {% endfor %}
</section>
