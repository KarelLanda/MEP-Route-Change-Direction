# Changelog - Otočit směr TZB trasy (OtocSmerTZB)

Veškeré významné změny v tomto doplňku jsou dokumentovány v tomto souboru.

## [v1.0.0] - 2026-10-29

### Přidané funkce
- **Otočení směru TZB trasy:** Podpora pro Potrubí, Vzduchotechniku i Kabelové žlaby v Archicadu 29 i 30.
- **In-Place reverzace (`ReverseDirection`):** Nativní otočení trasy přímo v paměti bez destruktivního mazání trasy.
- **Zachování T-kusů (`MEP::Branch`):** T-spoje, křížení i návazné kolineární trasy zůstávají plně propojené a nedotčené.
- **Zachování odboček (`TakeOff`):** Odbočky na segmentech zůstávají přesně na svých 3D souřadnicích.
- **Zachování excentrických přechodů a kotevních bodů:** Zrcadlení kotevních bodů (včetně svislých tras) a kompenzace pořadí `PreferredTransitionPlacement`.
- **Nová grafická ikona:** Retina dvouvrstvá vektorová ikona s podporou světlého i tmavého režimu (Dark Mode).

### Kompatibilita
- Windows 10/11 x64 (Archicad 29, Archicad 30)
- macOS ARM64 / Apple Silicon (Archicad 29, Archicad 30)
