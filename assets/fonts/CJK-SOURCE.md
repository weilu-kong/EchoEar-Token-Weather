# Dynamic calendar font

The 16px, weight 500 calendar firmware font (`sh_font_calendar`) is generated from official [Noto Sans CJK JP](https://github.com/notofonts/noto-cjk/blob/main/Sans/Variable/TTF/NotoSansCJKjp-VF.ttf), under the existing SIL Open Font License in OFL.txt. Other firmware fonts retain the original Noto Sans JP subsets.

Generation requests Latin U+0020–024F, punctuation U+2000–206F, Japanese kana U+3000–30FF, CJK U+4E00–9FFF, and fullwidth punctuation U+FF00–FFEF. This upstream font maps 20,976 basic ideographs; newer U+9FF0–9FFF have no glyph. Supplementary CJK, emoji and other scripts are outside this font's scope.

The CJK bitmap is uncompressed 4bpp, plus 21,733 glyph descriptors and maps. This avoids enabling LVGL compressed-font support. The source TTF is a development input and is not embedded in firmware. The five shared RGB565 backgrounds total 1,296,000 bytes. Current app partition usage is recorded in firmware/readable-text-claude/manifest.json; the build passed with the fixed ESP-IDF v6.1 checkout.

Calendar list labels use the generated font’s actual line_height (31px), rather than the 16px nominal font size. A time label takes one complete line and a title takes two complete lines (62px). The old 18px viewport clipped 9px of each font line; the reported 车/检/解/约/Claude glyphs are present in generated C maps. The calendar creates only two visible event slots and pages through the bounded snapshot count.

Ordinary UI fonts use smaller Noto Sans JP subsets, including the 14px/16px body sizes. The calendar alone uses the full CJK font, so enlarging ordinary labels does not inherit the calendar's tall line metrics.
