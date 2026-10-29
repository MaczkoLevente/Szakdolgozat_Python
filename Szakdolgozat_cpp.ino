#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>              
#include <BH1750.h>
#include <Adafruit_TCS34725.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <Adafruit_Sensor.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>

//-- Definiált Lábak --
#define DHTPIN 4
#define DHTTYPE DHT11
#define SD_CS_PIN 5
#define GOMB_PIN 14
#define GOMB_WIFI 32
const int PIN_PWM_Piros = 25;
const int PIN_PWM_Kek   = 26;
const int PIN_RELAY_230 = 27;

//-- SoftAP WebSzerver --
const char* AP_Nev    = "ESP32_Vilagitas";
const char* AP_Jelszo = "12345678";
WebServer server(80);
bool apAktiv = false;

//-- Időzítések és mintavételek --
const unsigned long DHT_MINTAVETEL_IDO_MS     = 2000;
unsigned long elozoDHTMeres_ms                 = 0;

const unsigned long INA_MINTAVETEL_IDO_MS     = 500;
unsigned long elozoINAMeres_ms                 = 0;

const unsigned long KIJELZO_FRISSITES_IDO_MS  = 500;
unsigned long elozoKijelzoFrissites_ms         = 0;

unsigned long utolsoGombNyomas_ms             = 0;
const unsigned long PRELL_IDO_MS              = 200;
bool elozoGombAllapot                         = HIGH;

int aktualisOldal                              = 0;
const int OSSZES_OLDAL                         = 7;

//-- PWM és szabályozási paraméterek --
const int PWM_FREQ       = 1000;
const int PWM_Felbontas = 10; // 0 - 1023

const float TS_LUX = 0.40f; // Gyors hurok: 400 ms
const float TS_RB  = 2.00f; // Lassú hurok: 2.0 s

const float MAX_ALPHA_LEPES = 0.06f;
float elozoAlpha            = 0.5f;
float elozoEloszlas         = 0.80f;

uint8_t       aktualisSzakaszIndex = 0;
unsigned long szakaszKezdoIdo_ms   = 0;
unsigned long szakaszNapSzamlalo   = 0;
unsigned long last_control_time    = 0;

const uint16_t TCS_MAX_COUNT_50MS   = 20480;
const float    TCS_SZATURACIO_ARANY = 0.90f;
const uint16_t TCS_SZATURACIO_HATAR = (uint16_t)(TCS_MAX_COUNT_50MS * TCS_SZATURACIO_ARANY);

