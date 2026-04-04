#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <RTClib.h>
// #include <SHT21.h>
#include "WifiCredentials.h"
//#include "SinricProCredentials.h"
#include <time.h>
#include <HTTPClient.h>
#include <List.hpp>
#include <Wire.h>
#include "Adafruit_HTU21DF.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/Org_01.h>
#include <Fonts/TomThumb.h>
#include <SD.h>
#include <SPI.h>
#include <TinyXML2.h>
#include <math.h>
using namespace tinyxml2;

#if __has_include("WeatherCredentials.h")
#include "WeatherCredentials.h"
#endif

#ifndef WEATHER_API_KEY
#define WEATHER_API_KEY ""
#endif

#ifndef WEATHER_CITY
#define WEATHER_CITY ""
#endif

#ifndef WEATHER_UNITS
#define WEATHER_UNITS "metric"
#endif

#ifndef WEATHER_LANG
#define WEATHER_LANG "it"
#endif

//#include "SinricPro.h"
//#include "SinricProSwitch.h"

boolean orologioAttivo = true; // Variabile per controllare se l'orologio è attivo

// Definizioni pin per modulo scheda microSD
#define SD_CS 33
#define MOSI 23
#define MISO 19
#define SCK 18

// Definizioni pin per matrice LED HUB75
#define R1_PIN 25
#define G1_PIN 26
#define B1_PIN 27
#define R2_PIN 14
#define G2_PIN 12
#define B2_PIN 13
#define A_PIN 2
#define B_PIN 32
#define C_PIN 17
#define D_PIN 5
#define E_PIN -1
#define LAT_PIN 4
#define OE_PIN 15
#define CLK_PIN 16

HUB75_I2S_CFG::i2s_pins _pins = {R1_PIN, G1_PIN, B1_PIN, R2_PIN, G2_PIN, B2_PIN, A_PIN, B_PIN, C_PIN, D_PIN, E_PIN, LAT_PIN, OE_PIN, CLK_PIN};

#define PANEL_RES_X 64 // Number of pixels wide of each INDIVIDUAL panel module.
#define PANEL_RES_Y 32 // Number of pixels tall of each INDIVIDUAL panel module.
#define PANEL_CHAIN 1  // Total number of panels chained one to another

// MatrixPanel_I2S_DMA dma_display;
MatrixPanel_I2S_DMA *dma_display = nullptr;

uint16_t myBLACK = dma_display->color565(0, 0, 0);
uint16_t myWHITE = dma_display->color565(255, 255, 255);
uint16_t myRED = dma_display->color565(255, 0, 0);
uint16_t myGREEN = dma_display->color565(0, 255, 0);
uint16_t myBLUE = dma_display->color565(0, 0, 255);
uint16_t myNEWS = dma_display->color565(255, 255, 0);
uint16_t myHOURS = dma_display->color565(255, 105, 180);
uint16_t myMINUTES = dma_display->color565(80, 200, 255);

char lastTimeStr[9] = "00:00:00";
char scrollingText[256] = {0};
int textX = PANEL_RES_X;
const char *newsSeparator = " - ";

RTC_DS3231 rtc;
// SHT21 sht;

// Crea un'istanza del sensore
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

const int LDR_PIN = 34; // Pin analogico a cui è collegato il sensore LDR

