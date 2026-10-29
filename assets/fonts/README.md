# Fonts

Drop `.ttf` files in here and assign them to a **UI Text** object's *Font*
field in the Inspector — or drag the file straight onto that field from the
Assets panel.

The path is the one the Assets panel shows, for example `fonts/Inter-Regular.ttf`.

## Leaving the field blank is fine

A label with no font uses the engine's built-in 8×8 font. It is plain and it
is fixed at one shape, but it needs no file and it always works — so an
interface is readable before anybody has gone looking for a font, and a font
that fails to load degrades to it rather than vanishing.

## What works

TrueType outlines, which is what `.ttf` means and what most `.otf` files also
contain. An OpenType file built from CFF/PostScript curves instead will be
refused, and the Console will say so rather than silently drawing nothing.

Each **size** is baked into its own texture, so using one font at 16px and
32px costs two atlases. That is normal; it is only worth thinking about if a
scene uses a great many distinct sizes.

## Why none are included

Most fonts you already have — the ones installed with Windows or macOS —
cannot be redistributed, so shipping one inside this repository would be a
licensing problem rather than a convenience. Fonts under the SIL Open Font
License (Inter, Roboto, Open Sans, Fira Sans and many others) can be, and
dropping one in here is all that is needed.