//-- Szenzorok és kijelző példányosítások --
DHT dht(DHTPIN, DHTTYPE);
BH1750 fenymero;
Adafruit_TCS34725 rgbSzenzor = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_50MS, TCS34725_GAIN_1X);
U8G2_SH1106_128X64_NONAME_F_HW_I2C kijelzo(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

//-- Adatstruktúra --
struct ErzekeloAdatai {
  unsigned long RendszerFutasiIdeje_ms;
  unsigned long DHTUtoljaraOlvasva_ms;
  unsigned long UtolsoINAOlvasas_ms;
  unsigned long UtolsoFenyOlvasas_ms;

  float Homerseklet;
  float Paratartalom;

  float Feszultseg_V;
  float SontFeszultseg_mV;
  float AramFelvetel_mA;
  float TeljesitmenyFelvetel;
  float OsszesEnergia;

  float Lux;

  uint16_t Voros;
  uint16_t Zold;
  uint16_t Kek;
  uint16_t Tiszta;

  float PwmPirosPct;
  float PwmKekPct;
  float Alpha;
  float CelLux;
  float CelRB;
  char SzakaszNev[32];
  uint8_t SzakaszIndex;
  uint16_t SzakaszNap;
  uint16_t SzakaszOsszNap;

  bool DhtVan;
  bool InaVan; // Virtuális jelenlét jelzés
  bool Bh1750Van;
  bool RGBSzenzorVan;
  bool sdKartyaKesz;
};

ErzekeloAdatai AktualisAdatok;

//-- Clampinges PI Szabályozó Osztály --
class PISzabalyozas {
  private:
    float kp, ki, ts;
    float out_min, out_max;
    float integral;

  public:
    PISzabalyozas(float p, float i, float dt, float min_val, float max_val)
      : kp(p), ki(i), ts(dt), out_min(min_val), out_max(max_val), integral(0.0f) {}

    float Szamolas(float setpoint, float meres, float hiszterezis = 0.0f) {
      float elteres = setpoint - meres;

      if (abs(elteres) <= hiszterezis) {
        elteres = 0.0f;
      }

      float p_tag = kp * elteres;
      float uj_integral = integral + (elteres * ts);
      float i_tag = ki * uj_integral;

      float nyers_output = p_tag + i_tag;
      float output = nyers_output;

      if (nyers_output > out_max) {
        output = out_max;
        if (elteres < 0.0f) {
          integral = uj_integral;
        }
      }
      else if (nyers_output < out_min) {
        output = out_min;
        if (elteres > 0.0f) {
          integral = uj_integral;
        }
      }
      else {
        output = nyers_output;
        integral = uj_integral;
      }
   
      return output;
    }

    void reset() {
      integral = 0.0f;
    }
};

PISzabalyozas piLux(0.008f, 0.004f, TS_LUX, 0.0f, 100.0f);
PISzabalyozas piOsszetetel(0.04f, 0.02f, TS_RB, 0.10f, 0.96f);

struct NovenyFejlodesFazis {
  char          nev[32];
  uint16_t      idotartamNap;
  float         celLux;
  float         celRB;
  unsigned long nappalHossz_s;
  unsigned long ejszakaHossz_s;
  unsigned long pirkadatHossz_s;
};

const uint8_t MAX_SZAKASZOK = 5;
NovenyFejlodesFazis szakaszok[MAX_SZAKASZOK] = {
  { "Csirazas",  1,   1500.0f, 0.9f,  10, 10,  5 },
  { "Novekedes", 10, 2000.0f, 0.8f, 820, 10, 30 }
};

uint8_t aktualisSzakaszokSzama = 2;
char aktualisNovenyNev[32] = "Alapertek";

void sdTorlesKezelo() {
  if (SD.exists("/adatok.csv")) {
    SD.remove("/adatok.csv");
    
    File ujFajl = SD.open("/adatok.csv", FILE_WRITE);
    if (ujFajl) {
      ujFajl.println("Ido_ms;Homerseklet_C;Paratartalom_pct;Feszultseg_V;Aram_mA;Energia_Wh;Lux;Voros;Zold;Kek;Tiszta;PWM_Piros_pct;PWM_Kek_pct;Alpha");
      ujFajl.close();
    }
    Serial.println("[AP] adatok.csv sikeresen torolve es ujrainicializalva!");
    server.send(200, "text/plain", "OK: Az adatok.csv torolve es ujra letrehozva.");
  } else {
    server.send(404, "text/plain", "Hiba: A fajl nem letezik, nincs mit torolni.");
  }
}

void ujReceptKezelo() {
  if (!server.hasArg("plain")) {
    server.send(400, "text/plain", "Hiba: Ures keres (Body hianyzik)!");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, server.arg("plain"));

  if (error) {
    Serial.print("[JSON HIBA] Sikertelen parse-olas: ");
    Serial.println(error.f_str());
    server.send(400, "text/plain", "Hiba: Ervenytelen JSON formatum!");
    return;
  }

  JsonArray fazisokTomb = doc["fazisok"].as<JsonArray>();
  if (fazisokTomb.isNull() || fazisokTomb.size() == 0) {
    server.send(400, "text/plain", "Hiba: A 'fazisok' tomb hianyzik vagy ures!");
    return;
  }

  const char* noveny = doc["noveny"] | "Ismeretlen";
  strncpy(aktualisNovenyNev, noveny, sizeof(aktualisNovenyNev) - 1);
  aktualisNovenyNev[sizeof(aktualisNovenyNev) - 1] = '\0';

  uint8_t bejovoDb = fazisokTomb.size();
  if (bejovoDb > MAX_SZAKASZOK) {
    bejovoDb = MAX_SZAKASZOK;
  }

  for (uint8_t i = 0; i < bejovoDb; i++) {
    JsonObject obj = fazisokTomb[i];
    const char* f_nev = obj["nev"] | "Ismeretlen";
    strncpy(szakaszok[i].nev, f_nev, sizeof(szakaszok[i].nev) - 1);
    szakaszok[i].nev[sizeof(szakaszok[i].nev) - 1] = '\0';

    szakaszok[i].idotartamNap    = obj["idotartamNap"] | 1;
    szakaszok[i].celLux         = obj["celLux"] | 500.0f;
    szakaszok[i].celRB          = obj["celRB"] | 1.0f;
    szakaszok[i].nappalHossz_s  = obj["nappalHossz_s"] | 43200UL;
    szakaszok[i].ejszakaHossz_s = obj["ejszakaHossz_s"] | 43200UL;
    szakaszok[i].pirkadatHossz_s= obj["pirkadatHossz_s"] | 1800UL;
  }

  aktualisSzakaszokSzama = bejovoDb;

  aktualisSzakaszIndex = 0;
  szakaszKezdoIdo_ms   = millis();
  szakaszNapSzamlalo   = 0;
  piLux.reset();
  piOsszetetel.reset();
  elozoAlpha = 0.5f;

  Serial.println("\n[RECEPT] Uj fenyrecept sikeresen betoltve!");
  Serial.print("[RECEPT] Fazisok szama: ");
  Serial.println(aktualisSzakaszokSzama);

  server.send(200, "text/plain", "OK: Fenyrecept sikeresen beallitva!");
}

void letoltesKezelo() {
  File dataFile = SD.open("/adatok.csv", FILE_READ);
  if (!dataFile) {
    server.send(404, "text/plain", "Hiba: Az adatok.csv nem talalhato!");
    return;
  }
  server.streamFile(dataFile, "text/csv");
  dataFile.close();
  Serial.println("[AP] Adatok.csv sikeresen elkuldve!");
}

void AP_Inditasa() {
  Serial.println("\n[AP] SoftAP es WebSzerver inditasa...");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_Nev, AP_Jelszo);

  server.on("/letoltes", HTTP_GET, letoltesKezelo);
  server.on("/beallitas", HTTP_POST, ujReceptKezelo);
  server.on("/torles", HTTP_POST, sdTorlesKezelo);
  server.begin();
 
  apAktiv = true;
  Serial.print("[AP] Csatlakozz az ESP32-hoz: ");
  Serial.println(WiFi.softAPIP());
}

