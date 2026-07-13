# Fixed Chain Viewport Design

## Goal

Keep the edit-mode signal chain compact and legible by showing five fixed-width effect tiles at a time, regardless of how many effects are in the preset.

## Layout

- The existing 1240 x 126 black chain region remains the viewport.
- Each effect tile uses one of five equal slots, with the existing 10 px inter-tile gap and 92 px tile height.
- A preset with one to five blocks leaves unused black slots; it never stretches a tile to fill the chain.
- A preset with more than five blocks extends the same row horizontally beyond the viewport.

## Interaction

- The chain viewport becomes horizontally swipe-scrollable when its row exceeds the visible width.
- Existing tap-to-select and drag-to-reorder interactions remain available on every tile.
- Drag placement and its acid-green insertion indicator use the viewport's current horizontal scroll offset, so dropping a block after scrolling inserts it at the visible position.

## Testing

- The LVGL smoke test asserts that a one-block chain uses the fixed five-slot tile width rather than filling the viewport.
- It asserts that an over-five-block chain is horizontally scrollable and that its sixth tile is positioned outside the initial viewport.
- Existing large-chain slot and insertion mapping coverage remains valid, updated for scroll-aware coordinates if necessary.
