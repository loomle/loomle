# LOOMLE Site

This directory is a Jekyll documentation site published with GitHub Pages.

Recommended Pages settings:

- Branch: `main`
- Folder: `/site`

Recommended custom domain:

- `loomle.ai`

Expected published URLs:

- `https://loomle.ai/`
- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

This site keeps install scripts at stable root URLs:

- `install.sh` and `install.ps1` are the only published site scripts
- they install LOOMLE globally from the release manifest and platform zip
- they do not download a temporary installer binary
- they do not install into an Unreal project
- project plugin install/update happens later through MCP `project_install`

The documentation pages use the Just the Docs Jekyll theme.

Build locally with Ruby 3.3:

```bash
bundle install
bundle exec jekyll build --destination _site
```

Keep the published bootstrap scripts aligned with `client/install.sh` and `client/install.ps1`.