void AP_Leallitasa() {
  server.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  apAktiv = false;
  Serial.println("[AP] Wi-Fi kikapcsolva.");
}

void WifiGombKezelo() {
  static unsigned long utolsoGombNyomas = 0;
  static bool elozoGomb = HIGH;

  bool jelenlegiGomb = digitalRead(GOMB_WIFI);

  if (elozoGomb == HIGH && jelenlegiGomb == LOW) {
    if (millis() - utolsoGombNyomas > 500) {
      utolsoGombNyomas = millis();
      if (!apAktiv) {
        AP_Inditasa();
      } else {
        AP_Leallitasa();
      }
    }
  }
  elozoGomb = jelenlegiGomb;

  if (apAktiv) {
    server.handleClient();
  }
}

float SzamitsCelLux(const NovenyFejlodesFazis &szak, unsigned long t_s, bool &nappalVan) {
  if (t_s < szak.nappalHossz_s) {
    nappalVan = true;

    if (t_s < szak.pirkadatHossz_s && szak.pirkadatHossz_s > 0) {
      float arany = (float)t_s / (float)szak.pirkadatHossz_s;
      float envelope = sin(arany * HALF_PI);
      return szak.celLux * envelope;
    }

    unsigned long duskStart = (szak.nappalHossz_s > szak.pirkadatHossz_s)
                                ? (szak.nappalHossz_s - szak.pirkadatHossz_s)
                                : 0;
    if (szak.pirkadatHossz_s > 0 && t_s >= duskStart) {
      unsigned long dt = t_s - duskStart;
      float arany = (float)dt / (float)szak.pirkadatHossz_s;
      arany = constrain(arany, 0.0f, 1.0f);
      float envelope = cos(arany * HALF_PI);
      return szak.celLux * envelope;
    }

    return szak.celLux;
  }
  nappalVan = false;
  return 0.0f;
}