char daysOfTheWeek[7][12] = {"Domenica", "Lunedi'", "Martedi'", "Mercoledi'", "Giovedi'", "Venerdi'", "Sabato"};
const char *months[] = {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"};

uint32_t tempBrightnessUpdateInterval = 15000; // 15 secondi
uint32_t scrollingSpeed = 20;

// Imposta la luminosità minima e massima
const int brightnessMin = 30;  // minimo di notte
const int brightnessMax = 220; // massimo di giorno

uint32_t newsUpdateInterval = 1200000; // 20 minuti
uint32_t timeUpdateInterval = 3600000; // 1 ora
uint32_t weatherUpdateInterval = 900000; // 15 minuti
uint32_t topRowToggleInterval = 10000;   // 10 secondi
uint32_t cityResolveRetryInterval = 3600000; // 1 ora

int brightness = brightnessMax;

// const char *rss_feed_url = "https://www.ansa.it/lazio/notizie/lazio_rss.xml";
const char *rss_feed_url = "https://www.repubblica.it/rss/homepage/rss2.0.xml?ref=RHFT";

uint8_t indiceNotizia = 0;

List<String> newsList;

unsigned long lastAttempt = 0;
const long interval = 5000; // Try to reconnect every 5 seconds

enum InfoRowState
{
  INFO_TEMP,
  INFO_DAY,
  INFO_DATE
};
InfoRowState infoRowState = INFO_TEMP;

const uint8_t TIME_AREA_TOP = 0;
const uint8_t TIME_AREA_HEIGHT = 16;
const uint8_t INFO_ROW_Y = 16;
const uint8_t NEWS_ROW_Y = 24;
const uint8_t TIME_LEFT_X = 2;

struct WeatherData
{
  bool valid = false;
  bool isDay = true;
  int temperature = 0;
  int conditionId = 800;
  unsigned long lastUpdateMs = 0;
};

WeatherData weatherData;
bool topRowShowingWeather = false;
bool weatherRenderDirty = true;
String detectedWeatherCity = "";
unsigned long lastCityResolveAttempt = 0;

bool wifiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

bool weatherConfigured()
{
  return strlen(WEATHER_API_KEY) > 0;
}

String urlEncodeSimple(String value)
{
  value.replace(" ", "%20");
  return value;
}

String getEffectiveWeatherCity()
{
  if (strlen(WEATHER_CITY) > 0)
  {
    return String(WEATHER_CITY);
  }

  return detectedWeatherCity;
}

String extractJsonString(const String &json, const char *key, int startPos = 0)
{
  int keyPos = json.indexOf(key, startPos);
  if (keyPos < 0)
    return "";

  keyPos += strlen(key);
  int endPos = json.indexOf('"', keyPos);
  if (endPos < 0)
    return "";

  return json.substring(keyPos, endPos);
}

float extractJsonFloat(const String &json, const char *key, float fallback, int startPos = 0)
{
  int keyPos = json.indexOf(key, startPos);
  if (keyPos < 0)
    return fallback;

  keyPos += strlen(key);
  int endPos = keyPos;
  while (endPos < json.length() && (isDigit(json[endPos]) || json[endPos] == '.' || json[endPos] == '-'))
  {
    endPos++;
  }

  return json.substring(keyPos, endPos).toFloat();
}

int extractJsonInt(const String &json, const char *key, int fallback, int startPos = 0)
{
  return (int)lroundf(extractJsonFloat(json, key, fallback, startPos));
}

bool resolveWeatherCity()
{
  if (!wifiConnected())
  {
    return false;
  }

  String configuredCity = WEATHER_CITY;
  if (configuredCity.length() > 0)
  {
    detectedWeatherCity = configuredCity;
    return true;
  }

  if (detectedWeatherCity.length() > 0)
  {
    return true;
  }

  unsigned long nowMs = millis();
  if (lastCityResolveAttempt != 0 && (nowMs - lastCityResolveAttempt) < cityResolveRetryInterval)
  {
    return false;
  }
  lastCityResolveAttempt = nowMs;

  HTTPClient http;
  String url = "http://ip-api.com/json/?fields=status,message,city,countryCode";
  Serial.println("Rilevo automaticamente la citta'...");
  Serial.println(url);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    String errorPayload = http.getString();
    Serial.printf("Errore HTTP geolocalizzazione: %d\n", httpCode);
    if (errorPayload.length() > 0)
    {
      Serial.println("Dettaglio errore geolocalizzazione:");
      Serial.println(errorPayload);
    }
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  String status = extractJsonString(payload, "\"status\":\"");
  if (status != "success")
  {
    Serial.println("Geolocalizzazione non riuscita.");
    if (payload.length() > 0)
    {
      Serial.println(payload);
    }
    return false;
  }

  String city = extractJsonString(payload, "\"city\":\"");
  String countryCode = extractJsonString(payload, "\"countryCode\":\"");
  if (city.length() == 0)
  {
    Serial.println("Citta' non trovata nella geolocalizzazione.");
    return false;
  }

  detectedWeatherCity = city;
  if (countryCode.length() > 0)
  {
    detectedWeatherCity += "," + countryCode;
  }

  Serial.print("Citta' rilevata automaticamente: ");
  Serial.println(detectedWeatherCity);
  return true;
}

bool fetchWeather()
{
  if (!wifiConnected() || !weatherConfigured())
  {
    return false;
  }

  String effectiveCity = getEffectiveWeatherCity();
  if (effectiveCity.length() == 0 && !resolveWeatherCity())
  {
    Serial.println("Citta' meteo non disponibile.");
    return false;
  }

  effectiveCity = getEffectiveWeatherCity();
  HTTPClient http;
  String cityQuery = urlEncodeSimple(effectiveCity);

  String url = String("http://api.openweathermap.org/data/2.5/weather?q=") +
               cityQuery +
               "&units=" + WEATHER_UNITS +
               "&lang=" + WEATHER_LANG +
               "&appid=" + WEATHER_API_KEY;

  Serial.println("Recupero meteo...");
  Serial.println(url);
  http.begin(url);
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK)
  {
    String errorPayload = http.getString();
    Serial.printf("Errore HTTP meteo: %d\n", httpCode);
    if (errorPayload.length() > 0)
    {
      Serial.println("Dettaglio errore meteo:");
      Serial.println(errorPayload);
    }
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  int weatherArrayPos = payload.indexOf("\"weather\":[");
  int mainPos = payload.indexOf("\"main\":");
  if (weatherArrayPos < 0 || mainPos < 0)
  {
    Serial.println("Risposta meteo non valida.");
    return false;
  }

  String icon = extractJsonString(payload, "\"icon\":\"", weatherArrayPos);
  int conditionId = extractJsonInt(payload, "\"id\":", 800, weatherArrayPos);
  float tempValue = extractJsonFloat(payload, "\"temp\":", 0.0f, mainPos);

  if (icon.length() < 3)
  {
    Serial.println("Icona meteo non trovata.");
    return false;
  }

  weatherData.valid = true;
  weatherData.isDay = icon.endsWith("d");
  weatherData.conditionId = conditionId;
  weatherData.temperature = (int)lroundf(tempValue);
  weatherData.lastUpdateMs = millis();
  weatherRenderDirty = true;

  Serial.printf("Meteo aggiornato: condizione=%d temp=%dC\n", weatherData.conditionId, weatherData.temperature);
  return true;
}

void clearNewsRow()
{
  dma_display->fillRect(0, NEWS_ROW_Y, PANEL_RES_X, 8, 0);
}

bool onPowerState1(const String &deviceId, bool &state)
{
  Serial.printf("Stato orologio: %s\r\n", state ? "on" : "off");
  orologioAttivo = state;
  return true; // request handled properly
}

// setup function for SinricPro
/*
void setupSinricPro()
{
  SinricProSwitch &mySwitch1 = SinricPro[SWITCH_ID_1];
  mySwitch1.onPowerState(onPowerState1);

  // setup SinricPro
  SinricPro.onConnected([]()
                        { Serial.printf("Connected to SinricPro\r\n"); });
  SinricPro.onDisconnected([]()
                           { Serial.printf("Disconnected from SinricPro\r\n"); });
  SinricPro.restoreDeviceStates(true); // Uncomment to restore the last known state from the server.

  SinricPro.begin(APP_KEY, APP_SECRET);
}
*/

// Funzione per scaricare il feed RSS e salvarlo su SD
void scaricaFeed(String url, const char *path)
{
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK)
  {
    Serial.printf("Errore HTTP: %d\n", httpCode);
    http.end();
    return;
  }

  File file = SD.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("Errore apertura file SD!");
    http.end();
    return;
  }

  WiFiClient *stream = http.getStreamPtr();
  uint8_t buffer[512];
  size_t total = 0;

  Serial.println("Scaricamento in corso...");
  while (http.connected())
  {
    size_t len = stream->readBytes(buffer, sizeof(buffer));
    if (len > 0)
    {
      file.write(buffer, len);
      total += len;
    }
    else
      break;
  }

  file.close();
  http.end();
  Serial.printf("Feed salvato su SD (%u byte)\n", (unsigned int)total);
}

