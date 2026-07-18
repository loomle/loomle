# Loomle Release Work

The root `package.json` is the only product-version source on `main`. Change it
without creating a tag, then regenerate and check its derived values:

```sh
npm version <version> --no-git-tag-version
npm run generate:version
npm test
```

`npm run generate:version` updates the generated Client version module and
`LoomleBridge.uplugin` `VersionName`. It does not change the independent Fab
build number in `LoomleBridge.uplugin` `Version`.

The remaining Python bundle, installer, and tag-driven workflow scripts belong
to the frozen 0.6 distribution path. They are not 0.7 release entrypoints and
must not be used from `main`. The complete 0.7 executable and Fab artifact path
will replace them as one release boundary.

## Release Branches

- `0.6` is the maintenance line rooted at `v0.6.24`. It accepts only
  compatible fixes and produces any future `v0.6.x` releases.
- `main` is the `0.7` development line. Development builds use a prerelease
  product version; the release commit uses `0.7.0` and is tagged `v0.7.0`.

Product versions and RPC protocol compatibility remain independent. Change the
protocol version only when compatibility actually changes.
