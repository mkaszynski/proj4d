# Texture provenance

`blocks/` contains the public-safe 16x16 block and item source tiles from the
pinned Hypercraft `d830abc` snapshot. The executable uses Hypercraft's matching
native deterministic atlas generator, so missing optional source PNGs and asset
search paths cannot make a released binary render differently.

The five files in `items/` replace Hypercraft's generic tool icons with original
pickaxes. Their source concept was generated with OpenAI image generation using
this prompt:

> Create exactly five classic pixel-art pickaxe item icons in one horizontal
> row, with identical silhouettes and wood, stone, tin, copper, and iron heads;
> transparent background, no text, hard pixels, and limited palettes suitable
> for reduction to 16x16.

The concept cells were cropped, palette-reduced, alpha-thresholded, and reduced
to exact 16x16 PNGs. Each adjacent `.rgba` file is the PNG's decoded 1,024-byte
RGBA payload; CMake embeds those bytes directly so the runtime needs no PNG
library or filesystem asset lookup.
