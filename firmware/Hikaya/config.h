// Hikaya - reselogg for Waveshare ESP32-S3-Touch-AMOLED-2.41
//
// Pinnarna nedan ar verifierade mot tva oberoende kallor:
//  1. Waveshares egen Arduino-kortdefinition (Arduino_GFX_dev_device.h,
//     blocket WAVESHARE_ESP32_S3_TOUCH_AMOLED_2_41)
//  2. CircuitPythons kortdefinition waveshare_esp32_s3_amoled_241
// Bada anger identiska pinnar. De ar dessutom provkorda i Gmate, som ar samma
// kort med samma kringutrustning.

#pragma once

#include <Arduino.h>

// ---------------------------------------------------------------- bygget ---
// Versionen och pull request-numret bakas in av bygget. Tillsammans pekar de
// ut exakt vilken kod som sitter pa kortet: "b534928 PR8" gar att sla upp pa
// GitHub utan att gissa. Lokala byggen far "lokal" och inget PR-nummer.
#ifndef FW_VERSION
#define FW_VERSION "lokal"
#endif
#ifndef FW_PR
#define FW_PR ""
#endif

static inline const char *fwVersionFull() {
  static char s[24] = "";
  if (!s[0]) {
    if (FW_PR[0]) {
      snprintf(s, sizeof(s), "%s PR%s", FW_VERSION, FW_PR);
    } else {
      snprintf(s, sizeof(s), "%s", FW_VERSION);
    }
  }
  return s;
}

// ------------------------------------------------------------- kortval ----
// Tva kort, en kodbas. Bygget valjer med -DBOARD_LCD35; utan flaggan byggs
// AMOLED 2.41-varianten. Flashsidan har en modellvaljare - korten har samma
// processor, sa firmware kan inte kanna igen kortet sjalv.

#if defined(BOARD_LCD35)
// ---------------------------------------------------------------- skarm ----
// Waveshare ESP32-S3-Touch-LCD-3.5: ST7796 via vanlig SPI, 320x480 staende.
// CS ar fast strappad pa kortet (skarmen ar ensam pa bussen) och RST gar via
// io-expandern - darfor finns ingen CS/RST-pinne har. Pinnarna ar lasta ur
// Waveshares schema (ESP32-S3-Touch-LCD-3.5-Schematic.pdf).
#define PIN_LCD_MOSI 1
#define PIN_LCD_MISO 2
#define PIN_LCD_DC 3
#define PIN_LCD_SCK 5
#define PIN_LCD_BL 6

#define SCREEN_W 320
#define SCREEN_H 480
#define LCD_COL_OFFSET 0

// ------------------------------------------------------------------ i2c ----
// Delad buss: FT6336 (pekskarm), QMI8658 (rorelse), PCF85063 (klocka),
// AXP2101 (strom), TCA9554 (io-expander).
#define PIN_I2C_SDA 8
#define PIN_I2C_SCL 7
#define PIN_TOUCH_RST -1

// ------------------------------------------------------------------ gps ----
// GPS:en pa det har kortet ar en GT-U7 (NEO-6M-klon) pa stiftlistens
// uart, inte qwiic-i2c som pa 2.41: korsade kablar, modulens TXD till
// GPIO44 (RXD) och modulens RXD till GPIO43 (TXD), plus 3V3 och GND.
// NEO-6M pratar nmea pa 9600 baud - u-blox-biblioteket (M8+) kan den
// inte, sa gnss.cpp har en egen nmea-tolk for det har kortet.
// OBS: GPIO43 ar darmed upptagen - piezo-stubben far aldrig rora den.
#define GNSS_UART 1
#define PIN_GNSS_RX 44
#define PIN_GNSS_TX 43
#define GNSS_BAUD 9600

// Io-expandern ager tre signaler vi behover: skarmens reset, kortplatsens CS
// och pekskarmens avbrott (som vi anda pollar).
#define EXPANDER_I2C_ADDR 0x20
#define EXIO_LCD_RST 1
#define EXIO_TP_INT 2
#define EXIO_SD_CS 3

