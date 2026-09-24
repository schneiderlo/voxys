# DM Sans

Source: [Google Fonts DM Sans](https://github.com/google/fonts/tree/main/ofl/dmsans), `DMSans[opsz,wght].ttf`, downloaded 2026-09-24. Licensed under the accompanying SIL Open Font License. Only line endings and trailing whitespace in the license were normalized.

The checked-in WOFF2 is a Latin subset retaining variable weight, optical size and layout features. It was produced with FontTools and Brotli:

```sh
python3 -m fontTools.subset 'DMSans[opsz,wght].ttf' \
  --output-file=dm-sans-latin.woff2 --flavor=woff2 \
  --unicodes='U+0020-007E,U+00A0-00FF,U+2000-206F,U+2190-2193,U+2212' \
  --layout-features='*'
```

Ordinary UI builds only copy the checked-in font. They require neither Python nor network font access. Attribution is included in the generated `web/build_ui_licenses.txt`.