// Funzione per ripulire il feed RSS
void cleanupFeed(const char *inputPath, const char *outputPath)
{
  File inFile = SD.open(inputPath, FILE_READ);
  if (!inFile)
  {
    Serial.println("Errore: impossibile aprire il file sorgente.");
    return;
  }

  File tempFile = SD.open(outputPath, FILE_WRITE);
  if (!tempFile)
  {
    Serial.println("Errore: impossibile creare il file di output.");
    inFile.close();
    return;
  }

  bool foundRSS = false;
  bool finishedRSS = false;
  char window[6] = {0}; // buffer scorrevole per cercare pattern

  while (inFile.available())
  {
    char c = inFile.read();

    // Scorri il buffer di 1 posizione e aggiungi il nuovo carattere
    memmove(window, window + 1, 5);
    window[5] = c;

    // --- TROVA INIZIO <rss ---
    if (!foundRSS && strncmp(window + 2, "<rss", 4) == 0)
    {
      foundRSS = true;
      tempFile.print("<rss");
    }

    // --- SE ABBIAMO TROVATO <rss> E NON ABBIAMO FINITO ---
    else if (foundRSS && !finishedRSS)
    {
      tempFile.write(c);

      // Controlla se abbiamo appena scritto la chiusura </rss>
      if (strncmp(window + 1, "rss>", 4) == 0)
      {
        finishedRSS = true;
        break; // stop lettura, ignoriamo tutto dopo </rss>
      }
    }
  }

  inFile.close();
  tempFile.close();

  if (foundRSS && finishedRSS)
  {
    Serial.println("Feed ripulito: salvato solo da <rss> a </rss>.");
  }
  else if (!foundRSS)
  {
    Serial.println("Tag <rss> non trovato nel file.");
  }
  else if (!finishedRSS)
  {
    Serial.println("Tag di chiusura </rss> non trovato (file incompleto).");
  }

  if (SD.remove("/feed.xml"))
  {
    Serial.println("File feed.xml cancellato con successo!");
  }
  else
  {
    Serial.println("Errore: impossibile cancellare feed.xml (forse non esiste?)");
  }
}

// Funzione per rimuovere gli accenti dalle lettere
void rimuoviAccenti(char *cdataBuffer)
{
  for (int i = 0; cdataBuffer[i] != '\0'; ++i)
  {
    unsigned char c = (unsigned char)cdataBuffer[i];
    if (c == 0xC3)
    { // In UTF-8, accenti iniziano con 0xC3
      unsigned char next = (unsigned char)cdataBuffer[i + 1];
      switch (next)
      {
      case 0xA0:
        cdataBuffer[i] = 'a';
        cdataBuffer[i + 1] = '\'';
        break; // à
      case 0xA8:
        cdataBuffer[i] = 'e';
        cdataBuffer[i + 1] = '\'';
        break; // è
      case 0xA9:
        cdataBuffer[i] = 'e';
        cdataBuffer[i + 1] = '\'';
        break; // é
      case 0xAC:
        cdataBuffer[i] = 'i';
        cdataBuffer[i + 1] = '\'';
        break; // ì
      case 0xB2:
        cdataBuffer[i] = 'o';
        cdataBuffer[i + 1] = '\'';
        break; // ò
      case 0xB9:
        cdataBuffer[i] = 'u';
        cdataBuffer[i + 1] = '\'';
        break; // ù
      case 0x80:
        cdataBuffer[i] = 'A';
        cdataBuffer[i + 1] = '\'';
        break; // À
      case 0x88:
        cdataBuffer[i] = 'E';
        cdataBuffer[i + 1] = '\'';
        break; // È
      case 0x8C:
        cdataBuffer[i] = 'I';
        cdataBuffer[i + 1] = '\'';
        break; // Ì
      case 0x92:
        cdataBuffer[i] = 'O';
        cdataBuffer[i + 1] = '\'';
        break; // Ò
      case 0x99:
        cdataBuffer[i] = 'U';
        cdataBuffer[i + 1] = '\'';
        break; // Ù
      default:
        break;
      }
    }
    // Gestione virgolette tipografiche “ e ”
    else if (c == 0xE2)
    {
      unsigned char next1 = (unsigned char)cdataBuffer[i + 1];
      unsigned char next2 = (unsigned char)cdataBuffer[i + 2];

      // “ (E2 80 9C) oppure ” (E2 80 9D)
      if (next1 == 0x80 && (next2 == 0x9C || next2 == 0x9D))
      {
        cdataBuffer[i] = '"';
        // Rimuovi i due byte successivi spostando la stringa a sinistra
        memmove(&cdataBuffer[i + 1], &cdataBuffer[i + 3], strlen(&cdataBuffer[i + 3]) + 1);
      }
      // ’ (apostrofo tipografico, E2 80 99)
      else if (next1 == 0x80 && next2 == 0x99)
      {
        cdataBuffer[i] = '\'';
        memmove(&cdataBuffer[i + 1], &cdataBuffer[i + 3], strlen(&cdataBuffer[i + 3]) + 1);
      }
    }
  }
}

