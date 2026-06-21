# Blueprint LGL Examples

These examples are the working corpus for the LGL SDK experiment. They are
sketches for parser, formatter, adapter, and agent-usability work; they are not
serialized Blueprint exports.

## Core

Core examples are the parser and formatter conformance target for the current
minimal implementation:

- `01-begin-play-delay-print.lgl`
- `08-insert-delay-before-print.patch.lgl`
- `10-find-print-nodes.query.lgl`
- `12-find-path-from-pin.query.lgl`
- `14-find-branch-node.query.lgl`
- `16-find-delay-pins.query.lgl`
- `17-move-delay.patch.lgl`
- `18-find-print-palette.query.lgl`
- `19-full-graph.query.lgl`
- `20-disconnect-branch-condition.patch.lgl`
- `22-set-print-and-insert-delay.patch.lgl`
- `24-remove-print.patch.lgl`

## Extended

Extended examples cover broader graph and patch shapes and are part of the
default parser and formatter conformance gate. They can also be run directly
with `npm run test:examples:extended` from `experiments/lgl`:

- `02-branch-on-health.lgl`
- `04-launchpad-overlap.lgl`
- `07-spawn-projectile-from-input.lgl`
- `09-add-branch-guard.patch.lgl`
- `11-find-path-into-branch.query.lgl`
- `13-find-branch-context.query.lgl`
- `15-find-spawn-node.query.lgl`

## Reference

Reference examples are richer design samples. They are audited by
`npm run test:examples:reference`, but they are kept outside the default
conformance gate so the minimal parser can stay focused:

- `03-enable-input-near-actor.lgl`
- `05-line-trace-print-hit.lgl`
- `06-door-overlap-timeline.lgl`

## Maintenance

- Prefer numbered examples over unnumbered variants.
- Avoid adding duplicates that only rename aliases or ids.
- Edge lines should name pins explicitly.
- If a file is not ready for default parser conformance, keep it in the
  reference set with a clear reason in its comments or in this README.