void FrissitsNapotEsSzakaszt(const NovenyFejlodesFazis &szak, unsigned long szakaszOn_s) {
  unsigned long teljesCiklusHossz_s = szak.nappalHossz_s + szak.ejszakaHossz_s;
  if (teljesCiklusHossz_s == 0) return;

  unsigned long napIndex = szakaszOn_s / teljesCiklusHossz_s;

  if (napIndex != szakaszNapSzamlalo) {
    szakaszNapSzamlalo = napIndex;

    if (szakaszNapSzamlalo >= szak.idotartamNap && aktualisSzakaszIndex < aktualisSzakaszokSzama - 1) {
      aktualisSzakaszIndex++;
      szakaszKezdoIdo_ms = millis();
      szakaszNapSzamlalo = 0;

      piLux.reset();
      piOsszetetel.reset();
      elozoAlpha = 0.5f;

      Serial.print("=== UJ NOVEKEDESI SZAKASZ: ");
      Serial.print(szakaszok[aktualisSzakaszIndex].nev);
      Serial.println(" ===");
    }
  }
}

void VilagitasSzabalyozas(unsigned long jelenlegiIdo_ms) {
  NovenyFejlodesFazis &szak = szakaszok[aktualisSzakaszIndex];

  strncpy(AktualisAdatok.SzakaszNev, szak.nev, sizeof(AktualisAdatok.SzakaszNev) - 1);
  AktualisAdatok.SzakaszNev[sizeof(AktualisAdatok.SzakaszNev) - 1] = '\0';
  AktualisAdatok.SzakaszIndex = aktualisSzakaszIndex;
  AktualisAdatok.SzakaszNap = (uint16_t)szakaszNapSzamlalo + 1;
  AktualisAdatok.SzakaszOsszNap = szak.idotartamNap;
  AktualisAdatok.CelRB = szak.celRB;

  if (!AktualisAdatok.Bh1750Van || !AktualisAdatok.RGBSzenzorVan) {
    ledcWrite(PIN_PWM_Piros, 0);
    ledcWrite(PIN_PWM_Kek, 0);
    digitalWrite(PIN_RELAY_230, LOW);
    AktualisAdatok.PwmPirosPct = 0.0f;
    AktualisAdatok.PwmKekPct   = 0.0f;
    AktualisAdatok.Alpha       = elozoAlpha;
    AktualisAdatok.CelLux      = 0.0f;
    return;
  }

  bool nappalVan = false;
  unsigned long szakaszOn_s = (jelenlegiIdo_ms - szakaszKezdoIdo_ms) / 1000UL;

  FrissitsNapotEsSzakaszt(szak, szakaszOn_s);

  unsigned long teljesCiklusHossz_s = szak.nappalHossz_s + szak.ejszakaHossz_s;
  unsigned long t_cikluson_belul_s  = (teljesCiklusHossz_s > 0)
                                          ? (szakaszOn_s % teljesCiklusHossz_s)
                                          : 0;

  float cel_lux   = SzamitsCelLux(szak, t_cikluson_belul_s, nappalVan);
  AktualisAdatok.CelLux = cel_lux;

  float nyers_lux = fenymero.readLightLevel();
  float jelenlegi_Lux = (nyers_lux >= 0.0f) ? nyers_lux : cel_lux;

  uint16_t r, g, b, c;
  rgbSzenzor.getRawData(&r, &g, &b, &c);

  bool szaturalt = (r >= TCS_SZATURACIO_HATAR || g >= TCS_SZATURACIO_HATAR ||
                    b >= TCS_SZATURACIO_HATAR || c >= TCS_SZATURACIO_HATAR);

  // --- NAPPALI ÜZEM ---
  if (nappalVan && cel_lux > 0.0f) {
    digitalWrite(PIN_RELAY_230, HIGH);

    float nyers_osszes_pwm = piLux.Szamolas(cel_lux, jelenlegi_Lux, 10.0f);

    static float szurt_osszes_pwm = 0.0f;
    float pwm_valtozas = constrain(nyers_osszes_pwm - szurt_osszes_pwm, -5.0f, 5.0f);
    szurt_osszes_pwm += pwm_valtozas;
    float osszes_pwm = szurt_osszes_pwm;

    static unsigned long utolsoRBSzabalyozas_ms = 0;
    if (jelenlegiIdo_ms - utolsoRBSzabalyozas_ms >= (unsigned long)(TS_RB * 1000.0f)) {
      utolsoRBSzabalyozas_ms = jelenlegiIdo_ms;

      if (!szaturalt && osszes_pwm > 10.0f && b > 0) {
        float jelenlegi_RB_Eloszlas = (float)r / (float)b;
        float piKimenet = piOsszetetel.Szamolas(szak.celRB, jelenlegi_RB_Eloszlas);
        float delta = constrain(piKimenet - elozoAlpha, -MAX_ALPHA_LEPES, MAX_ALPHA_LEPES);
        elozoAlpha = constrain(elozoAlpha + delta, 0.10f, 0.96f);
        elozoEloszlas = jelenlegi_RB_Eloszlas;
      }
    }

    float r_piros = 1.0f;
    float r_kek   = 1.0f;

    if (elozoAlpha >= 0.5f) {
      r_piros = 1.0f;
      r_kek   = (1.0f - elozoAlpha) / elozoAlpha;
    } else {
      r_piros = elozoAlpha / (1.0f - elozoAlpha);
      r_kek   = 1.0f;
    }

    float pwm_piros = constrain(osszes_pwm * r_piros, 0.0f, 100.0f);
    float pwm_kek   = constrain(osszes_pwm * r_kek, 0.0f, 100.0f);

    int kitoltes_Piros = (int)(pwm_piros * 1023.0f / 100.0f);
    int kitoltes_Kek   = (int)(pwm_kek * 1023.0f / 100.0f);

    ledcWrite(PIN_PWM_Piros, kitoltes_Piros);
    ledcWrite(PIN_PWM_Kek, kitoltes_Kek);

    AktualisAdatok.PwmPirosPct = pwm_piros;
    AktualisAdatok.PwmKekPct   = pwm_kek;
    AktualisAdatok.Alpha       = elozoAlpha;
  }
  // --- ÉJSZAKAI ÜZEM ---
  else {
    ledcWrite(PIN_PWM_Piros, 0);
    ledcWrite(PIN_PWM_Kek, 0);
    digitalWrite(PIN_RELAY_230, LOW);

    piLux.reset();
    piOsszetetel.reset();
    elozoAlpha = 0.5f;

    AktualisAdatok.PwmPirosPct = 0.0f;
    AktualisAdatok.PwmKekPct   = 0.0f;
    AktualisAdatok.Alpha       = 0.5f;
  }
}