// Funzione per il parsing RSS con TinyXML2
boolean parseRSS(const char *path)
{
  File file = SD.open(path, FILE_READ);
  if (!file)
  {
    Serial.println("Errore apertura file RSS!");
    return false;
  }

  // Legge tutto il file in un buffer (necessario per TinyXML2)
  String xmlContent;
  while (file.available())
  {
    xmlContent += (char)file.read();
  }
  file.close();

  XMLDocument doc;
  XMLError e = doc.Parse(xmlContent.c_str());
  if (e != XML_SUCCESS)
  {
    Serial.print("Errore parsing XML: ");
    Serial.println(e);
    return false;
  }

  newsList.removeAll(); // Cancella la lista delle notizie
  indiceNotizia = 0;    // Resetta l'indice della notizia

  // Ottieni nodo radice <rss>
  XMLElement *rss = doc.FirstChildElement("rss");
  if (!rss)
  {
    Serial.println("Tag <rss> non trovato!");
    return false;
  }

  // Trova <channel>
  XMLElement *channel = rss->FirstChildElement("channel");
  if (!channel)
  {
    Serial.println("Tag <channel> non trovato!");
    return false;
  }

  // --- Leggi info generali ---
  const char *title = channel->FirstChildElement("title") ? channel->FirstChildElement("title")->GetText() : "";
  const char *link = channel->FirstChildElement("link") ? channel->FirstChildElement("link")->GetText() : "";
  const char *desc = channel->FirstChildElement("description") ? channel->FirstChildElement("description")->GetText() : "";

  /*
  Serial.println("=== RSS CHANNEL ===");
  Serial.printf("Titolo: %s\n", title);
  Serial.printf("Link:   %s\n", link);
  Serial.printf("Descr:  %s\n\n", desc);
  */

  // --- Leggi tutti gli <item> ---
  for (XMLElement *item = channel->FirstChildElement("item");
       item != nullptr;
       item = item->NextSiblingElement("item"))
  {

    const char *itemTitle = item->FirstChildElement("title") ? item->FirstChildElement("title")->GetText() : "";
    const char *itemLink = item->FirstChildElement("link") ? item->FirstChildElement("link")->GetText() : "";
    const char *itemDesc = item->FirstChildElement("description") ? item->FirstChildElement("description")->GetText() : "";

    // Serial.println("--- ITEM ---");
    Serial.printf("Titolo: %s\n", itemTitle);
    // Converte in String per sicurezza
    String titolo = String(itemTitle);

    // Crea un buffer temporaneo (lunghezza dinamica)
    char buffer[titolo.length() + 1];
    titolo.toCharArray(buffer, sizeof(buffer));

    // Rimuove accenti
    rimuoviAccenti(buffer);
    // Aggiunge la notizia alla lista (convertendo di nuovo in String)
    newsList.add(String(buffer));

    // Serial.printf("Link:   %s\n", itemLink);
    // Serial.printf("Descr:  %s\n\n", itemDesc);
  }
  return true;
}

String leftPad(int number, int totalLength)
{
  // Convertiamo il numero in stringa
  String str = String(number);

  // Calcoliamo quanto padding è necessario
  int paddingNeeded = totalLength - str.length();

  // Aggiungiamo zeri a sinistra
  for (int i = 0; i < paddingNeeded; i++)
  {
    str = "0" + str; // Aggiungiamo uno zero a sinistra
  }

  return str;
}

// Calcola i pixel a sinistra per centrare una stringa
int centraStringa(String str)
{
  unsigned int stringLength = str.length();
  int leftSpace = (int)((64 - (6 * stringLength)) / 2);
  return leftSpace;
}

void syncRTCwithNTP()
{
  struct tm timeinfo;

  if (getLocalTime(&timeinfo))
  {

    // Converte struct tm in oggetto DateTime
    DateTime dt(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec);

    // Scrive l'orario sull'RTC
    // rtc.begin();
    rtc.adjust(dt);

    Serial.print("RTC updated to: ");
    Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S");
  }
}

void stampaOra()
{
  DateTime now = rtc.now();
  Serial.print("Ora: ");
  Serial.print((now.hour() < 10 ? "0" : "") + String(now.hour(), DEC));
  Serial.print(":");
  Serial.print((now.minute() < 10 ? "0" : "") + String(now.minute(), DEC));
  Serial.print(":");
  Serial.println((now.second() < 10 ? "0" : "") + String(now.second(), DEC));
}

