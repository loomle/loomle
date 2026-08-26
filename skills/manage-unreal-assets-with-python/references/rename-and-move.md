# Rename and Move Assets Inside a Project

## Treat rename and move as one native operation

In UE, changing only the asset name, only its package path, or both uses the
Asset Rename Manager. Use `EditorAssetLibrary.rename_asset` for a single
ordinary asset or a live-confirmed Asset Tools batch rename. Never rename the
UObject directly and never move `.uasset` files with filesystem APIs.

Do not apply this workflow to Level assets. Do not use a recursive directory
rename until every contained asset and destination has been enumerated and the
user's requested scope covers that complete set.

## Preflight the full effect set

Before invoking rename:

1. resolve the source through Asset SAL and verify its native Class;
2. prove the destination is absent;
3. confirm Asset Registry discovery is complete—the non-dialog native rename
   path fails while assets are still loading;
4. enumerate Asset Registry referencer packages, using loaded confirmation
   only when its extra cost is justified;
5. check source-control/read-only state for the source and known referencers;
6. return the exact old-to-new map and bounded referencer inventory before a
   large or surprising mutation.

A rename request normally authorizes UE's necessary reference updates inside
the stated project. Pause when preflight discovers a materially broader scope,
localized variants, external plugins, read-only packages, or code/config/text
references that cannot be safely rewritten by the ordinary asset path.

## Understand native side effects

The native rename path gathers hard and soft referencers, attempts checkout,
updates writable referring packages, and saves packages it successfully
rewrites. It may keep an ObjectRedirector when a reference cannot be updated,
a collection refers to the asset, or a package remains read-only.

CDO, code, config, text, and localized-variant references may require an Editor
dialog or manual follow-up even when the Python API itself has no dialog in its
name. If interaction appears or the result becomes uncertain, do not retry;
apply the base Skill's state reconstruction workflow.

## Verify both sides after mutation

In a new call and Asset query:

- prove the destination resolves to the expected native Class;
- inspect the old path and classify it as absent, redirector, unexpected
  asset, or unknown;
- re-query referencers and report packages still resolving through the old
  path;
- verify any asset-type-specific state at the new path;
- save only an exact remaining dirty target through its owning structured
  interface.

Do not automatically delete or fix redirectors as part of rename verification.
Redirector cleanup can modify additional referring packages and is a separate,
explicitly scoped mutation.

For a batch, report each mapping independently. A `false`, failed, lost, or
timed-out batch result does not prove that none of the earlier mappings were
applied.
