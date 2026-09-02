#include "eco.h"

#include <Preferences.h>
#include <esp_timer.h>
#include <math.h>

#include "config.h"
#include "gnss.h"

namespace {

SemaphoreHandle_t g_mutex = nullptr;
Preferences g_prefs;

EcoStatus g_status = {};

// ---- lodlinjen ------------------------------------------------------------
// Tyngdkraften, framplockad ur accelerationen. Vektorn behaller sin uppmatta
// langd; den ar kortets egen bild av vad 1 g ar, och all vagrat acceleration
// mats som andel av den. En sensor som visar 0,89 g i vila far darmed rätta
// granser anda.
//
// Filtret uppdateras inte under manover: annars skulle en lang kurva sakta
// tolkas som "ned" och bubblan sjunka tillbaka mot mitten fast bilen
// fortfarande ligger i kurvan. Frysningen ar tidsbegransad - se nedan.
float g_gx = 0, g_gy = 0, g_gz = 0;
bool g_haveGravity = false;
bool g_settled = false;

// Hur lange filtret statt fryst i strack, och om taket natts. Utan taket
// ravar frysningen sig sjalv: villkoret raknas fram ur lodlinjen, sa en
// lodlinje som en gang blivit fel haller sig fryst - och darmed fel - for
// alltid.
float g_freezeS = 0;
bool g_freezeCapped = false;

// Foregaende rada avlasning, for att se om vektorn ligger still.
float g_prevAx = 0, g_prevAy = 0, g_prevAz = 0;
bool g_havePrevA = false;

// ---- framatriktningen -----------------------------------------------------
// Enhetsvektor i vagplanet. Lars in genom att jamfora accelerometerns riktning
// med om GPS-farten okar eller minskar.
float g_fx = 0, g_fy = 0, g_fz = 0;
bool g_haveFwd = false;
float g_fwdQuality = 0;
bool g_fastLearn = false;
bool g_fwdSaved = false;

int64_t g_lastUs = 0;
uint32_t g_startMs = 0;

float g_score = 100.0f;
float g_peak = 0;
float g_prevMagG = 0;

bool g_inEvent = false;
float g_overHardS = 0;  // hur lange vardet legat over harda gransen i strack

// Resans medel: poangen integrerad over uppmatt tid. Det leva vardet later
// tva lugna minuter sudda ut vad som helst; medlet glommer ingenting.
double g_scoreIntegral = 0;
double g_measuredS = 0;

// ---- granserna ------------------------------------------------------------
// Ligger i variabler i stallet for i konstanter sa att de gar att andra fran
// gransmenyn medan bilen rullar. Startvardena ar de ur config.h.
float g_softG = ECO_SOFT_G;
float g_hardG = ECO_HARD_G;
float g_clearG = ECO_CLEAR_G;
float g_bubbleG = ECO_BUBBLE_FULL_G;
float g_penalty = ECO_PENALTY_PER_G_S;
// Poang per sekund tillbaka mot hundra. Harleds ur fonstret: en mjuk stracka
// lika lang som fonstret ska racka hela vagen fran noll.
float g_recovery = 100.0f / 120.0f;

// Taran utfors av avlasningstraden, inte av den som trycker pa knappen. Annars
// skulle lodlinjen skrivas fran tva hall samtidigt, mitt i en filtrering.
// Knapptrycket lamnar bara en begaran har och vantar pa svaret.
bool g_tarePending = false;
bool g_tareDone = false;
bool g_tareOk = false;

// GPS-fart, for att kunna skilja gaspadrag fran inbromsning. Bara tva varden
// behovs: mottagaren lamnar en ny fart en gang i sekunden.
float g_speedNow = 0, g_speedPrev = 0;
uint32_t g_speedNowMs = 0, g_speedPrevMs = 0;
bool g_haveSpeed = false;

void lock() {
  if (g_mutex) xSemaphoreTake(g_mutex, portMAX_DELAY);
}
void unlock() {
  if (g_mutex) xSemaphoreGive(g_mutex);
}

void normalize3(float &x, float &y, float &z) {
  const float n = sqrtf(x * x + y * y + z * z);
  if (n > 0.0001f) {
    x /= n;
    y /= n;
    z /= n;
  }
}

// Langsgaende acceleration ur GPS-farten, i g. Noll nar underlaget saknas.
// Tecknet ar det intressanta: plus ar gaspadrag, minus ar inbromsning.
float gpsLongitudinalG() {
  if (!g_haveSpeed || g_speedNowMs == g_speedPrevMs) return 0;
  const float dtS = (float)(g_speedNowMs - g_speedPrevMs) / 1000.0f;
  if (dtS < 0.2f || dtS > 5.0f) return 0;
  const float dvMs = (g_speedNow - g_speedPrev) / 3.6f;
  return (dvMs / dtS) / 9.80665f;
}

void updateSpeed() {
  if (!gnss::present()) {
    g_haveSpeed = false;
    return;
  }
  const GnssFix f = gnss::fix();
  if (!f.valid) {
    g_haveSpeed = false;
    return;
  }

  const uint32_t now = millis();
  if (!g_haveSpeed) {
    g_speedPrev = g_speedNow = f.speedKmh;
    g_speedPrevMs = g_speedNowMs = now;
    g_haveSpeed = true;
    return;
  }
  // Mottagaren lamnar ett nytt varde ungefar en gang i sekunden.
  if (f.speedKmh != g_speedNow || now - g_speedNowMs > 1500) {
    g_speedPrev = g_speedNow;
    g_speedPrevMs = g_speedNowMs;
    g_speedNow = f.speedKmh;
    g_speedNowMs = now;
  }
}

void saveCalibration(bool withForward) {
  g_prefs.begin("ecocal", false);
  g_prefs.putFloat("gx", g_gx);
  g_prefs.putFloat("gy", g_gy);
  g_prefs.putFloat("gz", g_gz);
  g_prefs.putBool("haveG", true);
  if (withForward) {
    g_prefs.putFloat("fx", g_fx);
    g_prefs.putFloat("fy", g_fy);
    g_prefs.putFloat("fz", g_fz);
    g_prefs.putBool("haveF", g_haveFwd);
  }
  g_prefs.end();
}

void loadCalibration() {
  g_prefs.begin("ecocal", true);
  const bool haveG = g_prefs.getBool("haveG", false);
  if (haveG) {
    g_gx = g_prefs.getFloat("gx", 0);
    g_gy = g_prefs.getFloat("gy", 0);
    g_gz = g_prefs.getFloat("gz", 1);
    g_haveGravity = true;
    g_settled = true;
  }
  if (g_prefs.getBool("haveF", false)) {
    g_fx = g_prefs.getFloat("fx", 0);
    g_fy = g_prefs.getFloat("fy", 0);
    g_fz = g_prefs.getFloat("fz", 0);
    g_haveFwd = true;
    // Ett sparat varde far en bra men inte perfekt utgangspunkt, sa att
    // korningen anda korrigerar det om hallaren flyttats.
    g_fwdQuality = 0.8f;
    g_fwdSaved = true;
  }
  g_prefs.end();

  lock();
  g_status.levelStored = haveG;
  unlock();
}

}  // namespace