#else
// ---------------------------------------------------------------- skarm ----
// Waveshare ESP32-S3-Touch-AMOLED-2.41: RM690B0 AMOLED via QSPI. Panelen ar
// 450x600 (staende) och har 16 pixlars kolumnoffset. Pinnarna ar verifierade
// mot Waveshares kortdefinition och CircuitPythons, och provkorda i Gmate.
#define PIN_LCD_CS 9
#define PIN_LCD_SCK 10
#define PIN_LCD_D0 11
#define PIN_LCD_D1 12
#define PIN_LCD_D2 13
#define PIN_LCD_D3 14
#define PIN_LCD_RST 21

// GPIO16 matar bade skarmen och batterikretsen. Maste dras hog forst av allt,
// annars tander skarmen aldrig.
#define PIN_PANEL_POWER 16

#define SCREEN_W 450
#define SCREEN_H 600
#define LCD_COL_OFFSET 16

// ------------------------------------------------------------------ i2c ----
// Delad buss: QMI8658 (rorelsesensor), FT6336U (pekskarm), PCF85063 (klocka),
// TCA9554 (io-expander, anvands inte har).
#define PIN_I2C_SDA 47
#define PIN_I2C_SCL 48
#define PIN_TOUCH_RST 3
#endif

// Pekskarmens avbrottssignal gar via io-expandern, inte till en riktig GPIO,
// sa vi lasar av pekskarmen med pollning i stallet.
#define TOUCH_IRQ_NOT_CONNECTED -1

// u-blox GPS pa samma i2c-buss. Adressen krockar inte med nagot ombord:
// rorelsesensorn ligger pa 0x6B, pekskarmen 0x38, klockan 0x51 och
// io-expandern 0x20.
//
// Till skillnad fran Gmate ar GPS:en inte valfri har. En reselogg utan
// position har ingenting att logga, sa saknas mottagaren sager skarmen det
// rakt ut i stallet for att tiga och spara tomma filer.
#define GNSS_I2C_ADDR 0x42

// GPS ger tiden i UTC, vilket ar entydigt aret om och det som ska sta i en
// gpx-fil. Resedagboken visar lokal tid genom att lagga pa offseten nedan.
// Sommartiden byts inte om automatiskt.
#define GNSS_UTC_OFFSET_MINUTES 120

// Om pekningarna hamnar fel: satt dessa till 1 for att spegla respektive axel.
#define TOUCH_FLIP_X 0
#define TOUCH_FLIP_Y 0
#define TOUCH_SWAP_XY 0

// ------------------------------------------------------------- minneskort --
#if defined(BOARD_LCD35)
// Kortplatsen sitter pa SDMMC i 1-bitslage, precis som pa 2.41-kortet -
// Waveshares egen SD-demo kor SD_MMC.setPins(11, 10, 9). Kortets D3/CS
// gar via io-expandern och maste hallas HOG, annars hamnar kortet i
// SPI-lage vid forsta kommandot.
#define PIN_SD_CLK 11
#define PIN_SD_CMD 10
#define PIN_SD_D0 9
#else
// Kortplatsen sitter pa SDMMC i 1-bitslage.
#define PIN_SD_CLK 4
#define PIN_SD_CMD 5
#define PIN_SD_D0 6
#endif

// ---------------------------------------------------------------- knappar --
// BOOT-knappen. Anvands som skarm av/pa.
#define PIN_BOOT_BUTTON 0

