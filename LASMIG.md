# Datafiler till DriveLogger

Byggda ur Trafikverkets oppna data (CC0) med `tools/hamta-trafikverket.py`.

| Fil | Innehall |
|---|---|
| `HASTIGHET.BIN.gz` | Hela Sveriges hastighetsgranser, 14,4 miljoner punkter. Packa upp (dubbelklick pa Mac) och lagg som `DRIVE/HASTIGHET.BIN` pa minneskortet |
| `KAMEROR.BIN` | Alla fartkameror med vagens skyltade hastighet inbakad. Laggs som `DRIVE/KAMEROR.BIN` - eller ladda upp via enhetens wifi-sida |

Grenen `data` innehaller bara de har filerna, sa att koden pa `main` slipper
bara pa 80 MB data i varje klon.