String readTemperatureAndHumidity()
{
  static u_int8_t count = 0;

  float temperature = htu.readTemperature(); // Legge la temperatura
  float humidity = htu.readHumidity();       // Legge l'umidità

  // Controlla se ci sono errori nella lettura
  if (isnan(temperature) || isnan(humidity))
  {
    Serial.println("Errore nella lettura del sensore!");
    return "";
  }

  // Formatta i valori float in una stringa
  String bufferTemp;
  if (count == 0)
  {
    bufferTemp = "Tmp:" + (String)temperature + "C";
  }
  else
  {
    bufferTemp = "Umd:" + (String)humidity + "%";
  }

  count = (count + 1) % 2;

  return bufferTemp;
}

void visualizzaInfoRiga(DateTime now)
{
  dma_display->fillRect(0, INFO_ROW_Y, PANEL_RES_X, 8, 0);
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);

  if (infoRowState == INFO_TEMP)
  {
    dma_display->setCursor(2, INFO_ROW_Y);
    dma_display->setTextColor(dma_display->color565(255, 0, 0));
    dma_display->print(readTemperatureAndHumidity());
  }
  else if (infoRowState == INFO_DAY)
  {
    int leftSpace = centraStringa((String)daysOfTheWeek[now.dayOfTheWeek()]);
    dma_display->setCursor(leftSpace, INFO_ROW_Y);
    dma_display->setTextColor(dma_display->color565(60, 180, 60));
    dma_display->print(daysOfTheWeek[now.dayOfTheWeek()]);
  }
  else
  {
    int leftSpace = 3;
    dma_display->setCursor(leftSpace, INFO_ROW_Y);
    dma_display->setTextColor(dma_display->color565(60, 180, 60));
    dma_display->print(leftPad(now.day(), 2));
    dma_display->setCursor(leftSpace + 14, INFO_ROW_Y);
    dma_display->print(months[now.month() - 1]);
    dma_display->setCursor(leftSpace + 34, INFO_ROW_Y);
    dma_display->print(now.year());
  }
}

void drawSunIcon(int x, int y)
{
  uint16_t sunColor = dma_display->color565(255, 210, 40);
  dma_display->fillCircle(x + 8, y + 8, 4, sunColor);
  for (int i = 0; i < 8; i++)
  {
    float angle = i * PI / 4.0f;
    int x1 = x + 8 + (int)lroundf(cos(angle) * 6);
    int y1 = y + 8 + (int)lroundf(sin(angle) * 6);
    int x2 = x + 8 + (int)lroundf(cos(angle) * 8);
    int y2 = y + 8 + (int)lroundf(sin(angle) * 8);
    dma_display->drawLine(x1, y1, x2, y2, sunColor);
  }
}

void drawMoonIcon(int x, int y)
{
  uint16_t moonColor = dma_display->color565(255, 230, 180);
  dma_display->fillCircle(x + 8, y + 8, 5, moonColor);
  dma_display->fillCircle(x + 10, y + 6, 5, myBLACK);
}

void drawCloudIcon(int x, int y, uint16_t cloudColor)
{
  dma_display->fillCircle(x + 5, y + 9, 3, cloudColor);
  dma_display->fillCircle(x + 9, y + 7, 4, cloudColor);
  dma_display->fillCircle(x + 13, y + 9, 3, cloudColor);
  dma_display->fillRect(x + 4, y + 9, 10, 4, cloudColor);
}

void drawRainDrops(int x, int y, uint16_t rainColor)
{
  dma_display->drawLine(x + 5, y + 12, x + 4, y + 14, rainColor);
  dma_display->drawLine(x + 9, y + 12, x + 8, y + 15, rainColor);
  dma_display->drawLine(x + 13, y + 12, x + 12, y + 14, rainColor);
}

void drawSnowFlakes(int x, int y, uint16_t snowColor)
{
  for (int i = 0; i < 3; i++)
  {
    int cx = x + 5 + i * 4;
    int cy = y + 13;
    dma_display->drawPixel(cx, cy, snowColor);
    dma_display->drawPixel(cx - 1, cy, snowColor);
    dma_display->drawPixel(cx + 1, cy, snowColor);
    dma_display->drawPixel(cx, cy - 1, snowColor);
    dma_display->drawPixel(cx, cy + 1, snowColor);
  }
}

void drawLightning(int x, int y, uint16_t color)
{
  dma_display->drawLine(x + 9, y + 10, x + 7, y + 13, color);
  dma_display->drawLine(x + 7, y + 13, x + 10, y + 13, color);
  dma_display->drawLine(x + 10, y + 13, x + 8, y + 15, color);
}

void drawFog(int x, int y, uint16_t color)
{
  dma_display->drawLine(x + 3, y + 8, x + 13, y + 8, color);
  dma_display->drawLine(x + 2, y + 11, x + 12, y + 11, color);
  dma_display->drawLine(x + 4, y + 14, x + 14, y + 14, color);
}