// ------------------------------------------------------------------ ljud ---
// Kortet har ingen hogtalare, ingen summer och ingen ljudkrets. Varningen
// kraver darfor en piezo utifran, och den kopplas till UART-portens TXD-stift.
//
// Det ar med flit: UART-porten ar en JST SH 1,0 mm 4-polig kontakt med
// stiftordningen GND / 3V3 / TXD / RXD, tryckt i klartext bredvid kontakten.
// En vanlig Qwiic-kabel passar rakt in, sa piezon kan lodas till en kabel i
// stallet for till kortet. Ingen lodning pa sjalva kortet behovs.
//
// GPIO43 ar UART0:s sandarstift. Det ar ledigt eftersom kortet byggs med
// CDCOnBoot=cdc, sa all serieutmatning gar over USB och UART0 anvands inte.
// Vill du hellre anvanda RXD-stiftet ar det GPIO44.
#define PIN_BUZZER 43

// Piezon drivs med en fyrkantsvag fran ledc. En passiv piezo later hogst kring
// sin egen resonansfrekvens, och de flesta ligger nara 4 kHz.
#define BUZZER_LEDC_CHANNEL 0
#define BUZZER_RESONANCE_HZ 4000

// ------------------------------------------------------------ resedetektor -
// En resa borjar av sig sjalv nar bilen rullar och slutar av sig sjalv nar den
// star stilla. Grundtanken ar att en korjournal aldrig far bero pa att nagon
// kom ihag att trycka pa en knapp.
//
// Med tandningsstyrd strom - det tankta driftlaget - slutar de flesta resor i
// praktiken med att strommen forsvinner, och da skrivs resan fardigt vid nasta
// start (se trip.cpp). Stillastaendegransen nedan ar for resten: tomgang,
// farjelagen, langa koer.

// Sa fort maste det ga for att raknas som rorelse. Under detta ar det
// gps-brus: en stillastaende mottagare rapporterar sallan exakt noll.
#define TRIP_START_KMH 8.0f

// ... och sa lange maste det halla i sig. Ett enstaka brusutslag ska inte
// starta en resa.
#define TRIP_START_S 4

// Under den har farten raknas bilen som stillastaende.
#define TRIP_STOP_KMH 3.0f

// Sa lange maste den sta stilla for att resan ska raknas som avslutad. Kortare
// an sa har delar rodljus och koer upp en resa i bitar; langre an sa har gor
// att ett kundbesok pa tio minuter forsvinner in i samma resa.
#define TRIP_STOP_S 240

// Malet satts dar bilen slutade rora sig, inte dar den stod nar tiden gick ut.
// Annars hade varje resa fatt fyra minuters extra parkeringstid pahangd, och
// malpunkten hade legat i gps-bruset kring parkeringsplatsen.

// ---------------------------------------------------- palitlig gps-fart ----
// Farten anvands bara nar mottagaren sjalv star for den: fixet ska vara
// tredimensionellt och ligga inom mottagarens egna noggrannhetsmasker, och
// den angivna osakerheten far inte vara storre an sa har. Utan de kraven
// slinker enstaka vansinnesvarden igenom under svag mottagning - en enda
// sadan punkt racker for att forstora en resas maxfart for alltid.
#define SPEED_TRUST_ACC_KMH 10.0f

// Over detta ar vardet inte en bil. Taket ligger val over allt bilen kan
// gora men langt under de skrapvarden en mottagare kan lamna under
// uppstarten.
#define SPEED_TRUST_MAX_KMH 250.0f

// ------------------------------------------------------------- sparpunkter -
// Tatast mojliga avstand mellan tva sparpunkter, i sekunder.
#define TRACK_MIN_INTERVAL_S 2

// En punkt sparas forst nar bilen flyttat sig sa har manga meter. Utan den
// regeln fylls sparet med tusentals identiska punkter sa fort man star stilla,
// och gps-bruset ritar ett garnnystan dar bilen faktiskt stod.
#define TRACK_MIN_MOVE_M 10.0

// ... men en punkt sparas anda sa har ofta, aven vid stillastaende, sa att
// uppehallet syns i sparet i stallet for att forsvinna.
#define TRACK_MAX_INTERVAL_S 60