namespace eco {

void begin() {
  if (g_mutex == nullptr) g_mutex = xSemaphoreCreateMutex();
  g_startMs = millis();
  loadCalibration();
  lock();
  g_status.score = 100.0f;
  // Sa att skarmen har vettiga varden att rita med aven innan installningarna
  // hunnit tillampas. En nolla har hade blivit en division med noll.
  g_status.softG = g_softG;
  g_status.hardG = g_hardG;
  g_status.bubbleG = g_bubbleG;
  unlock();
}

void setLimits(float softG, float hardG, float bubbleG, float penaltyPerGs,
               uint16_t windowS) {
  lock();
  g_softG = softG;
  // Den harda gransen maste ligga over den mjuka, annars skulle ett varde
  // kunna vara bade mjukt och hart samtidigt.
  g_hardG = hardG > softG ? hardG : softG + 0.01f;
  // Avslutning tva tredjedelar ned mot den mjuka gransen. Det ar det glapp
  // som hindrar ett studsande varde fran att raknas som flera handelser.
  g_clearG = g_softG + (g_hardG - g_softG) * 0.67f;
  g_bubbleG = bubbleG;
  g_penalty = penaltyPerGs;
  g_recovery = 100.0f / (windowS > 0 ? (float)windowS : 120.0f);
  g_status.softG = g_softG;
  g_status.hardG = g_hardG;
  g_status.bubbleG = g_bubbleG;
  unlock();
}

void reset() {
  lock();
  g_score = 100.0f;
  g_peak = 0;
  g_inEvent = false;
  g_overHardS = 0;
  g_scoreIntegral = 0;
  g_measuredS = 0;
  g_startMs = millis();
  g_status.score = 100.0f;
  g_status.tripScore = 100.0f;
  g_status.measured = false;
  g_status.measuredS = 0;
  g_status.peakG = 0;
  g_status.hardAccel = 0;
  g_status.hardBrake = 0;
  g_status.hardTurn = 0;
  g_status.hardTotal = 0;
  g_status.elapsedS = 0;
  unlock();
}

void tareRequest() {
  lock();
  g_tareDone = false;
  g_tareOk = false;
  g_tarePending = true;
  unlock();
}

uint8_t tareState() {
  lock();
  uint8_t st = 0;
  if (g_tarePending) {
    st = 1;
  } else if (g_tareDone) {
    st = g_tareOk ? 2 : 3;
    g_tareDone = false;  // kvitterat
  }
  unlock();
  return st;
}

void tareCancel() {
  lock();
  g_tarePending = false;
  g_tareDone = false;
  unlock();
}

void tick(const Sample &s) {
  const int64_t nowUs = esp_timer_get_time();
  if (g_lastUs == 0) {
    g_lastUs = nowUs;
    return;
  }
  float dt = (float)(nowUs - g_lastUs) / 1000000.0f;
  g_lastUs = nowUs;
  // Ett hopp i tiden, t.ex. efter en lang skrivning, far inte dra ivag poangen.
  if (dt <= 0 || dt > 0.5f) dt = 0.5f;

  updateSpeed();
  const float gpsLongG = gpsLongitudinalG();

  // ---- ligger kortet stilla? ----------------------------------------------
  // Det har avgors av gyrot och av att den rada vektorn star still, aldrig av
  // nagot som raknats fram ur lodlinjen. Det ar hela poangen: bedomningen
  // maste komma utifran, annars kan en felaktig lodlinje intyga sin egen
  // riktighet.
  //
  // Gyrot skiljer de tva fall som annars ser likadana ut for accelerometern:
  // en bil i jamn kurva har stor sidoacceleration och tydlig girhastighet,
  // ett kort som ligger snett pa ett bord har lika stor utslag men star helt
  // stilla.
  const float gyroMag =
      sqrtf(s.gx * s.gx + s.gy * s.gy + s.gz * s.gz);
  float jitter = 0;
  if (g_havePrevA) {
    jitter = fabsf(s.ax - g_prevAx) + fabsf(s.ay - g_prevAy) +
             fabsf(s.az - g_prevAz);
  }
  g_prevAx = s.ax;
  g_prevAy = s.ay;
  g_prevAz = s.az;
  const bool atRest = g_havePrevA && gyroMag < ECO_REST_GYRO_DPS &&
                      jitter < ECO_REST_JITTER_G;
  g_havePrevA = true;

  // ---- lodlinjen ----------------------------------------------------------
  if (!g_haveGravity) {
    g_gx = s.ax;
    g_gy = s.ay;
    g_gz = s.az;
    g_haveGravity = true;
  } else {
    // Vardet fran forra varvet duger som matt pa om det pagar en manover -
    // det andras inte sa fort.
    const bool manoeuvring = g_prevMagG > ECO_FREEZE_MAG_G ||
                             fabsf(gpsLongG) > ECO_FREEZE_LONG_G;

    float tau = ECO_GRAVITY_TAU_SLOW_S;
    bool frozen = false;

    if (atRest) {
      // Ligger kortet stilla ar den uppmatta vektorn tyngdkraften, per
      // definition. Da finns ingen anledning till forsiktighet - las in den
      // snabbt. Det ar ocksa detta som gor att man kan flytta hallaren och fa
      // ratt lodlinje inom nagra sekunder utan att rora en knapp.
      tau = ECO_GRAVITY_TAU_FAST_S;
      g_freezeS = 0;
      g_freezeCapped = false;
      g_settled = true;
    } else if (!manoeuvring) {
      g_freezeS = 0;
      g_freezeCapped = false;
      if (!g_settled && millis() - g_startMs > 5000) g_settled = true;
    } else if (!g_freezeCapped) {
      frozen = true;
      g_freezeS += dt;
      if (g_freezeS >= ECO_FREEZE_MAX_S) g_freezeCapped = true;
    }
    // Nar taket natts slapper frysningen och den langsamma tidskonstanten far
    // ta over. Langsam uppdatering ar sammare an ingen alls under en riktig
    // kurva, men oandligt mycket battre an en lodlinje som sitter fast fel.

    if (!frozen) {
      float k = dt / tau;
      if (k > 1.0f) k = 1.0f;
      g_gx += (s.ax - g_gx) * k;
      g_gy += (s.ay - g_gy) * k;
      g_gz += (s.az - g_gz) * k;
    }
  }

  const float gMag = sqrtf(g_gx * g_gx + g_gy * g_gy + g_gz * g_gz);

  // Under fritt fall, eller innan filtret hunnit satta sig, finns ingen
  // palitlig lodlinje och da ar det arligare att inte visa nagot alls.
  if (gMag <= 0.5f) {
    lock();
    g_status.levelled = false;
    unlock();
    return;
  }

  // ---- rakna bort tyngdkraften -------------------------------------------
  // Resultatet uttrycks som andel av den uppmatta tyngdkraften, inte i
  // sensorns absoluta g. Visar sensorn 0,89 g i vila ar 0,89 dess bild av
  // 1 g, och da ska granserna raknas mot 0,89 - annars slar de in for sent i
  // precis den utstrackning sensorn ar felkalibrerad. Kvoten ar dessutom
  // oberoende av matomradet, sa granserna betyder samma sak vid +/-2 g som
  // vid +/-16 g.
  const float dx = g_gx / gMag, dy = g_gy / gMag, dz = g_gz / gMag;
  const float along = s.ax * dx + s.ay * dy + s.az * dz;
  const float inv = 1.0f / gMag;
  const float hx = (s.ax - along * dx) * inv;
  const float hy = (s.ay - along * dy) * inv;
  const float hz = (s.az - along * dz) * inv;
  const float magG = sqrtf(hx * hx + hy * hy + hz * hz);
  g_prevMagG = magG;

  // ---- tara, om nagon bett om det ----------------------------------------
  lock();
  const bool wantTare = g_tarePending;
  unlock();
  if (wantTare) {
    // Vilan bedoms av gyrot, inte av magG. Att fraga magG vore att fraga
    // lodlinjen om lodlinjen ar ratt: har den blivit fel ar magG stort, och
    // da skulle taran - det enda som kan rata till den - vagra kora.
    const bool ok = atRest;
    if (ok) {
      // Vektorn satts till den uppmatta, med langd och allt. Langden ar
      // kortets referens for 1 g och far inte skalas bort.
      g_gx = s.ax;
      g_gy = s.ay;
      g_gz = s.az;
      g_settled = true;

      // Framatriktningen gar inte att lara ut nu - stillastaende finns inget
      // framat att observera. Den nollstalls och lars om under korningen som
      // foljer, vilket ar precis vad man vill nar hallaren just flyttats.
      g_haveFwd = false;
      g_fwdQuality = 0;
      g_fastLearn = true;
      g_fwdSaved = false;

      saveCalibration(false);
    }

    lock();
    g_tarePending = false;
    g_tareDone = true;
    g_tareOk = ok;
    if (ok) g_status.levelStored = true;
    unlock();
  }

  // ---- lar in framatriktningen -------------------------------------------
  // Accelerometern ensam kan aldrig veta vilket hall som ar framat. Farten kan:
  // okar den ar den vagrata accelerationen riktad framat, minskar den ar den
  // riktad bakat. Efter nagra gaspadrag och inbromsningar star riktningen.
  if (g_haveSpeed && fabsf(gpsLongG) > ECO_FWD_MIN_LONG_G &&
      magG > ECO_FWD_MIN_MAG_G) {
    const float sgn = (gpsLongG > 0) ? 1.0f : -1.0f;
    const float ux = sgn * hx / magG;
    const float uy = sgn * hy / magG;
    const float uz = sgn * hz / magG;

    if (!g_haveFwd) {
      g_fx = ux;
      g_fy = uy;
      g_fz = uz;
      g_haveFwd = true;
    } else {
      const float k = g_fastLearn ? ECO_FWD_GAIN_FAST : ECO_FWD_GAIN_SLOW;
      g_fx += (ux - g_fx) * k;
      g_fy += (uy - g_fy) * k;
      g_fz += (uz - g_fz) * k;
    }

    // Riktningen maste ligga i vagplanet, annars skulle den sakta tippa med
    // varje backe den lart sig i.
    const float dot = g_fx * dx + g_fy * dy + g_fz * dz;
    g_fx -= dot * dx;
    g_fy -= dot * dy;
    g_fz -= dot * dz;
    normalize3(g_fx, g_fy, g_fz);

    g_fwdQuality += g_fastLearn ? 0.06f : 0.02f;
    if (g_fwdQuality > 1.0f) g_fwdQuality = 1.0f;
    if (g_fwdQuality >= 1.0f) g_fastLearn = false;

    // Spara en gang nar riktningen satt sig, sa att den finns kvar efter en
    // omstart. Fler skrivningar an sa behover flashminnet inte.
    if (!g_fwdSaved && g_fwdQuality >= 1.0f) {
      g_fwdSaved = true;
      saveCalibration(true);
    }
  }

  // ---- tva axlar i vagplanet, for bubblan --------------------------------
  float lonG, latG;
  const bool oriented = g_haveFwd && g_fwdQuality > 0.2f;
  if (oriented) {
    // Hoger = ned x framat. Da hamnar gas uppat och hogerkurva at hoger pa
    // skarmen, precis som i ett vanligt g-diagram.
    const float rx = dy * g_fz - dz * g_fy;
    const float ry = dz * g_fx - dx * g_fz;
    const float rz = dx * g_fy - dy * g_fx;
    lonG = hx * g_fx + hy * g_fy + hz * g_fz;
    latG = hx * rx + hy * ry + hz * rz;
  } else {
    // Innan riktningen ar inlard duger kortets egna axlar. Storleken stammer,
    // riktningen ar godtycklig - och det sags pa skarmen.
    float bx, by, bz;
    if (fabsf(dx) < 0.9f) {
      bx = 1.0f - dx * dx;
      by = -dx * dy;
      bz = -dx * dz;
    } else {
      bx = -dy * dx;
      by = 1.0f - dy * dy;
      bz = -dy * dz;
    }
    normalize3(bx, by, bz);
    const float cx = dy * bz - dz * by;
    const float cy = dz * bx - dx * bz;
    const float cz = dx * by - dy * bx;
    lonG = hx * bx + hy * by + hz * bz;
    latG = hx * cx + hy * cy + hz * cz;
  }

  // ---- riktningsberoende grans -------------------------------------------
  // En kraftig kurva ar inte samma sak som en kraftig inbromsning. Kurvan
  // kostar lite bransle och ar ofta bara van korning; gas och broms ar precis
  // det ecodrive handlar om. Granserna ar darfor en ellips i stallet for en
  // cirkel: sidled kramas ihop med ECO_LAT_TOLERANCE innan avstandet mats, sa
  // att det kravs sa manga ganger mer kraft at sidan for att na samma grans.
  //
  // Ellipsen forutsatter att framatriktningen ar inlard. Innan dess ar lon och
  // lat godtyckliga axlar pa kortet, och en riktningsberoende grans vore ren
  // slump - da galler cirkeln, precis som forr.
  const float latTol = oriented ? ECO_LAT_TOLERANCE : 1.0f;
  float loadG = magG;
  if (oriented) {
    const float lat = latG / latTol;
    loadG = sqrtf(lonG * lonG + lat * lat);
  }

  // ---- poang --------------------------------------------------------------
  lock();
  const float softG = g_softG, hardG = g_hardG, clearG = g_clearG;
  const float penalty = g_penalty;
  const float recovery = g_recovery;
  unlock();

  if (loadG > softG) {
    g_score -= (loadG - softG) * penalty * dt;
  } else {
    g_score += recovery * dt;
  }
  if (g_score < 0) g_score = 0;
  if (g_score > 100.0f) g_score = 100.0f;

  // Resans medel. Bara uppmatt tid raknas: sekunder utan palitlig lodlinje
  // hamnar varken i taljaren eller namnaren.
  g_scoreIntegral += (double)g_score * dt;
  g_measuredS += dt;

  if (magG > g_peak) g_peak = magG;

  // ---- handelser ----------------------------------------------------------
  // Ett hart moment ar nagot som haller i sig. En riktig inbromsning ligger
  // over gransen i sekunder; ett potthal ger en spik pa nagra hundradelar.
  // Utan tidskravet raknar dagboken vagens skick i stallet for korningen -
  // en verklig resa fick 41 "harda moment" pa tio mil av just det skalet.
  uint32_t addAccel = 0, addBrake = 0, addTurn = 0, addTotal = 0;
  if (loadG >= hardG) {
    g_overHardS += dt;
    if (!g_inEvent && g_overHardS >= ECO_EVENT_MIN_S) {
      g_inEvent = true;
      if (g_haveSpeed) {
        // Farten avgor vad handelsen var. Det kraver ingen kunskap om hur
        // kortet ar vant i bilen, vilket ar hela poangen med att fraga GPS:en
        // i stallet for accelerometern.
        if (gpsLongG > 0.08f) {
          addAccel = 1;
        } else if (gpsLongG < -0.08f) {
          addBrake = 1;
        } else {
          addTurn = 1;
        }
      } else {
        addTotal = 1;
      }
    }
  } else if (loadG < clearG) {
    g_overHardS = 0;
    g_inEvent = false;
  }

  lock();
  g_status.score = g_score;
  g_status.tripScore =
      (g_measuredS > 0.5) ? (float)(g_scoreIntegral / g_measuredS) : 100.0f;
  g_status.measured = g_measuredS >= (double)ECO_MEASURED_MIN_S;
  g_status.measuredS = (uint32_t)g_measuredS;
  g_status.lonG = lonG;
  g_status.latG = latG;
  g_status.magG = magG;
  g_status.loadG = loadG;
  g_status.latTolerance = latTol;
  // Toppen ar en fysisk uppgift och forblir den verkliga accelerationen -
  // "topp 0,42 g" ska betyda 0,42 g, inte 0,42 av nagon ellips.
  g_status.peakG = g_peak;
  g_status.levelled = true;
  g_status.gpsClassify = g_haveSpeed;
  g_status.forwardKnown = oriented;
  g_status.forwardQuality = g_fwdQuality;
  g_status.forwardNeedsGnss = !g_haveSpeed;
  g_status.hardAccel += addAccel;
  g_status.hardBrake += addBrake;
  g_status.hardTurn += addTurn;
  g_status.hardTotal += addTotal;
  g_status.elapsedS = (millis() - g_startMs) / 1000;
  g_status.atRest = atRest;
  g_status.softG = softG;
  g_status.hardG = hardG;
  g_status.bubbleG = g_bubbleG;
  unlock();
}

EcoStatus status() {
  lock();
  EcoStatus s = g_status;
  unlock();
  return s;
}

}  // namespace eco
