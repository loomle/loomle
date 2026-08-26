# Cross-Project Asset Migration

## Use only for an explicit cross-project request

UE migration copies packages, normally with their dependency closure, into
another project's Content directory while preserving mounted package-relative
paths. It is not an in-project move and it does not remove the source.

Require an exact destination Content directory and authorization for that
external project. Do not infer the destination from recent projects, a display
name, or a `.uproject` association.

## Account for UE's broad native behavior

The reflected Asset Tools migration path has important constraints:

- before migration it asks UE to save dirty map and content packages across
  the current project, even when scripting suppresses prompts;
- it recursively gathers dependencies unless `ignore_dependencies` is chosen;
- it may include external Actor/object packages associated with a Level;
- engine and plugin content can be filtered or excluded according to mount
  point and plugin origin;
- conflict policy is `skip`, `overwrite`, or `cancel`;
- the Python-exposed operation returns no per-package result;
- if Asset Registry discovery is active, UE may defer work until discovery
  completes, after the initiating Python call has returned.

Because this Skill excludes Level assets, do not migrate a map or OFPA closure
through this workflow.

## Preflight before any write

Use separate read-only calls to establish:

1. the exact source package names and native Classes;
2. an idle Asset Registry;
3. the complete dependency closure and exclusions;
4. whether any unrelated map or content packages are dirty;
5. the exact destination Content directory and its owning project;
6. collisions at destination and the intended conflict policy;
7. sufficient filesystem access and compatible target engine/plugins.

Stop if unrelated packages are dirty. Do not call Save All to make migration
admissible. Present a bounded manifest with counts and explicit truncation;
obtain direction if the dependency closure or destination effects materially
exceed the user's authorized scope.

Use `skip` as the conservative conflict policy. `overwrite` is destructive and
requires explicit authorization for the exact colliding packages. Use
`ignore_dependencies` only when the user requested a source-only copy and the
agent can explain why the target remains valid without the excluded closure.

## Separate invocation from completion

A successful `python.run` proves at most that the reflected migration method
returned without a Python exception. It does not prove which packages copied,
which collisions skipped, whether deferred work completed, or whether the
target project can load the assets.

After invocation:

1. inspect source state without assuming it changed;
2. verify expected destination package files as preliminary evidence;
3. when an authorized target Editor is available, bind it explicitly and use
   Asset SAL to verify exact Asset Paths and native Classes;
4. load or validate representative root assets through the target project's
   native APIs when required;
5. report every package as `verified`, `fileOnly`, `skipped`, `missing`, or
   `unknown`.

Do not claim migration success from logs, a `None`/void return, or destination
files alone. Target Asset Registry discovery and loadability are the strongest
available evidence.