// -------------------------------------------------------------- fartkamera -
// Avstanden da varningarna ger sig till kanna. Den forsta ska komma sa tidigt
// att man hinner slappa gasen utan att bromsa, den sista sa nara att den bara
// bekraftar det man redan ser.
#define CAM_WARN_FAR_M 800
#define CAM_WARN_MID_M 500
#define CAM_WARN_NEAR_M 250

// Sa langt bakom kameran den forsvinner ur bilden. Utan marginal blinkar
// varningen till igen av ett enda gps-hopp precis efter passagen.
#define CAM_PASSED_M 80

// Kameran maste ligga framfor bilen for att varnas om: riktningen fran bilen
// till kameran far hogst avvika sa har mycket fran fardriktningen.
#define CAM_AHEAD_TOLERANCE_DEG 55.0f

// ... och kameran ska matas i samma riktning som vi kor. Trafikverket anger
// vilket hall kameran tittar, sa en kamera pa motsatt korbana kan sorteras
// bort i stallet for att skrika i onodan.
#define CAM_BEARING_TOLERANCE_DEG 60.0f

// Sa nara en kamera maste man vara for att dess skyltade hastighet ska anses
// galla for vagen man kor pa.
#define CAM_LIMIT_RADIUS_M 1500

// Sokfonstret i kameralistan, i grader latitud. En grad ar 111 km, sa det har
// ar drygt tre kilometer i nord-sydlig riktning - val over den langsta
// varningsstrackan.
#define CAM_SEARCH_WINDOW_DEG 0.03

// Hur ofta kameralistan genomsoks. En gang i sekunden ar samma takt som
// mottagaren levererar positioner i, sa tatare vore bortkastat arbete.
#define CAM_SCAN_INTERVAL_MS 1000

// -------------------------------------------------------- hastighetsgrans -
// Sa nara ett hastighetsprov maste bilen vara for att provet ska anses galla.
// Punkterna ligger tatt langs vagarna, sa ett langre avstand an sa har betyder
// att vi kor pa en vag som inte finns i filen.
#define LIMIT_MATCH_RADIUS_M 60

// Delstorleken pa molnets stora filer - ett kontrakt med webappen
// (PART_BYTES i DataFiles.jsx). Hela delar ar giltiga aterupptagnings-
// punkter: en bruten nedladdning kostar en del, inte hela filen.
#define CLOUD_PART_BYTES (4UL * 1024UL * 1024UL)

// Overhastighet raknas forst har. Bilens hastighetsmatare visar med flit for
// mycket, gps-farten ar den sanna, och ingen vill bli tillsagd for tre km/h.
#define LIMIT_TOLERANCE_KMH 3.0f

// ---------------------------------------------------------------- ecodrive -
// Granserna ar satta efter hur det kanns i bilen, inte efter vad som ar
// tekniskt mojligt. 0,15 g ar ungefar sa mycket man kanner utan att tanka pa
// det; 0,30 g ar en inbromsning som far passagerarna att titta upp.
#define ECO_SOFT_G 0.15f
#define ECO_HARD_G 0.30f

// Sa lange maste vardet ligga over den harda gransen for att raknas som ett
// hart moment. Ett potthal eller en brunnslock ger en spik pa nagra
// hundradels sekunder; en riktig inbromsning haller i sig. Utan kravet
// raknas vagens skick, inte korningen.
#define ECO_EVENT_MIN_S 0.3f

// Sa manga sekunder matning kravs innan resans ecopoang anses saga nagot.
// Kortare an sa ar poangen en gissning, och da redovisas den inte alls.
#define ECO_MEASURED_MIN_S 60

// Handelsen raknas som avslutad forst har, sa att ett enda haftigt ryck inte
// raknas som fem handelser nar vardet studsar kring gransen.
#define ECO_CLEAR_G 0.20f

// Poang som dras per g over den mjuka gransen och sekund.
#define ECO_PENALTY_PER_G_S 40.0f

