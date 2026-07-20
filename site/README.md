# Loomle Site

This directory is the Jekyll documentation site published at
<https://loomle.ai/> through GitHub Pages.

Loomle 0.7 will use Fab as its only installation channel. Until the matching
0.7 package is public, Pages deployment is manual so the live 0.6 listing is
not paired with premature 0.7 installation instructions. The site publishes
documentation only: there are no website bootstrap scripts, global installers,
or downloadable Client copies. The Client ships inside the Fab plugin under
`Resources/Loomle/<platform-arch>/loomle(.exe)`.

Build locally with Ruby 3.3:

```sh
bundle install
bundle exec jekyll build --destination _site
```

The Pages workflow builds this directory directly and deploys `_site` only
when manually dispatched; it does not copy files from `client/` or construct
release artifacts.