void drawWeatherIcon(int x, int y)
{
  uint16_t cloudColor = dma_display->color565(190, 190, 190);
  uint16_t rainColor = dma_display->color565(80, 180, 255);
  uint16_t fogColor = dma_display->color565(150, 150, 150);
  uint16_t lightningColor = dma_display->color565(255, 220, 0);
  uint16_t snowColor = dma_display->color565(230, 230, 255);

  int id = weatherData.conditionId;

  if (id == 800)
  {
    if (weatherData.isDay)
      drawSunIcon(x, y);
    else
      drawMoonIcon(x, y);
    return;
  }

  if (id >= 801 && id <= 804)
  {
    if (weatherData.isDay)
      drawSunIcon(x - 2, y - 1);
    else
      drawMoonIcon(x - 1, y - 1);
    drawCloudIcon(x + 1, y + 2, cloudColor);
    return;
  }

  drawCloudIcon(x, y + 1, cloudColor);

  if (id >= 200 && id < 300)
  {
    drawLightning(x, y, lightningColor);
    drawRainDrops(x, y, rainColor);
  }
  else if (id >= 300 && id < 600)
  {
    drawRainDrops(x, y, rainColor);
  }
  else if (id >= 600 && id < 700)
  {
    drawSnowFlakes(x, y, snowColor);
  }
  else if (id >= 700 && id < 800)
  {
    drawFog(x, y, fogColor);
  }
}

void visualizzaMeteoGrande()
{
  dma_display->fillRect(0, TIME_AREA_TOP, PANEL_RES_X, TIME_AREA_HEIGHT, 0);

  if (!weatherData.valid)
  {
    dma_display->setFont(&Org_01);
    dma_display->setTextColor(myWHITE);
    dma_display->setCursor(5, 10);
    dma_display->print("meteo n/d");
    dma_display->setFont();
    weatherRenderDirty = false;
    return;
  }

  drawWeatherIcon(4, 0);

  String tempLabel = String(weatherData.temperature) + "C";
  int16_t x1, y1;
  uint16_t w, h;
  dma_display->setFont(&FreeSans9pt7b);
  dma_display->getTextBounds(tempLabel, 0, 0, &x1, &y1, &w, &h);

  int textX = 23;
  if (textX + w > PANEL_RES_X)
  {
    dma_display->setFont(&TomThumb);
    textX = 22;
  }

  dma_display->setTextColor(dma_display->color565(180, 220, 255));
  dma_display->setCursor(textX, 13);
  dma_display->print(tempLabel);
  dma_display->setFont();
  weatherRenderDirty = false;
}

void visualizzaOraGrande(const String &hours, const String &minutes, const String &seconds)
{
  String hh = hours;
  String mm = minutes;
  String ss = seconds;

  int16_t x1, y1, x2, y2;
  uint16_t w1, h1, w2, h2;

  dma_display->setFont(&FreeSansBold9pt7b);
  dma_display->getTextBounds("88:88", 0, 0, &x1, &y1, &w1, &h1);

  dma_display->setFont(&Org_01);
  dma_display->getTextBounds(":88", 0, 0, &x2, &y2, &w2, &h2);

  int startX = TIME_LEFT_X;
  int ascent1 = -y1;
  int ascent2 = -y2;
  int descent1 = (int)h1 - ascent1;
  int descent2 = (int)h2 - ascent2;
  int totalHeight = max(ascent1, ascent2) + max(descent1, descent2);
  int baseline = TIME_AREA_TOP + (TIME_AREA_HEIGHT - totalHeight) / 2 + max(ascent1, ascent2);

  dma_display->fillRect(0, TIME_AREA_TOP, PANEL_RES_X, TIME_AREA_HEIGHT, 0);

  bool blinkOn = (seconds.toInt() % 2) == 0;
  uint16_t yellow = dma_display->color565(255, 255, 0);
  uint16_t lightBlue = dma_display->color565(180, 220, 255);

  int x = startX;

  dma_display->setFont(&FreeSansBold9pt7b);
  dma_display->setTextColor(myHOURS);
  dma_display->setCursor(x, baseline);
  dma_display->print(hh);
  int16_t bx, by;
  uint16_t bw, bh;
  dma_display->getTextBounds(hh, 0, 0, &bx, &by, &bw, &bh);
  x += bw;

  x += 2;
  dma_display->setTextColor(yellow);
  dma_display->setCursor(x, baseline);
  dma_display->print(":");
  dma_display->getTextBounds(":", 0, 0, &bx, &by, &bw, &bh);
  x += bw + 1;

  dma_display->setTextColor(myMINUTES);
  dma_display->setCursor(x, baseline);
  dma_display->print(mm);
  dma_display->getTextBounds(mm, 0, 0, &bx, &by, &bw, &bh);
  x += bw + 2;

  dma_display->setFont(&TomThumb);
  dma_display->setTextSize(1);
  x += 1;
  dma_display->setTextColor(blinkOn ? lightBlue : myBLACK);
  dma_display->setCursor(x, baseline);
  dma_display->print(":");
  dma_display->getTextBounds(":", 0, 0, &bx, &by, &bw, &bh);
  x += bw + 1;

  dma_display->setTextColor(lightBlue);
  dma_display->setCursor(x, baseline);
  dma_display->print(ss);

  dma_display->setFont(); // reset to default
  dma_display->setTextSize(1);
  dma_display->setTextWrap(false);
}

// Funzione per rimuovere duplicati da una lista di String
void rimuoveDuplicati(List<String> &list)
{
  for (int i = 0; i < list.getSize(); i++)
  {
    String current = list.get(i);

    // Controlla tutti gli elementi successivi
    for (int j = i + 1; j < list.getSize(); j++)
    {
      if (list.get(j) == current)
      {
        Serial.println("Rimuovo duplicato: " + current);
        list.remove(j);
        j--; // importantissimo: la lista si compatta
      }
    }
  }
}

