# Loomle Site

This directory is the Jekyll documentation site published at
<https://loomle.ai/> through GitHub Pages.

The site documents the current Loomle 0.7 release candidate. GitHub Releases is
the active 0.7 installation channel; the public Fab listing still contains
0.6 and is explicitly marked as incompatible with these instructions. The site
publishes documentation only: there are no website bootstrap scripts, global
installers, or standalone Client copies. The Client ships inside each plugin
archive under `Resources/Loomle/<platform-arch>/loomle(.exe)`.

Build locally with Ruby 3.3:

```sh
bundle install
bundle exec jekyll build --destination _site
```

The Pages workflow builds this directory directly and deploys `_site` after
relevant changes reach `main`, or when manually dispatched. It does not copy
files from `client/` or construct release artifacts.
