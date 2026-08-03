# Fab Listing Media

The canonical upload set for Loomle 0.7.3 is under `v0.7.3/`, in Fab gallery
order. Those six PNGs are snapshots of the approved listing artwork.

The HTML files, shared CSS, Loomle mark, and renderer keep the artwork
reproducible. Files whose names describe earlier concepts (`six-calls`,
`interfaces`, and `verified-workflow`) are retained as design history and are
not part of the 0.7.3 upload set.

Render an HTML source at 1920×1080 with:

```sh
node packaging/fab/media/render-media.cjs \
  packaging/fab/media/fab-01-cover.html \
  /tmp/fab-01-cover.png
```
