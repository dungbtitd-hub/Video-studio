# Bundled fonts

All fonts are licensed under the SIL Open Font License 1.1 (OFL): see the OFL-*.txt file of each family.
They are embedded into the core library (cmake/EmbedFiles.cmake), so text renders the same on every OS.
Every font covers Vietnamese (checked by a core test).

| File | Family | Source |
|------|--------|--------|
| BeVietnamPro-Regular/Bold/ExtraBold.ttf | Be Vietnam Pro | github.com/google/fonts (ofl/bevietnampro) |
| Pacifico-Regular.ttf | Pacifico | github.com/google/fonts (ofl/pacifico) |
| Lobster-Regular.ttf | Lobster | github.com/google/fonts (ofl/lobster) |
| PatrickHand-Regular.ttf | Patrick Hand | github.com/google/fonts (ofl/patrickhand) |
| Bungee-Regular.ttf | Bungee | github.com/google/fonts (ofl/bungee) |

`core/third_party/stb/stb_truetype.h` (public domain / MIT, Sean Barrett) reads the outlines.
