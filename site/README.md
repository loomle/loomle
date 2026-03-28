# LOOMLE Site

This directory is intended to be published with GitHub Pages from the `main` branch using the `/site` folder.

Recommended Pages settings:

- Branch: `main`
- Folder: `/site`

Recommended custom domain:

- `loomle.ai`

Expected published URLs:

- `https://loomle.ai/`
- `https://loomle.ai/install.sh`
- `https://loomle.ai/install.ps1`

This site is intentionally minimal:

- `index.html` is the install entrypoint page
- `install.sh` and `install.ps1` are the only published site scripts
- they install LOOMLE directly from the release manifest and platform zip
- they do not download a temporary installer binary

Keep the published bootstrap scripts aligned with `packaging/bootstrap/install.sh` and `packaging/bootstrap/install.ps1`.