//-- Virtuális Szenzor Számítás --
void VirtualisAramMeres() {
  unsigned long most_ms = millis();
  float dt_ora = 0.0f;

  if (AktualisAdatok.UtolsoINAOlvasas_ms > 0) {
    dt_ora = (float)(most_ms - AktualisAdatok.UtolsoINAOlvasas_ms) / 3600000.0f;
  }

  float p_piros = AktualisAdatok.PwmPirosPct;
  float p_kek   = AktualisAdatok.PwmKekPct;

  float aram_kek = 0.0f;
  float pwr_kek  = 0.0f;
  if (p_kek > 0.0f) {
    aram_kek = 13.5f * p_kek;
    pwr_kek  = 138.8f * p_kek;
  }

  float aram_piros = 0.0f;
  float pwr_piros  = 0.0f;
  if (p_piros >= 5.0f) {
    aram_piros = 13.3f * p_piros;
    pwr_piros  = 94.0f * p_piros;
  }

  float aram_feher = 0.0f;
  float pwr_feher  = 0.0f;
  if (digitalRead(PIN_RELAY_230) == HIGH) {
    aram_feher = 40.3f;
    pwr_feher  = 314.6f;
  }

  float ossz_aram = aram_kek + aram_piros + aram_feher;
  float ossz_pwr  = pwr_kek + pwr_piros + pwr_feher;

  float feszultseg = 0.0f;
  if (ossz_aram > 0.0f) {
    feszultseg = ossz_pwr / ossz_aram;
  }

  AktualisAdatok.SontFeszultseg_mV    = 0.0f;
  AktualisAdatok.Feszultseg_V        = feszultseg;
  AktualisAdatok.AramFelvetel_mA      = ossz_aram;
  AktualisAdatok.TeljesitmenyFelvetel = ossz_pwr;
  AktualisAdatok.UtolsoINAOlvasas_ms  = most_ms;

  if (dt_ora > 0.0f && ossz_pwr > 0.0f) {
    AktualisAdatok.OsszesEnergia += (ossz_pwr / 1000.0f) * dt_ora;
  }
}

