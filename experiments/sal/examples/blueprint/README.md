# Blueprint SAL Examples

These examples are the working corpus for the SAL SDK experiment. They are
sketches for parser, formatter, adapter, and agent-usability work; they are not
serialized Blueprint exports.

## Core

Core examples are the parser and formatter conformance target for the current
minimal implementation:

- `01-begin-play-delay-print.sal`
- `08-insert-delay-before-print.patch.sal`
- `10-find-print-nodes.query.sal`
- `12-find-path-from-pin.query.sal`
- `14-find-branch-node.query.sal`
- `16-find-delay-pins.query.sal`
- `17-move-delay.patch.sal`
- `18-find-print-palette.query.sal`
- `19-full-graph.query.sal`
- `20-disconnect-branch-condition.patch.sal`
- `21-find-branch-palette-from-pin.query.sal`
- `22-set-print-and-insert-delay.patch.sal`
- `24-remove-print.patch.sal`
- `25-maintenance-ops.patch.sal`

## Extended

Extended examples cover broader graph and patch shapes and are part of the
default parser and formatter conformance gate. They can also be run directly
with `npm run test:examples:extended` from `experiments/sal`:

- `02-branch-on-health.sal`
- `04-launchpad-overlap.sal`
- `07-spawn-projectile-from-input.sal`
- `09-add-branch-guard.patch.sal`
- `11-find-path-into-branch.query.sal`
- `13-find-branch-context.query.sal`
- `15-find-spawn-node.query.sal`

## Reference

Reference examples are richer design samples. They are audited by
`npm run test:examples:reference`, and the same check is included in the
default `npm test` gate:

- `03-enable-input-near-actor.sal`
- `05-line-trace-print-hit.sal`
- `06-door-overlap-timeline.sal`

## Maintenance

- Prefer numbered examples over unnumbered variants.
- Avoid adding duplicates that only rename aliases or ids.
- Edge lines should name pins explicitly.
- If a file is not ready for default parser conformance, keep it in the
  reference set with a clear reason in its comments or in this README.