// Tiden poangen speglar. En sammanhangande mjuk stracka sa har lang tar
// poangen fran noll tillbaka till hundra.
static const uint16_t kEcoWindowS[] = {60, 120, 300, 600, 1800, 3600};
static const uint8_t kEcoWindowCount = 6;
#define DEFAULT_ECO_WINDOW_INDEX 1  // 2 min

// Vad ytterringen i bubblan motsvarar.
#define ECO_BUBBLE_FULL_G 0.40f

// Vardena ovan ar startvarden. De gar att andra i gransmenyn medan bilen
// rullar, och valet sparas. Listorna nedan ar vad man kan valja mellan.
static const float kEcoSoft[] = {0.08f, 0.10f, 0.12f, 0.15f,
                                 0.18f, 0.20f, 0.25f, 0.30f};
static const uint8_t kEcoSoftCount = 8;
#define DEFAULT_ECO_SOFT_INDEX 3  // 0,15 g

static const float kEcoHard[] = {0.20f, 0.25f, 0.30f, 0.35f,
                                 0.40f, 0.50f, 0.60f};
static const uint8_t kEcoHardCount = 7;
#define DEFAULT_ECO_HARD_INDEX 2  // 0,30 g

static const float kEcoBubble[] = {0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.80f};
static const uint8_t kEcoBubbleCount = 6;
#define DEFAULT_ECO_BUBBLE_INDEX 2  // 0,40 g

// Hur hart poangen straffar. Lag siffra ger en snall matare, hog en strang.
static const float kEcoPenalty[] = {10.0f, 20.0f, 40.0f, 60.0f, 90.0f};
static const uint8_t kEcoPenaltyCount = 5;
#define DEFAULT_ECO_PENALTY_INDEX 2  // 40

// Tidskonstanter for att hitta tyngdkraften. Den snabba anvands nar kortet
// ligger stilla - da ar den uppmatta vektorn tyngdkraften och det finns ingen
// anledning att vara forsiktig. Den langsamma anvands under fard, sa att en
// utdragen kurva inte hinner tolkas som "ned".
#define ECO_GRAVITY_TAU_FAST_S 2.0f
#define ECO_GRAVITY_TAU_SLOW_S 30.0f

// Sa har stilla maste det vara for att raknas som vila. Gyrot ar det som
// avgor: en bil i en jamn kurva har stor sidoacceleration men ocksa en tydlig
// girhastighet, medan ett kort som ligger pa ett bord har ingen alls.
#define ECO_REST_GYRO_DPS 2.5f

// ... och sa lite far accelerationsvektorn andra sig mellan tva avlasningar.
#define ECO_REST_JITTER_G 0.04f

// Under manover uppdateras lodlinjen inte alls. Det ar det som gor att en lang
// avfart eller rondell far behalla sitt varde i stallet for att sjunka undan.
#define ECO_FREEZE_MAG_G 0.12f
#define ECO_FREEZE_LONG_G 0.06f

// Men aldrig langre an sa har. Frysningen avgors av ett varde som raknas fram
// ur lodlinjen, sa en felaktig lodlinje kan halla sig sjalv fryst i all
// evighet om den slapps los. En riktig kurva varar inte tolv sekunder; en
// felaktig lodlinje varar tills stromen bryts.
#define ECO_FREEZE_MAX_S 12.0f

// Sa mycket maste hanna for att en handelse ska duga till att lara ut vilket
// hall som ar framat. Under detta ar bruset for stort for att lita pa.
#define ECO_FWD_MIN_LONG_G 0.08f
#define ECO_FWD_MIN_MAG_G 0.08f

// Inlarningstakt: snabb precis efter en tara, langsam darefter sa att
// riktningen inte vandrar av enstaka konstiga handelser.
#define ECO_FWD_GAIN_FAST 0.20f
#define ECO_FWD_GAIN_SLOW 0.04f

// ------------------------------------------------------------------ filer --
#define DRIVE_DIR "/DRIVE"