void AdatokKiirasaSorosra() {
  Serial.println("========================================");
  Serial.print("Uzemido: ");
  Serial.print(AktualisAdatok.RendszerFutasiIdeje_ms / 1000);
  Serial.println(" s");

  Serial.print("Szakasz: ");
  Serial.print(AktualisAdatok.SzakaszNev);
  Serial.print(" (");
  Serial.print(AktualisAdatok.SzakaszIndex + 1);
  Serial.print("/");
  Serial.print(aktualisSzakaszokSzama);
  Serial.print(") | Nap: ");
  Serial.print(AktualisAdatok.SzakaszNap);
  Serial.print("/");
  Serial.print(AktualisAdatok.SzakaszOsszNap);
  Serial.println(" nap");

  Serial.print("Celertekek -> Lux: ");
  Serial.print(AktualisAdatok.CelLux, 1);
  Serial.print(" | R/B: ");
  Serial.println(AktualisAdatok.CelRB, 2);

  Serial.print("Vilagitas -> Kek PWM: ");
  Serial.print(AktualisAdatok.PwmKekPct, 1);
  Serial.print("% | Piros PWM: ");
  Serial.print(AktualisAdatok.PwmPirosPct, 1);
  Serial.print("% | Alpha: ");
  Serial.println(AktualisAdatok.Alpha, 3);

  if (AktualisAdatok.DhtVan) {
    Serial.print("Klima: ");
    Serial.print(AktualisAdatok.Homerseklet, 1);
    Serial.print(" *C | RH: ");
    Serial.print(AktualisAdatok.Paratartalom, 1);
    Serial.println(" %");
  }

  if (AktualisAdatok.InaVan) {
    Serial.print("Tap (Virt): ");
    Serial.print(AktualisAdatok.Feszultseg_V, 2);
    Serial.print(" V | ");
    Serial.print(AktualisAdatok.AramFelvetel_mA, 1);
    Serial.print(" mA | ");
    Serial.print(AktualisAdatok.OsszesEnergia, 3);
    Serial.println(" Wh");
  }

  if (AktualisAdatok.Bh1750Van) {
    Serial.print("Fenyero: ");
    Serial.print(AktualisAdatok.Lux, 1);
    Serial.println(" Lux");
  }

  if (AktualisAdatok.RGBSzenzorVan) {
    float rb = (AktualisAdatok.Kek > 0) ? ((float)AktualisAdatok.Voros / (float)AktualisAdatok.Kek) : 0.0f;
    Serial.print("Spektrum: R=");
    Serial.print(AktualisAdatok.Voros);
    Serial.print(" G=");
    Serial.print(AktualisAdatok.Zold);
    Serial.print(" B=");
    Serial.print(AktualisAdatok.Kek);
    Serial.print(" | R/B arany: ");
    Serial.println(rb, 3);
  }
  Serial.println("========================================\n");
}

void HomersekletMeresMentes() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (isnan(t) || isnan(h)) {
    AktualisAdatok.DhtVan = false;
  } else {
    AktualisAdatok.Homerseklet = t;
    AktualisAdatok.Paratartalom = h;
    AktualisAdatok.DHTUtoljaraOlvasva_ms = millis();
    AktualisAdatok.DhtVan = true;
  }
}

void FenyMeresMentes() {
  if (!AktualisAdatok.Bh1750Van) return;

  float lux = fenymero.readLightLevel();
  if (lux < 0) {
    AktualisAdatok.Bh1750Van = false;
  } else {
    AktualisAdatok.Lux = lux;
    AktualisAdatok.UtolsoFenyOlvasas_ms = millis();
    AktualisAdatok.Bh1750Van = true;
  }
}

void RGBSzenzorMeresMentes() {
  if (!AktualisAdatok.RGBSzenzorVan) return;

  uint16_t r, g, b, c;
  rgbSzenzor.getRawData(&r, &g, &b, &c);

  AktualisAdatok.Voros = r;
  AktualisAdatok.Zold  = g;
  AktualisAdatok.Kek   = b;
  AktualisAdatok.Tiszta = c;
  AktualisAdatok.RGBSzenzorVan = true;
}

void AdatokMenteseSDre() {
  if (!AktualisAdatok.sdKartyaKesz) return;

  File adatFajl = SD.open("/adatok.csv", FILE_APPEND);
  if (adatFajl) {
    adatFajl.print(AktualisAdatok.RendszerFutasiIdeje_ms);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Homerseklet, 1);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Paratartalom, 1);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Feszultseg_V, 2);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.AramFelvetel_mA, 1);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.OsszesEnergia, 4);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Lux, 1);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Voros);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Zold);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Kek);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.Tiszta);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.PwmPirosPct, 1);
    adatFajl.print(";");
    adatFajl.print(AktualisAdatok.PwmKekPct, 1);
    adatFajl.print(";");
    adatFajl.println(AktualisAdatok.Alpha, 3);

    adatFajl.close();
  }
}

