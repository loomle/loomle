# Loomle Site

This directory is the Jekyll documentation site published at
<https://loomle.ai/> through GitHub Pages.

The site documents the current Loomle 0.7 release. GitHub Releases provides the
latest engine-specific packages, while Fab provides the current
marketplace-approved build. The site publishes documentation only: there are
no website bootstrap scripts, global installers, or standalone Client copies.
The Client ships inside each plugin archive under
`Resources/Loomle/<platform-arch>/loomle(.exe)`.

## Local Development

The site uses Ruby 3.3, matching the Pages workflow. Do not use the system Ruby
included with macOS. On macOS, install the required version once:

```sh
brew install ruby@3.3
```

From the repository root, install the site dependencies:

```sh
npm run site:setup
```

Start the local server with live reload:

```sh
npm run site:serve
```

The preview is available at <http://127.0.0.1:4000/>. The repository command
selects Homebrew Ruby 3.3 automatically, so no global shell `PATH` change is
required.

Build or validate the production output with:

```sh
npm run site:build
npm run site:check
```

The Pages workflow builds this directory directly and deploys `_site` after
relevant changes reach `main`, or when manually dispatched. It does not copy
files from `client/` or construct release artifacts.