// Funzione per scaricare, pulire e fare il parsing del feed RSS
void fetchRSSFeed()
{
  if (!wifiConnected())
  {
    Serial.println("WiFi non disponibile: salto aggiornamento news.");
    return;
  }

  Serial.println("Recupero feed RSS...");
  scaricaFeed(rss_feed_url, "/feed.xml");
  cleanupFeed("/feed.xml", "/feed_clean.xml");
  if (parseRSS("/feed_clean.xml"))
  {
    newsUpdateInterval = 1200000; // Ripristina l'intervallo normale in caso di successo
    rimuoveDuplicati(newsList);   // Rimuove duplicati
  }
  else
  {
    newsUpdateInterval = 120000; // Riduci l'intervallo in caso di errore
  }
}

bool waitForWiFiConnection(unsigned long timeoutMs)
{
  unsigned long start = millis();
  while (!wifiConnected() && (millis() - start < timeoutMs))
  {
    Serial.print(".");
    delay(500);
  }

  return wifiConnected();
}

void setup()
{
  Serial.begin(115200);
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Motivo Reset: ");
  switch (reason)
  {
  case ESP_RST_POWERON:
    Serial.println("Power-on reset");
    break;
  case ESP_RST_EXT:
    Serial.println("External reset");
    break;
  case ESP_RST_SW:
    Serial.println("Software reset");
    break;
  case ESP_RST_PANIC:
    Serial.println("Exception/panic reset");
    break;
  case ESP_RST_INT_WDT:
    Serial.println("Interrupt watchdog reset");
    break;
  case ESP_RST_TASK_WDT:
    Serial.println("Task watchdog reset");
    break;
  case ESP_RST_WDT:
    Serial.println("Other watchdog reset");
    break;
  case ESP_RST_DEEPSLEEP:
    Serial.println("Wake from deep sleep");
    break;
  default:
    Serial.println("Unknown reason");
    break;
  }

  SPI.begin(18, 19, 23, SD_CS); // SCK = 18, MISO = 19, MOSI = 23
  // Configura i pin fisici

  if (!SD.begin(SD_CS))
  {
    Serial.println("Errore inizializzazione scheda SD");
    return;
  }
  Serial.println("Scheda SD inizializzata.");

  // Connessione al WiFi
  Serial.println("In attesa di connettersi al WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  if (waitForWiFiConnection(10000))
  {
    Serial.println("\nConnesso al WiFi");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());

    delay(1000);      // Attendi 1 secondo per stabilizzare la connessione
    //setupSinricPro(); // Inizializza SinricPro dopo la connessione WiFi
  }
  else
  {
    Serial.println("\nNon connesso al WiFi");
  }

  // Inizializza I2C (puoi cambiare i pin se necessario)
  Wire.begin(21, 22); // SDA = 21, SCL = 22

  // Inizializza il sensore
  if (!htu.begin())
  {
    Serial.println("Errore: HTU21D non trovato!");
    while (1)
      ;
  }

  Serial.println("HTU21D inizializzato correttamente");

  // Imposta il fuso orario italiano con ora legale
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org");

  // Avvia il client NTP
  // timeClient.begin();

  pinMode(LDR_PIN, INPUT); // Imposta il pin LDR come ingresso

  // initializing the rtc
  if (!rtc.begin())
  {
    Serial.println("Couldn't find RTC!");
    Serial.flush();
    // while (1) delay(10);
    delay(3000);
    ESP.restart(); // Riavvia il dispositivo
  }

  if (rtc.lostPower())
  {
    // this will adjust to the date and time at compilation
    // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    Serial.println("RTC lost power, setting default time...");
  }

  // we don't need the 32K Pin, so disable it
  rtc.disable32K();

  // Imposta il modulo DS3231 per generare un segnale SQW di 1Hz (necessario per rilevare l'inizio di ogni ora)
  rtc.writeSqwPinMode(DS3231_SquareWave1Hz);
  Serial.println("Modulo RTC DS3231 configurato.");

  syncRTCwithNTP(); // Sincronizza all'avvio

  // Module configuration
  HUB75_I2S_CFG mxconfig(
      PANEL_RES_X, // module width
      PANEL_RES_Y, // module height
      PANEL_CHAIN, // Chain length
      _pins);

  //  ::SHIFTREG; // Tipico per pannelli HUB75
  mxconfig.driver = HUB75_I2S_CFG::FM6126A;

  // Display Setup
  dma_display = new MatrixPanel_I2S_DMA(mxconfig);
  // dma_display->begin();

  // Allocate memory and start DMA display
  if (not dma_display->begin())
    Serial.println("****** !KABOOM! HUB75 memory allocation failed ***********");
  dma_display->setBrightness8(brightness); // 0-255
  dma_display->clearScreen();
  dma_display->fillScreen(myBLACK);

  if (wifiConnected())
  {
    fetchRSSFeed();
    fetchWeather();
  }
  else
  {
    clearNewsRow();
  }

  visualizzaInfoRiga(rtc.now());
}

