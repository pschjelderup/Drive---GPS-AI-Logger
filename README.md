# DriveLogger

Reselogg för **Waveshare ESP32-S3-Touch-AMOLED-2.41**. Loggar varje resa i bilen
till en GPX-fil och en rad i en resedagbok, märker resan som privat, företag
eller diffus, och varnar för fartkameror ur Trafikverkets öppna data.

Resan startar och slutar av sig själv. Det är inte en bekvämlighet utan hela
poängen: **en körjournal som bara innehåller de resor någon kom ihåg att trycka
igång är inte en körjournal.**

Bygger på [Gmate](https://github.com/pschjelderup/Gmate) – samma kort, samma
sensorer, samma deploykedja. Ecodrive-skärmen är flyttad hit oförändrad.

> ### Läs det här först
>
> Firmwaren är **kompilerad men aldrig körd på riktig hårdvara.** Logiken som går
> att pröva utan kort är prövad – tidsräkningen mot systemets egen `timegm` i
> 2 564 datum, lagningen av avbrutna GPX-filer i åtta avbrottsscenarier med en
> XML-parser som domare, och kamerasökningen i elva geometrifall. Men skärmens
> layout, pekytorna, piezon och I2C-bussen är oprövade i praktiken. Räkna med att
> första körningen hittar saker.

---

## Vad du behöver

| Sak | Krävs? | Kommentar |
|---|---|---|
| Waveshare ESP32-S3-Touch-AMOLED-2.41 | Ja | Med eller utan fodral (`-B` betyder bara att fodral ingår – samma kort) |
| u-blox-GPS, t.ex. SparkFun NEO-M9N | **Ja** | Utan position finns ingen resa att logga |
| **GNSS-antenn** | **Ja, om kortet har U.FL** | Se nedan. Det här är det lättaste att missa |
| Qwiic-kabel | Ja | Passar rakt in i `I2C`-porten. Ingen adapter, ingen lödning |
| microSD-kort | Ja | 8–128 GB, formaterat som **FAT32** eller **exFAT** |
| USB-C-kabel som klarar **data** | Ja | Många kablar är rena laddkablar och fungerar inte |
| Passiv piezo | Nej | Men utan den är varningen tyst – se [Ljudet](#ljudet), med köpförslag |
| 12 V-till-USB i bilen | Nej | Batteriet räcker inte till en arbetsdag |

Till skillnad från Gmate är GPS:en inte valfri. Saknas mottagaren säger skärmen
det rakt ut vid start i stället för att tiga och spara tomma filer.

### Antennen är inte valfri heller

SparkFuns NEO-M9N finns i två utföranden, och de ser nästan likadana ut:

| Kort | Antenn |
|---|---|
| **GPS-15712** – U.FL | **Ingen ombord.** Kräver extern antenn i kontakten märkt `ANT` |
| GPS-15733 – Chip Antenna | Inbyggd chipantenn vid vänstra Qwiic-kontakten |

Har du U.FL-varianten utan antenn får modulen **aldrig fix**. Skärmen svarar
"GPS söker" i evighet, ingen resa kan starta, och loggern blir helt tyst. Det
syns inte som ett fel i mjukvaran, för det är det inte.

För bil är rätt val en **aktiv magnetmonterad GNSS-antenn** på tak eller vindruta.
En antenn inne i kupén ser knappt satelliterna, och en logger som tappar fix i
tunnlar och under broar är en logger som delar resor på fel ställen. De flesta
magnetantenner har SMA-kontakt, så det behövs troligen en **U.FL-till-SMA-pigtail**
emellan.

> Sitter det ett **backupbatteri** (myntcell) på GPS-kortet minns modulen
> satellitbanorna mellan körningar. Då tar en varmstart sekunder i stället för
> minuter, vilket märks direkt: resan börjar där du startade bilen och inte en
> kilometer bort.

---

## Installera

1. Öppna **[flashsidan](https://pschjelderup.github.io/Drive---GPS-AI-Logger/)**
   i Chrome eller Edge på en dator. (Safari och Firefox kan inte prata med kort
   över USB.)
2. Sätt i USB-kabeln.
3. Klicka på **Installera DriveLogger på kortet** och välj kortets port i listan.
4. Vänta tills det står att det är klart.

Vilken version som sitter på kortet ser du i **MENY**. Stämmer den med raden på
flashsidan gick flashningen igenom.

Hittar datorn ingen port: håll inne **BOOT**, tryck och släpp **RESET**, släpp
sedan BOOT.

### Behöver jag kryssa i *Erase device*?

**Nej.** Firmwaren skrivs i två delar med ett hål emellan, precis där kortet
sparar sina inställningar. Följande överlever varje omflashning:

- Ljudval och skärmtimeout
- Ecodrive-gränserna och poängfönstret
- Tarat monteringsläge och inlärd framåtriktning
- **Resenumret**, så att nästa resa inte får samma nummer som en gammal

Kryssa i den bara när du *vill* börja om från noll. Då nollställs även
resenumret, vilket ger två resor med samma nummer om gamla filer ligger kvar på
minneskortet.

### Engångsinställning innan flashsidan fungerar

Flashsidan ligger på GitHub Pages, och det behöver slås på en gång:

1. **Settings → General**, längst ned: **Change repository visibility → Public**.
2. **Settings → Pages → Source: GitHub Actions**.
3. Slå ihop grenen till `main`. Bygget publicerar då sidan automatiskt.

---

## Så fungerar en resa

### Den startar och slutar själv

| Vad som händer | Vad enheten gör |
|---|---|
| Bilen börjar rulla, över 8 km/h i 4 sekunder | Resan startar. Ny GPX-fil, nytt nummer |
| Bilen står stilla, under 3 km/h | Klockan börjar ticka. Resan är fortfarande igång |
| Stillastående i 4 minuter | Resan avslutas |
| **Tändningen slås av** | Strömmen försvinner. Resan skrivs färdigt vid nästa start – se nedan |

Med **tändningsstyrd ström** – det tänkta driftläget – är den sista raden det
normala slutet på en resa: man parkerar, vrider av, och strömmen dör innan
fyraminutersklockan hunnit ticka klart. Det är inte ett undantag som ska
undvikas utan huvudspåret, och hela strömavbrottsmaskineriet nedan finns för att
det ska vara ett fullgott sätt att avsluta på. Fyraminutersregeln finns kvar för
det som blir över: tomgångskörning, färjelägen, långa köer.

Fyra minuter är valt med flit. Kortare delar rödljus och köer upp en resa i
bitar; längre gör att ett kundbesök på tio minuter försvinner in i samma resa.

**Målet sätts där bilen slutade röra sig**, inte där den stod när de fyra
minuterna gick ut. Annars hade varje resa fått parkeringstiden påhängd, och
målpunkten hamnat i GPS-bruset kring parkeringen.

Vill du styra själv finns knapparna kvar:

- **STARTA RESA NU** – startar utan att vänta på att bilen rullar
- **AVSLUTA RESA** – avslutar direkt
- **DELA HÄR** – avslutar den pågående resan och startar en ny från samma punkt,
  utan glapp emellan. För kundbesöket på vägen hem, som ska bli två rader

### Syftet: privat, företag eller diffust

Tre knappar nederst på huvudskärmen. De går att trycka **under** resan, **när
den avslutas**, eller – och det här är det vanliga med tändningsstyrd ström –
**vid nästa start**. Slås tändningen av innan frågan hunnit ställas är den inte
förverkad: nästa gång du vrider på möts du av *"Resa 12 · 34,5 km · Vad var
resan till?"* med samma stora knappar, medan du ändå sitter still.

Den som inte svarar inom en minut, eller kör iväg direkt, får resan märkt
**DIFFUST**. Det är inte en gissning utan en beskrivning: resan *var*
odefinierad, och det går att ändra i webben efteråt.

Trycker du **FÖRETAG** öppnas kundlistan direkt. Att märka en resa som
företagsresa och att säga vilken kund det gäller är i praktiken samma handling,
så det är en knapptryckning och inte två. Finns ingen kund att välja trycker du
**INGEN KUND – BARA FÖRETAG**, så behålls märkningen med tomt kundfält.

Kundlistan är filen `DRIVE/KUNDER.CSV`, ett namn per rad:

```
Volvo Torslanda
Scania Södertälje
4711;Kund med kundnummer
```

Står det ett id först, avskilt med semikolon, används namnet efter semikolonet.
Filen synkas ner från webben när den delen är byggd, men går alltid att skriva
för hand – **en enhet som kräver ett moln för att kunna namnge en kund är en
enhet som slutar fungera när molnet gör det.**

### Vad ett strömavbrott kostar

Det här är den del som fått mest omsorg, eftersom det är den som avgör om
journalen går att lita på.

GPX-filen avslutas korrekt **efter varje sparpunkt** och avslutningen skrivs över
av nästa punkt. Filen på kortet är alltså alltid komplett XML. Men en komplett
GPX-fil vet ingenting om att den tillhörde en resa som skulle ha ett mål, ett
syfte och en rad i dagboken. Det bär **tillståndsfilen** `DRIVE/PAGAR.BIN`:

- Två slots, växelvis, med löpnummer och kontrollsumma
- Varje slot är 512 byte, så de hamnar i **olika sektorer** på kortet – en
  avbruten skrivning kan då aldrig skada båda
- Skrivs om vid varje sparpunkt

Vid nästa start läses båda slotsen och den med högsta löpnummer och riktig
kontrollsumma gäller. Säger den att en resa var öppen, då försvann strömmen mitt
i den, och enheten:

1. **Lagar GPX-filen** om sista raden blev halvskriven – klipper efter sista hela
   punkten och skriver avslutningen igen
2. **Sätter målet** till sista kända position, med avslutsorsak `strom av`
3. **Ställer syftesfrågan** om resan inte hann taggas – med tändningsstyrd ström
   är det så frågan normalt ställs. Taggades resan under färd sägs ingenting
   alls: den skrivs tyst och klart
4. **Låter nästa resa börja på samma punkt** – bilen har inte flyttat sig av sig
   själv medan den var strömlös

Punkt 1 finns för att stänga ett smalt men verkligt hål: sparpunkten skrivs genom
filsystemets buffert, och går den bufferten över en sektorgräns mitt i en punkt
hamnar en halv punkt på kortet där avslutningen stod. Då är filen inte längre
giltig XML, och en kartapp vägrar öppna den – vilket är precis det
avslutningstricket skulle undvika. Fönstret är smalt, men det finns, så det tas
inte i med tro.

Kör du iväg utan att svara på frågan skrivs raden som *diffust* när den nya
resan börjar. Frågan hinner alltså aldrig gå förlorad med resan. **Ingenting
försvinner tyst, och ingenting tjatar i onödan.**

---

## Skärmen under färd

Huvudskärmen är byggd för att uppfattas i ögonvrån, inte läsas.

| Var | Vad |
|---|---|
| Överst | GPS-status och satelliter, antal kameror i listan, minneskort, ljud av/på |
| Farten | Stora siffror. Färgen säger om den är laglig: grön under, gul nära, röd över |
| Skylten | Skyltad hastighet, ritad som skylten vid vägen |
| Under farten | Hur många km/h över eller under, med siffra **och** stapel |
| Kamerarutan | Fartkameravarning, eller besked om att listan saknas |
| Reserutan | Resans nummer, sträcka, tid – och om den står stilla, hur länge |
| Nederst | Syftesknapparna, resknappen, och ECO / KUND / MENY |

Farten ritas med **egna segmentsiffror**, inte med förstorad text. Det inbyggda
typsnittet blir kantigt långt innan det blir stort nog att läsas i ögonvrån.

Varningarna och panelerna läggs **ovanpå** bilden med riktig alfablandning, inte
som rutor som tränger undan den. Det är möjligt eftersom allt ritas i en
bildbuffert i psram som går att läsa tillbaka. Ju närmare kameran, desto mindre
ljus släpper varningspanelen igenom – en varning på åttahundra meter ska inte se
ut som en på tvåhundra.

### Skärmen släcks bara när bilen står

Under en pågående resa är skärmen hela poängen och släcks aldrig av sig själv.
När bilen står parkerad släcks den efter den tid du valt i **MENY** – både för
strömmen och för att en AMOLED inte mår bra av en stillastående bild i timmar.

Tänd igen med **sidoknappen (BOOT)** eller ett tryck på skärmen. Första trycket
tänder bara skärmen; det märker aldrig en resa av misstag.

---

## Wifi och telefonen

När ingen resa pågår reser enheten ett eget wifi-nät. Det är så resorna lämnar
bilen och datafilerna kommer in – ingen kabel, ingen kortutmatning, ingen app.

1. Sätt dig i bilen med tändningen på (eller enheten på skrivbordet med USB).
2. Anslut telefonen till nätet **DriveLogger**, lösenord **kordagbok**.
   Båda står på skärmen när nätet är uppe.
3. Sidan öppnas **av sig själv** – samma mekanism som får ett hotellwifi att
   poppa upp. Öppnas den inte: gå till `http://192.168.4.1` i Safari.

På sidan:

| Del | Vad den gör |
|---|---|
| Statuskorten | Version, antal kameror, ledigt utrymme, kundlista |
| Resorna | Nyaste först, med syftesfärg, km och kund. **GPX** hämtar spåret, **Klar** flyttar det till `UPPLADDAT` |
| RESOR.CSV | Hela dagboken, öppnas direkt i Excel |
| Ladda upp | `KAMEROR.BIN`, `HASTIGHET.BIN` och `KUNDER.CSV` läggs på plats – kvittot säger t.ex. *"2771 kameror inlästa"* |

**Under färd är nätet släckt.** Det har inget ärende då, det drar ström, och en
webbsida är inget man ska titta på när man kör. Nätet kommer tillbaka några
sekunder efter att resan avslutats.

Uppladdningen skrivs först till en tillfällig fil, innehållet kontrolleras mot
filens magi, och först då byts den gamla filen ut – en halv kamerafil som redan
ligger på sin riktiga plats vore värre än ingen alls. Filbytet sker genom ett
handslag med avläsningstråden, så att kamerasökningen aldrig står mitt i en fil
som byts ut.

Lösenordet är ingen hemlighet utan en tröskel: utan det kan vem som helst på
parkeringen ladda ner dina resor. Vill du byta står det i `config.h`
(`WIFI_AP_PASSWORD`, minst åtta tecken).

---

## Fartkameror

### Varningen

Tre ringar, med olika ljud, så att man hör hur nära det är utan att titta:

| Avstånd | Ljud | Tanken |
|---|---|---|
| 800 m | ett kort, lågt pip | hinner släppa gasen utan att bromsa |
| 500 m | två pip | närmar sig |
| 250 m | fyra pip, sista längre | nu |

Ljudet kommer **en gång per ring och kamera**. Att åka bakåt genom en ring – köer,
avfarter – ger inget nytt pip.

Överhastighet har **egen röst**: två korta, låga. Man ska kunna höra skillnad på
"du kör för fort" och "det står en kamera där framme" utan att titta på skärmen.
Den kräver att överhastigheten hållit i sig fyra sekunder och vilar sedan i trettio,
så att en omkörning inte ger ett pip och en jämn överhastighet inte ger tvåhundra.

### Två villkor för att varna – och båda behövs

1. **Kameran ska ligga i färdriktningen.** Annars varnar vi för den vi just
   passerade.
2. **Kameran ska mäta i vårt håll.** Trafikverket anger vilken riktning kameran
   övervakar, så en kamera på motsatt körbana kan sorteras bort i stället för att
   skrika i onödan.

Saknar en kamera riktningsuppgift varnas den i båda körriktningarna – hellre en
varning för mycket än en utebliven.

Under 20 km/h varnas ingenting. Färdriktningen är inte att lita på när bilen
nästan står stilla, och en varning när man rullar fram i en kö är bara i vägen.

### Var datan kommer från

**Trafikverkets öppna API, objekttypen `TrafficSafetyCamera`.** Licensen är
**CC0** – ingen attribution krävs, inga villkor att hålla reda på. Ungefär 2 771
fasta kameror på 599 ATK-sträckor. En gratis API-nyckel hämtas på
[data.trafikverket.se](https://data.trafikverket.se/). Samma data ligger bakom
lagret *SpeedCameras* i
[Trafikverkets egen trafikkarta](https://www.trafikverket.se/trafikinformation/vag/).

Objekttypen har fälten `ID`, `Name`, `Bearing`, `Geometry.WGS84`,
`Geometry.SWEREF99TM`, `RoadNumber`, `Counties`, `IconId`, `Deleted` och
`ModifiedTime`. Två av dem är värda att veta om:

- **`Bearing`** är riktningen kameran är riktad mot, i grader medsols från norr.
  Det är den som gör att kameror på motsatt körbana kan sorteras bort.
- **`Deleted`** finns eftersom API:et låter borttagna objekt ligga kvar, så att
  den som synkar inkrementellt får veta att de försvunnit. För oss som hämtar allt
  på en gång är de nedmonterade kameror, och de filtreras bort. Att varna för en
  nedmonterad kamera är värre än att inte varna alls – det är sådant som får en
  att sluta lita på varningarna.

**Det finns ingen hastighetsuppgift i kameradatan.** Vill du att skylten ska visa
en siffra vid kamerorna måste vägens gräns bakas in när filen skapas, ur samma
NVDB-data som `HASTIGHET.BIN` byggs av:

```bash
export TRV_API_KEY=din-nyckel

# Utan hastigheter - kamerorna varnas, men utan siffra
tools/hamta-trafikverket.py kameror --ut KAMEROR.BIN

# Med hastigheter inbakade ur vägdatan
tools/hamta-trafikverket.py kameror --granser hastighet.geojson --ut KAMEROR.BIN
```

Lägg `KAMEROR.BIN` i mappen `DRIVE` på minneskortet. Hela Sverige är drygt
trettiotusen byte, så listan bor i minnet och genomsöks en gång i sekunden med
binärsökning på latitud – en bil på väg 73 behöver inte fråga om kameror i
Kiruna.

Byter Trafikverket schema säger verktyget vilka fält som faktiskt kom, i stället
för att tiga och skriva en tom fil:

```bash
tools/hamta-trafikverket.py kameror --visa-falt
```

---

## Hastighetsgränser

Den skyltade hastigheten kommer från **NVDB**, Trafikverkets nationella vägdatabas.
Sedan 2025 ligger tolv NVDB-dataset i det öppna API:et, och hela-Sverige-paket
finns på Lastkajen. Att läsa datan kräver inget konto; att ladda ner den gör det.

Hela Sverige är hundratals megabyte, alltså inte flashminnesmaterial. Därför:

```bash
tools/hamta-trafikverket.py granser --in hastighet.geojson --ut HASTIGHET.BIN
```

Verktyget lägger ut punkter var fyrtionde meter längs vägarna och sorterar dem på
latitud. Enheten söker **närmaste punkt inom sextio meter**, direkt i filen med
binärsökning – filen behöver alltså aldrig läsas in i minnet, bara sökas i. En
uppslagning jämför några tiotal punkter, inte hundratusentals.

Hittas ingen punkt kör vi på en väg som inte finns i filen, och då svarar enheten
**inte vet** i stället för att gissa. En gissad hastighetsgräns är sämre än ingen.

Saknas `HASTIGHET.BIN` helt används den hastighet som bakats in i kameraposten,
inom 1,5 km från en kamera – se `--granser` ovan. Har inget bakats in visas ingen
gräns alls, och därmed heller ingen över- eller underhastighet.

> **Den kända svagheten:** enheten söker närmaste *punkt*, inte närmaste *väg*. I
> en korsning kan därför den korsande vägens gräns vinna, eftersom dess punkter
> ligger lika nära. Det varar de sekunder det tar att passera korsningen. Att
> matcha mot linjegeometri i stället hade löst det, men kostar både minne och
> komplexitet som en punktsökning slipper – och en väg är sällan bara någon
> enstaka meter bred.

> **Tänkt nästa steg:** låt backenden baka hastighetsfiler per geografisk ruta och
> skicka bara de rutor du faktiskt kör i – din egen heatmap talar om vilka. Då
> blir filen några megabyte i stället för några hundra, utan att täckningen
> försämras där du kör.

---

## Ljudet

**Kortet har ingen högtalare, ingen summer och ingen ljudkrets.** Till skillnad
från Waveshares 1.8"- och 2.06"-syskon finns ingen ES8311 ombord. Varningen
kräver därför en piezo utifrån.

Den kopplas till **UART-portens TXD-stift**, och det är valt med flit:

| | Detalj |
|---|---|
| Kontakt | JST SH 1,0 mm, 4-polig – samma som Qwiic |
| Stiftordning | GND · 3V3 · TXD · RXD, tryckt i klartext vid kontakten |
| Piezon kopplas | mellan **TXD** och **GND** |
| Lödning på kortet | **Ingen.** Piezon löds till en Qwiic-kabel |

GPIO43 är UART0:s sändarstift och är ledigt eftersom kortet byggs med
`CDCOnBoot=cdc` – all serieutmatning går över USB och UART0 används inte. Vill du
hellre använda RXD-stiftet är det GPIO44; ändra `PIN_BUZZER` i
`firmware/DriveLogger/config.h`.

> Kontakten märkt `UART` sitter bredvid den märkta `I2C`, och en Qwiic-kabel
> passar fysiskt i båda – och i den märkta `RTC`. Kontrollera texten vid
> kontakten. GPS:en ska i `I2C`, piezon i `UART`.

### Vilken piezo ska jag köpa?

En **passiv** sådan – en aktiv summer har egen oscillator, låter bara på en enda
ton och struntar i tonerna firmware spelar. Beprövat och billigt:

- [AZDelivery KY-006, 3-pack](https://www.amazon.se/AZDelivery-Piezo-summerlarmmodul-kompatibel-Raspberry-inklusive/dp/B07DPR4BTN)
  på amazon.se, ca 60 kr med Prime. Passiv piezomodul med tre ben: signalbenet
  (mitten eller `S`) till TXD, minus till GND, tredje benet lämnas okopplat.
  Tre stycken betyder två i reserv den dagen ett ben bryts av.
- Söker du något ljudstarkare: en passiv **elektromagnetisk** summermodul (ofta
  märkt *passive buzzer 3.3V*) låter mer vid lägre frekvenser, på samma
  inkoppling.

Till inkopplingen utan lödkolv mot kortet: en **Qwiic-kabel med öppna ändar**
(söks som *"Qwiic cable breadboard jumper"* – SparkFuns egen heter så) i
`UART`-porten. Gul ledare är stift 4 (TXD) och svart GND; piezons signalben på
den gula, minus på den svarta, och de två övriga ledarna isoleras och lämnas.

Ljudet stängs av med **LJUD PÅ/AV** uppe till höger på huvudskärmen, eller i
**MENY**. Valet sparas. En reselogg som tjuter när man kör med sovande barn i
baksätet blir en reselogg man drar ur.

Allt ljud ligger bakom ett litet gränssnitt som säger *vad som hänt*, inte hur det
ska låta. En I2S-förstärkare med inspelade ord – "fartkamera, trehundra meter" –
sågas in på samma ställe utan att någon varningslogik behöver skrivas om.

---

## Filerna på kortet

Allt bor i mappen `DRIVE`:

| Fil | Vad det är |
|---|---|
| `GPX/R0042.GPX` | Ett spår per resa. Dra in i Google Earth eller vilken kartapp som helst |
| `RESOR.CSV` | Resedagboken för människor. Öppnas direkt i Excel |
| `RESOR.JSONL` | Samma resor för maskiner. En JSON-rad per resa, för synken |
| `PAGAR.BIN` | Tillståndsfilen. Se [Vad ett strömavbrott kostar](#vad-ett-strömavbrott-kostar) |
| `KAMEROR.BIN` | Fartkamerorna |
| `HASTIGHET.BIN` | Hastighetsgränserna |
| `KUNDER.CSV` | Kundlistan |
| `UPPLADDAT/` | Hit flyttas synkade GPX-filer. De raderas inte – kortet är den enda kopian tills något annat bevisats |

### Resedagboken

`RESOR.CSV` använder **semikolon** som avdelare och **komma** som decimaltecken.
Det är inte slarv: Excel på en svensk dator läser `12.3` som text men `12,3` som
ett tal, och en kolumn man inte kan summera är en kolumn man inte har.

| Kolumn | Betydelse |
|---|---|
| `resa` | Resans nummer, samma som i filnamnet |
| `start`, `mal` | Lokal tid |
| `minuter`, `km` | Restid och sträcka |
| `syfte`, `kund` | Privat / Företag / Diffust, och kundnamn om något valts |
| `start_lat`, `start_lon`, `mal_lat`, `mal_lon` | Positionerna, sju decimaler |
| `maxfart_kmh`, `fortkorning_min` | Högsta uppmätta fart, och minuter över skyltad |
| `ecopoang`, `harda_moment` | Från ecodrive, per resa |
| `avslut` | `automatiskt`, `knapp`, `strom av` eller `kortet fullt` |
| `gpx` | Sökväg till spåret |
| `karta` | Färdig Google Maps-länk med start och mål |

`karta`-kolumnen är en `maps/dir`-länk, alltså hela resan och inte bara en punkt.
Klicka och du ser sträckan.

### Om klockan

Klockan hålls i **UTC**, alltid. Det är den tid som ska stå i en GPX-fil och den
enda som är entydig året om. Lokal tid räknas fram när den ska visas för en
människa – lagras aldrig.

Offseten sitter i `GNSS_UTC_OFFSET_MINUTES` i `config.h` och står på `120`
(svensk sommartid). Sommartiden byts **inte** om automatiskt; ändra till `60` på
vintern. Det påverkar bara vad du ser och vad som står i `RESOR.CSV` – GPX-filerna
är riktiga oavsett.

Kortet har en egen klocka med backup. Är den ny vet den inte vad tiden är, och
ställs då till tidpunkten firmware byggdes. Så fort GPS:en fått fix ställs den
efter satellittid, och sedan en gång i timmen för att motverka drift.

---

## ECODRIVE-skärmen

Knappen **ECO** öppnar Gmates ecodrive-skärm, oförändrad. Den visar hur mjukt du
kör medan du kör: en bubbla som ska stå still i mitten, en poäng 0–100, och
räknare för hårda moment uppdelade i gas, broms och kurva.

Hela beskrivningen – varför sparsam och mjuk körning är samma sak, varför kortet
får sitta hur som helst, hur framåtriktningen lärs in ur GPS-farten, och vad TARA
är till för – står i
[Gmates README](https://github.com/pschjelderup/Gmate#ecodrive-skärmen). Ingenting
av det har ändrats.

Två skillnader:

- **Poängen nollställs vid varje ny resa**, så att siffran säger något om den här
  körningen och inte om allt sedan kortet flashades.
- **Poängen och de hårda momenten sparas per resa** i dagboken, så att utvecklingen
  går att följa över tid utan att öppna rådatan.

---

## Det som inte är byggt än

Ärlig lista, så att ingen letar efter knappar som inte finns:

| Vad | Läge |
|---|---|
| **Bluetooth** | Struken med flit – iPhone saknar Web Bluetooth, wifi-sidan gör jobbet |
| **Webbgränssnittet** med heatmaps och insikter | Inte byggt. Supabase + Vercel är valt |
| **Molnsynk till Supabase** | Inte byggd. Wifi-sidan är bryggan; nästa steg är att webappen hämtar därifrån |
| **Hastighetsfiler per ruta** | Inte byggd. Hela NVDB-exporten får duga |
| **Resehistorik på skärmen** | Inte byggd. `RESOR.CSV` på kortet är historiken |
| **Mätarställning** | Matas in och justeras i webappen när den byggs – enheten loggar sträckan per resa, appen räknar ackumulerat och ber om avstämning |

### Om Skatteverket

Skatteverket vill utöver det som loggas här också se **mätarställning vid start
och stopp**, och för tjänsteresor vilken kund som besöktes. Kunden finns redan.
Mätarställningen är tänkt att bo i webappen: enheten loggar sträckan per resa,
appen ackumulerar den till en löpande mätarställning, och ber om en avstämning
mot instrumentbrädan då och då. Avvikelsen mellan GPS-sträcka och mätare är
normalt någon procent, så avstämningarna håller journalen ärlig utan att du
behöver läsa av bilen vid varje resa.

---

## Om något strular

| Symptom | Vad det brukar vara |
|---|---|
| Skärmen är svart efter flashning | Fel USB-kabel, eller kortet sitter kvar i flashläge. Tryck **RESET** |
| **INGEN GPS** vid start | Qwiic-kabeln sitter i `UART` eller `RTC` i stället för `I2C`. Kontrollera texten vid kontakten |
| GPS-pricken är gul och stannar gul | Mottagaren fungerar men ser inga satelliter. **Kontrollera först att en antenn sitter i** – U.FL-kortet har ingen ombord. Sedan: flytta antennen mot fönstret eller taket. En kall start utan backupbatteri kan ta minuter |
| Inget minneskort | Kortet sitter inte i ordentligt, eller är formaterat som NTFS. Formatera om till FAT32 eller exFAT |
| Ingen resa startar när jag kör | Ingen GPS-fix. Reserutan säger *väntar på GPS-fix* när det är fallet |
| Resan delades i två | Bilen stod stilla längre än fyra minuter. Höj `TRIP_STOP_S` i `config.h` |
| Två resor blev en | Uppehållet var kortare än fyra minuter. Tryck **DELA HÄR**, eller sänk `TRIP_STOP_S` |
| Ingen kameravarning | `KAMEROR.BIN` saknas – statusraden säger det. Eller så är farten under 20 km/h |
| Ingen hastighetsgräns visas | `HASTIGHET.BIN` saknas, eller vägen finns inte i filen. Enheten gissar med flit inte |
| Piezon är tyst | Kolla att ljudet är på, att kabeln sitter i `UART`, och att piezon är **passiv** – en aktiv summer har egen oscillator och låter bara på en ton |
| Knapptryck hamnar fel på skärmen | Sätt `TOUCH_FLIP_X` eller `TOUCH_FLIP_Y` till `1` i `config.h` |
| Kortet beter sig som den gamla versionen | Jämför versionen i **MENY** med raden på flashsidan. Skiljer de sig: ladda om sidan med `Ctrl`+`F5` |

Sitter enheten i bilen är serieporten den enda insynen. Den skriver en statusrad
var femte sekund med GPS-läge, resans tillstånd, aktuell hastighetsgräns och
antal inlästa kameror – även med släckt skärm, vilket är precis när man behöver
den.

---

## För den som vill bygga själv

Firmware ligger i `firmware/DriveLogger` och är en vanlig Arduino-skiss. Den byggs
automatiskt av GitHub Actions vid varje ändring, och resultatet blir både en
nedladdningsbar fil och flashsidan.

```bash
arduino-cli core install esp32:esp32@3.3.11
arduino-cli lib install "GFX Library for Arduino"@1.6.7
arduino-cli lib install "SensorLib"@0.4.1
arduino-cli lib install "SparkFun u-blox GNSS v3"@3.1.14
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,FlashMode=qio,PartitionScheme=huge_app,CDCOnBoot=cdc \
  firmware/DriveLogger
```

Kortpaketet är drygt sju gigabyte, eftersom det innehåller kompilatorer och
förkompilerade bibliotek för varje chip i ESP32-familjen – inte bara för S3:an.
Av det är ungefär 1,4 GB relevant här. Bygget i CI cachar det mellan körningar.

### Modulerna

| Fil | Ansvar |
|---|---|
| `DriveLogger.ino` | Uppstart, skärmval, pekhantering, inställningar |
| `config.h` | Pinnar och konstanter. Allt som är ett val bor här |
| `sensors.cpp` | Rörelsesensor, klocka, minneskort, avläsningstråden |
| `gnss.cpp` | u-blox-mottagaren. Oförändrad från Gmate |
| `trip.cpp` | Resedetektorn, GPX-skrivningen, dagboken, tillståndsfilen |
| `cams.cpp` | Fartkameror och hastighetsgränser |
| `sound.cpp` | Piezon, bakom ett gränssnitt som beskriver händelser |
| `eco.cpp` | Ecodrive. Oförändrad från Gmate |
| `ui.cpp` | Allt som ritas, inklusive alfablandning och segmentsiffror |
| `geo.cpp` | Avstånd och bäring på jordytan |
| `customers.cpp` | Kundlistan från kortet |

**Bara en tråd skriver till minneskortet.** Resan, dagboken och tillståndsfilen
skrivs alla från avläsningstråden, och skärmen beställer i stället för att skriva
själv. Då kan två skrivningar aldrig krocka, och all filskrivning har ett enda hem.
Läsningar får ske från skärmtråden – kundlistan gör det – eftersom
filsystemslagret serialiserar åtkomsten. Det som inte får ske därifrån är att
frigöra något avläsningstråden samtidigt söker i, och därför är
`cams::reload()` en beställning och inte en inläsning.

Pinnarna i `config.h` är verifierade mot två oberoende källor: Waveshares egen
Arduino-kortdefinition och CircuitPythons kortdefinition för samma kort. Båda
anger identiska pinnar, och de är dessutom provkörda i Gmate.

---

## Källor

- [Trafikverkets öppna API för trafikinformation](https://www.trafikverket.se/e-tjanster/trafikverkets-oppna-api-for-trafikinformation/) – `TrafficSafetyCamera`, CC0
- [Hämta öppen data från Trafikverket](https://www.trafikverket.se/e-tjanster/hamta-data-fran-trafikverket/) – datautbytesportalen och Lastkajen
- [NVDB-vägdata i det öppna API:et](https://www.nvdb.se/sv/aktuellt/nyhetsarkiv/2025/nvdb-vagdata-tillgangliga-i-trafikverkets-datautbytesportal-for-anvandning-i-oppet-api/) – hastighetsgränser
- [Waveshare ESP32-S3-Touch-AMOLED-2.41](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.41) – kortets dokumentation
- [SparkFun GPS Breakout NEO-M9N, U.FL (Qwiic)](https://www.sparkfun.com/sparkfun-gps-breakout-neo-m9n-u-fl-qwiic.html) – kräver extern antenn
- [SparkFun GPS NEO-M9N Hookup Guide](https://learn.sparkfun.com/tutorials/sparkfun-gps-neo-m9n-hookup-guide/all) – inkoppling och antennval
- [Gmate](https://github.com/pschjelderup/Gmate) – prototypen det här bygger på