void KijelzoFrissites() {
  kijelzo.clearBuffer();

  unsigned long osszesMp = AktualisAdatok.RendszerFutasiIdeje_ms / 1000UL;
  int masodperc = osszesMp % 60;
  int perc      = (osszesMp / 60) % 60;
  int ora       = (osszesMp / 3600) % 24;
  int nap       = osszesMp / 86400UL;

  kijelzo.setFont(u8g2_font_ncenB08_tr);
  char fejlec[32];
  snprintf(fejlec, sizeof(fejlec), "Noveny: %s", aktualisNovenyNev);
  kijelzo.drawStr(0, 10, fejlec);

  switch (aktualisOldal) {
    case 0:
      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.drawStr(0, 30, "UZEMIDO:");
      kijelzo.setCursor(0, 50);
      if (nap > 0) {
        kijelzo.print(nap);
        kijelzo.print(" nap ");
        kijelzo.print(ora);
        kijelzo.print(" ora");
      }
      else if (ora > 0) {
        kijelzo.print(ora);
        kijelzo.print(" ora ");
        kijelzo.print(perc);
        kijelzo.print(" perc");
      }
      else {
        kijelzo.print(perc);
        kijelzo.print(" perc ");
        kijelzo.print(masodperc);
        kijelzo.print(" mp");
      }
      break;

    case 1:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "KLIMA ADATOK:");
      
      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print("T: ");
      kijelzo.print(AktualisAdatok.Homerseklet, 1);
      kijelzo.print(" *C");

      kijelzo.setCursor(0, 60);
      kijelzo.print("RH: ");
      kijelzo.print(AktualisAdatok.Paratartalom, 1);
      kijelzo.print(" %");
      break;

    case 2:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "ENERGIA MONITOR:");

      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print("P: ");
      kijelzo.print(AktualisAdatok.TeljesitmenyFelvetel, 1);
      kijelzo.print(" mW");

      kijelzo.setCursor(0, 60);
      kijelzo.print("W: ");
      kijelzo.print(AktualisAdatok.OsszesEnergia, 3);
      kijelzo.print(" Wh");
      break;

    case 3:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "FENY VISZONYOK:");

      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print("Lux: ");
      kijelzo.print(AktualisAdatok.Lux, 0);

      float rbArany;
      rbArany = (AktualisAdatok.Kek > 0) ? ((float)AktualisAdatok.Voros / (float)AktualisAdatok.Kek) : 0.0f;
      kijelzo.setCursor(0, 60);
      kijelzo.print("R/B: ");
      kijelzo.print(rbArany, 2);
      break;

    case 4:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "PWM VEZERLES:");

      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print("Kek_PWM: ");
      kijelzo.print(AktualisAdatok.PwmKekPct, 1);
      kijelzo.print("%");

      kijelzo.setCursor(0, 60);
      kijelzo.print("Piros_PWM: ");
      kijelzo.print(AktualisAdatok.PwmPirosPct, 1);
      kijelzo.print("%");
      break;

    case 5:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "CELERTEKEK:");

      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print("Cel LUX: ");
      kijelzo.print(AktualisAdatok.CelLux, 0);

      kijelzo.setCursor(0, 60);
      kijelzo.print("Cel R/B: ");
      kijelzo.print(AktualisAdatok.CelRB, 2);
      break;

    case 6:
      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.drawStr(0, 26, "SZAKASZ ALLAPOT:");

      kijelzo.setFont(u8g2_font_helvB10_tr);
      kijelzo.setCursor(0, 42);
      kijelzo.print(AktualisAdatok.SzakaszNev);

      kijelzo.setFont(u8g2_font_ncenB08_tr);
      kijelzo.setCursor(0, 60);
      kijelzo.print("Nap: ");
      kijelzo.print(AktualisAdatok.SzakaszNap);
      kijelzo.print(" / ");
      kijelzo.print(AktualisAdatok.SzakaszOsszNap);
      kijelzo.print(" nap");
      break;
  }

  kijelzo.sendBuffer();
}

