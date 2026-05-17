# Mini Browser

`mini_browser.c` is a tiny standalone HTML/CSS viewer for the GLIC80 text
screen. It renders into a virtual page that is 64 text columns wide, so the
visible 16-column screen is a one-quarter-width viewport.

## Controls

- Up/down: scroll one text row.
- Left/right: scroll horizontally by four columns.
- `[A]`: page down.
- `[B]`: page up.
- Joystick center: return to the top-left of the page.
- `[C]`: help screen.

Vertical scroll is intentionally allowed past the end of the document, up to
row 999, so the viewport behaves like a large page/canvas instead of a fixed
screen.

## Loading HTML

The app has an embedded demo page. To load your own HTML without recompiling,
preload this RAM block before starting the program:

| Address | Value |
| --- | --- |
| `0x6000`..`0x6003` | ASCII magic `HTM1` |
| `0x6004` | HTML length low byte |
| `0x6005` | HTML length high byte |
| `0x6006` onward | HTML bytes, maximum 2048 bytes |

The HTML does not need a trailing NUL byte. If the header is absent or the
length is invalid, the embedded demo page is used instead.

You can also edit the `default_html` string in `mini_browser.c` and rebuild:

```sh
./tools/sdcc-z80-bin -o c_programs/bin/apps/mini_browser.bin \
  --asm-out c_programs/asm/apps/mini_browser.asm \
  c_programs/apps/mini_browser.c
```

## Supported Markup

The renderer supports a practical subset:

- Block tags: `html`, `body`, `main`, `section`, `article`, `header`,
  `footer`, `nav`, `div`, `p`, `h1`, `h2`, `h3`, `ul`, `ol`, `li`, `pre`,
  and `blockquote`.
- Inline tags: `span`, `a`, and `code`.
- Empty tags: `br`, `hr`, `img`, `meta`, `link`, and `input`.
- Entities: `&amp;`, `&lt;`, `&gt;`, `&quot;`, and `&nbsp;`.

`img` renders as `[IMG alt]` or `[IMG src]`. Scripts, head content, style text,
metadata, and elements with `display:none` are hidden.

## Supported CSS

CSS can come from inline `style=""` attributes or simple element selectors in a
`<style>` block, for example `p { margin-left: 16px; }`.

Supported properties:

- `display: none`
- `margin-left`, `padding-left`, and `padding`
- `margin-top`, `margin-bottom`, `padding-top`, and `padding-bottom`
- `border` and `border-width`
- `font-weight: bold`
- `text-transform: uppercase`
- `white-space: pre`
- `text-align: center` or `right`

Because the display is monochrome text, CSS colors, real fonts, images, and
external stylesheets are not fetched or painted. JavaScript and network loading
are intentionally out of scope for this hardware-size viewer.
