# Dynamic calendar font

The 14px, weight 500 firmware font is generated from official [Noto Sans CJK JP](https://github.com/notofonts/noto-cjk/blob/main/Sans/Variable/TTF/NotoSansCJKjp-VF.ttf), under the existing SIL Open Font License in OFL.txt. Other firmware fonts retain the original Noto Sans JP subsets.

Generation requests Latin U+0020–024F, punctuation U+2000–206F, Japanese kana U+3000–30FF, CJK U+4E00–9FFF, and fullwidth punctuation U+FF00–FFEF. This upstream font maps 20,976 basic ideographs; newer U+9FF0–9FFF have no glyph. Supplementary CJK, emoji and other scripts are outside this font's scope.

The CJK bitmap is uncompressed 4bpp (2,034,791 bytes), plus 21,733 glyph descriptors and maps. This avoids enabling LVGL compressed-font support. The source TTF is a development input and is not embedded in firmware. The five shared RGB565 backgrounds total 1,296,000 bytes. Final app partition usage still requires a firmware build after the testing pause ends.

Calendar list labels use the generated font’s actual line_height (27px), rather than the 14px nominal font size. A time label takes one complete line and a title takes two complete lines (54px). The old 18px viewport clipped 9px of each font line; the reported 车/检/解/约/Claude glyphs are present in generated C maps. The calendar creates only two visible event slots and pages through the bounded snapshot count.