void GombKezeles() {
  bool gombMost = digitalRead(GOMB_PIN);

  if (gombMost == LOW && elozoGombAllapot == HIGH) {
    if (millis() - utolsoGombNyomas_ms > PRELL_IDO_MS) {
      aktualisOldal++;
      if (aktualisOldal >= OSSZES_OLDAL) {
        aktualisOldal = 0;
      }
      utolsoGombNyomas_ms = millis();
      KijelzoFrissites();
    }
  }
  elozoGombAllapot = gombMost;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("--- Novenykelteto ESP32 Rendszer Indulasa (Virtualis INA) ---");

  szakaszKezdoIdo_ms = millis();

  Wire.begin(21, 22);
  Wire.setClock(100000);

  Wire1.begin(16, 17);
  Wire1.setClock(25000);

  dht.begin();
  pinMode(GOMB_PIN, INPUT_PULLUP);
  pinMode(GOMB_WIFI, INPUT_PULLUP);
  pinMode(PIN_RELAY_230, OUTPUT);
  digitalWrite(PIN_RELAY_230, LOW);

  AktualisAdatok.DhtVan = false;
  AktualisAdatok.InaVan = true; // Virtuális mérő mindig aktív
  AktualisAdatok.Bh1750Van = false;
  AktualisAdatok.RGBSzenzorVan = false;
  AktualisAdatok.sdKartyaKesz = false;

  if (fenymero.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, 0x23, &Wire1)) {
    AktualisAdatok.Bh1750Van = true;
  }

  if (rgbSzenzor.begin(TCS34725_ADDRESS, &Wire1)) {
    AktualisAdatok.RGBSzenzorVan = true;
  }

  if (SD.begin(SD_CS_PIN)) {
    AktualisAdatok.sdKartyaKesz = true;
    if (!SD.exists("/adatok.csv")) {
      File ujFajl = SD.open("/adatok.csv", FILE_WRITE);
      if (ujFajl) {
        ujFajl.println("Ido_ms;Homerseklet_C;Paratartalom_pct;Feszultseg_V;Aram_mA;Energia_Wh;Lux;Voros;Zold;Kek;Tiszta;PWM_Piros_pct;PWM_Kek_pct;Alpha");
        ujFajl.close();
      }
    }
  }

  ledcAttach(PIN_PWM_Piros, PWM_FREQ, PWM_Felbontas);
  ledcAttach(PIN_PWM_Kek, PWM_FREQ, PWM_Felbontas);
  ledcWrite(PIN_PWM_Piros, 0);
  ledcWrite(PIN_PWM_Kek, 0);  

  HomersekletMeresMentes();
  AktualisAdatok.RendszerFutasiIdeje_ms = millis();
  kijelzo.begin();
}

void loop() {
  unsigned long jelenlegiIdo_ms = millis();
  AktualisAdatok.RendszerFutasiIdeje_ms = jelenlegiIdo_ms;

  // Szabályozási ciklus (400 ms)
  if (jelenlegiIdo_ms - last_control_time >= (unsigned long)(TS_LUX * 1000.0f)) {
    last_control_time = jelenlegiIdo_ms;
    VilagitasSzabalyozas(jelenlegiIdo_ms);
  }

  // Lassú mérések, SD mentés és soros kiírás (2000 ms)
  if (jelenlegiIdo_ms - elozoDHTMeres_ms >= DHT_MINTAVETEL_IDO_MS) {
    elozoDHTMeres_ms = jelenlegiIdo_ms;

    HomersekletMeresMentes();
    FenyMeresMentes();
    RGBSzenzorMeresMentes();
    AdatokMenteseSDre();
    AdatokKiirasaSorosra();
  }

  // Virtuális árammérés és fogyasztásintegrálás (500 ms)
  if (jelenlegiIdo_ms - elozoINAMeres_ms >= INA_MINTAVETEL_IDO_MS) {
    elozoINAMeres_ms = jelenlegiIdo_ms;
    VirtualisAramMeres();
  }

  // Kijelző frissítés (500 ms)
  if (jelenlegiIdo_ms - elozoKijelzoFrissites_ms >= KIJELZO_FRISSITES_IDO_MS) {
    elozoKijelzoFrissites_ms = jelenlegiIdo_ms;
    KijelzoFrissites();
  }  

  WifiGombKezelo();
  GombKezeles();
}