// En gpx-fil per resa. Mappen halls ren pa sa satt att en synkad resa flyttas
// till UPPLADDAT, inte raderas - kortet ar den enda kopian tills nagot annat
// bevisats.
#define GPX_DIR "/DRIVE/GPX"
#define GPX_SYNCED_DIR "/DRIVE/UPPLADDAT"

// Resedagboken. Tva format med flit: csv for den som vill oppna den i Excel,
// jsonl for synken. En rad per resa i bada, och radslut skrivs sist, sa att en
// halvskriven rad efter ett stromavbrott gar att kanna igen och slanga.
#define TRIPS_CSV "/DRIVE/RESOR.CSV"
#define TRIPS_JSONL "/DRIVE/RESOR.JSONL"

// Tillstandsfilen. Den ar det som gor att ett stromavbrott mitt i en resa gar
// att laka: har star var bilen senast var, och att resan aldrig avslutades.
#define STATE_FILE "/DRIVE/PAGAR.BIN"

// Kameror och hastighetsgranser, hamtade fran Trafikverket och lagda hit av
// synken. Saknas filerna fungerar allt annat som vanligt - skarmen sager bara
// att den inte kan varna.
#define CAMS_FILE "/DRIVE/KAMEROR.BIN"
#define LIMITS_FILE "/DRIVE/HASTIGHET.BIN"

// Kundlistan, synkad ner fran webben. Format: id;namn, en per rad.
#define CUSTOMERS_FILE "/DRIVE/KUNDER.CSV"

// Enhetsloggen: en rad per viktig handelse (start, synk, fel). Synken laddar
// upp nya rader till molnet och kommer ihag hur langt den kommit; taket
// haller filen liten aven om enheten aldrig far natkontakt.
#define LOG_FILE "/DRIVE/LOGG.TXT"
#define LOG_MAX_BYTES (128 * 1024)

// ------------------------------------------------------------------ skarm --
// Skarmen slacks efter sa har manga sekunders orordhet. 0 = slacks aldrig.
// Skalan gar i hela minuter fran 1 till 45, tatt i borjan dar skillnaden
// kanns och glesare mot slutet dar den inte gor det.
static const uint16_t kScreenTimeouts[] = {0,   60,   120,  300,
                                           600, 900,  1800, 2700};
static const uint8_t kScreenTimeoutCount = 8;
#define DEFAULT_SCREEN_TIMEOUT_INDEX 3  // 5 min

// Ljudet gar att stanga av, och valet sparas. En reselogg som tjuter nar man
// kor med sovande barn i baksatet blir en reselogg man drar ur.
#define DEFAULT_SOUND_ON 1
// Autosynken: enheten synkar sjalv nar den star stilla och nar natet.
// Avslagen synkar den bara pa knappen pa molnskarmen.
#define DEFAULT_AUTO_SYNC 1

// Matomraden for rorelsesensorn. De behover inte kunna andras i menyn har -
// resan bryr sig inte om dem, och ecodrive rakna i andel av uppmatt tyngdkraft
// och blir riktig oavsett.
#define IMU_ACCEL_RANGE_G 8
#define IMU_GYRO_RANGE_DPS 512

// Avlasningstakt for rorelsesensorn. Ecodrive behover jamna varden, inte
// snabba: 20 Hz ar tio ganger mer an bubblan hinner visa.
#define IMU_SAMPLE_HZ 20

// ------------------------------------------------------------------ wifi ---
// Enhetens eget nat. Uppe nar ingen resa pagar, slackt under fard - se
// websync.h. Losenordet ar inte en hemlighet utan en trappa: utan det kan
// vem som helst pa parkeringen ladda ner dina resor.
#define WIFI_AP_SSID "Hikaya"
#define WIFI_AP_PASSWORD "kordagbok"

// Natet startas forst nagra sekunder efter att resan avslutats, sa att en
// snabb stopp-och-korning inte hinner dra igang wifi i onodan.
#define WIFI_START_DELAY_S 5