void gestisciOrologio()
{

  DateTime now = rtc.now();
  String hours = (now.hour() < 10 ? "0" : "") + String(now.hour(), DEC);
  String minutes = (now.minute() < 10 ? "0" : "") + String(now.minute(), DEC);
  String seconds = (now.second() < 10 ? "0" : "") + String(now.second(), DEC);

  char timeStr[9];
  sprintf(timeStr, "%s:%s:%s", hours, minutes, seconds);

  bool shouldShowWeather = weatherConfigured() && weatherData.valid && ((millis() / topRowToggleInterval) % 2 == 1);

  if (shouldShowWeather)
  {
    if (!topRowShowingWeather || weatherRenderDirty)
    {
      visualizzaMeteoGrande();
      topRowShowingWeather = true;
    }
  }
  else if ((strcmp(timeStr, lastTimeStr) != 0) || topRowShowingWeather)
  {
    visualizzaOraGrande(hours, minutes, seconds);
    strcpy(lastTimeStr, timeStr);
    topRowShowingWeather = false;
  }

  // Reload news list
  static unsigned long lastUpdateNewsTime = 0;
  if (millis() > lastUpdateNewsTime + newsUpdateInterval)
  {
    Serial.println("Reload news...");
    lastUpdateNewsTime = millis();
    indiceNotizia = 0;
    textX = PANEL_RES_X;
    // fetchRSSFeed();
  }

  static unsigned long lastUpdateDisplay = 0;
  if (!wifiConnected() || (newsList.getSize() == 0))
  {
    clearNewsRow();
  }
  else if (millis() > lastUpdateDisplay + scrollingSpeed)
  {
    lastUpdateDisplay = millis();
    String notizia = newsList.get(indiceNotizia);
    String notiziaSuccessiva = newsList.get((indiceNotizia + 1) % newsList.getSize());
    String testoScorrevole = notizia + newsSeparator + notiziaSuccessiva;
    if (testoScorrevole.length() >= (int)sizeof(scrollingText))
    {
      testoScorrevole.remove(sizeof(scrollingText) - 1);
    }
    testoScorrevole.toCharArray(scrollingText, sizeof(scrollingText));
    clearNewsRow();
    dma_display->setFont();
    dma_display->setTextSize(1);
    dma_display->setCursor(textX, NEWS_ROW_Y);
    dma_display->setTextColor(myNEWS);
    dma_display->print(scrollingText);
    // dma_display->show();

    textX--;
    int lunghezzaBloccoCorrente = (notizia.length() + strlen(newsSeparator)) * 6;
    int limiteNegativo = 0 - lunghezzaBloccoCorrente;
    if (textX < limiteNegativo)
    {
      // Serial.print("Visualizzo la notizia numero ");
      // Serial.println(indiceNotizia);
      textX += lunghezzaBloccoCorrente;
      indiceNotizia = (indiceNotizia + 1) % newsList.getSize();
    }
  }

  // Aggiorna la luminosità e la temperatura ogni 30 secondi
  static unsigned long lastUpdateTime = 0;
  if (millis() > lastUpdateTime + tempBrightnessUpdateInterval)
  {
    lastUpdateTime = millis();

    int ldrValue = analogRead(LDR_PIN); // Legge il valore analogico dal sensore LDR
    // Serial.print("LDR value: ");
    // Serial.println(ldrValue); // Stampa il valore letto nel monitor seriale

    // Adatta questi range in base al tuo ambiente reale
    brightness = map(ldrValue, 4000, 100, brightnessMin, brightnessMax);
    brightness = constrain(brightness, brightnessMin, brightnessMax);
    dma_display->setBrightness8(brightness); // 0-255

    // syncRTCwithNTP();
  }

  // Alterno contenuti della riga info
  static unsigned long lastUpdateInfoRow = 0;
  if (millis() > lastUpdateInfoRow + 5000)
  {
    visualizzaInfoRiga(now);
    lastUpdateInfoRow = millis();
    if (infoRowState == INFO_TEMP)
      infoRowState = INFO_DAY;
    else if (infoRowState == INFO_DAY)
      infoRowState = INFO_DATE;
    else
      infoRowState = INFO_TEMP;
  }
}

void loop()
{
  delay(1); // Piccola pausa per evitare blocchi
  static boolean nextState = true;
  static unsigned long lastUpdateNews = 0;

  if (!wifiConnected())
  {
    if (millis() - lastAttempt >= interval)
    {
      Serial.println("WiFi disconnesso. In attesa di riconnessione...");
      clearNewsRow();
      newsList.removeAll();
      indiceNotizia = 0;
      textX = PANEL_RES_X;
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      lastAttempt = millis();
    }
  }
  else
  {
    //SinricPro.handle();

    // Aggiorna l'ora dal NTP ogni ora
    static unsigned long lastUpdateTime = 0;
    if (millis() > lastUpdateTime + timeUpdateInterval)
    {
      syncRTCwithNTP(); // Sincronizza ogni mez'ora
      Serial.println("Sincronizzazione RTC con NTP eseguita con successo.");
      lastUpdateTime = millis();
    }
    static unsigned long lastWeatherUpdate = 0;
    if (weatherConfigured() && millis() > lastWeatherUpdate + weatherUpdateInterval)
    {
      fetchWeather();
      lastWeatherUpdate = millis();
    }
    if (millis() > lastUpdateNews + newsUpdateInterval)
    {
      fetchRSSFeed();
      lastUpdateNews = millis();
    }
  }

  if (orologioAttivo)
  {
    gestisciOrologio();
    nextState = false;
  }
  else
  {
    // Se l'orologio non è attivo, non fare nulla
    if (nextState == false)
    {
      dma_display->fillScreen(myBLACK); // Pulisce lo schermo
      nextState = true;
    }
  }
}
