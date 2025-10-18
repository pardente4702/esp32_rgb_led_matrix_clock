#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <WiFi.h>
#include <RTClib.h>
// #include <SHT21.h>
#include "WifiCredentials.h"
#include <time.h>
#include <HTTPClient.h>
#include <List.hpp>
#include <expat.h>
// #define ESPALEXA_DEBUG
#include <Espalexa.h>
#include <Wire.h>
#include "Adafruit_HTU21DF.h"

Espalexa espalexa;

boolean orologioAttivo = true; // Variabile per controllare se l'orologio è attivo

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

char lastTimeStr[9] = "00:00:00";
char scrollingText[256] = {0};
int textX = PANEL_RES_X;

RTC_DS3231 rtc;
// SHT21 sht;

// Crea un'istanza del sensore
Adafruit_HTU21DF htu = Adafruit_HTU21DF();

const int LDR_PIN = 34;    // Pin analogico a cui è collegato il sensore LDR
const int SQW_PIN = 19;    // Pin SQW collegato all'ESP32
const int BUZZER_PIN = 18; // Pin del buzzer collegato all'ESP32

char daysOfTheWeek[7][12] = {"Domenica", "Lunedi'", "Martedi'", "Mercoledi'", "Giovedi'", "Venerdi'", "Sabato"};
const char *months[] = {"Gen", "Feb", "Mar", "Apr", "Mag", "Giu", "Lug", "Ago", "Set", "Ott", "Nov", "Dic"};

uint32_t tempBrightnessUpdateInterval = 15000; // 15 secondi
uint32_t scrollingSpeed = 15;

volatile bool hourInterrupt = false;

// Imposta la luminosità minima e massima
const int brightnessMin = 30;  // minimo di notte
const int brightnessMax = 220; // massimo di giorno

uint32_t newsUpdateInterval = 600000; // 10 minuti

int brightness = brightnessMax;

// const char *rss_feed_url = "https://www.ansa.it/lazio/notizie/lazio_rss.xml";
const char *rss_feed_url = "https://www.repubblica.it/rss/homepage/rss2.0.xml?ref=RHFT";

uint8_t indiceNotizia = 0;

List<String> newsList;

String currentTitle = "";

// #define BUFFER_SIZE 512
#define BUFFER_SIZE 512

char cdataBuffer[BUFFER_SIZE];
size_t cdataPos = 0;
bool insideItem = false;
bool insideTitle = false;

enum DataGiornoState
{
  DATA,
  GIORNO
}; // Stati possibili
DataGiornoState dataGiornoState = DATA; // Stato iniziale

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

void XMLCALL startElement(void *userData, const char *name, const char **atts)
{
  if (strcmp(name, "item") == 0)
  {
    insideItem = true;
  }
  else if (insideItem && strcmp(name, "title") == 0)
  {
    insideTitle = true;
    cdataPos = 0;
    cdataBuffer[0] = '\0';
  }
}

void XMLCALL endElement(void *userData, const char *name)
{
  if (strcmp(name, "item") == 0)
  {
    insideItem = false;
  }
  else if (strcmp(name, "title") == 0 && insideTitle)
  {
    insideTitle = false;
    Serial.print("Titolo: ");
    Serial.println(cdataBuffer);
    rimuoviAccenti(cdataBuffer); // Converti le lettere accentate
    newsList.add(cdataBuffer);   // Aggiungi il titolo alla lista delle notizie
  }
}

void XMLCALL characterData(void *userData, const char *s, int len)
{
  if (insideTitle && cdataPos + len < BUFFER_SIZE - 1)
  {
    strncat(cdataBuffer, s, len);
    cdataPos += len;
    cdataBuffer[cdataPos] = '\0';
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

void fetchRSSFeed()
{

  Serial.println("Fetching RSS feed...");
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(rss_feed_url);
    int httpCode = http.GET();

    if (httpCode > 0)
    {
      stampaOra();                                     // Stampa l'ora corrente
      newsList.removeAll();                            // Cancella la lista delle notizie
      indiceNotizia = 0;                               // Resetta l'indice della notizia
      textX = PANEL_RES_X;                             // Resetta la posizione del testo
      dma_display->fillRect(0, 24, PANEL_RES_X, 8, 0); // Cancella solo la riga del testo scorrevole

      WiFiClient *stream = http.getStreamPtr();
      XML_Parser parser = XML_ParserCreate("UTF-8");
      XML_SetElementHandler(parser, startElement, endElement);
      XML_SetCharacterDataHandler(parser, characterData);

      char buffer[BUFFER_SIZE];

      boolean xmlParsingInError = false;
      while (stream->connected() && stream->available())
      {
        size_t len = stream->readBytes(buffer, sizeof(buffer));

        delay(100);

        // Serial.println("quello che leggo");
        // Serial.println(buffer);
        if (!XML_Parse(parser, buffer, len, false))
        {
          Serial.printf("Errore nel parsing XML: %s\n", XML_ErrorString(XML_GetErrorCode(parser)));
          Serial.printf("Errore nel parsing XML alla riga %lu, colonna %lu: %s\n",
                        XML_GetCurrentLineNumber(parser),
                        XML_GetCurrentColumnNumber(parser),
                        XML_ErrorString(XML_GetErrorCode(parser)));
          xmlParsingInError = true;
          break;
        }
      }

      if (xmlParsingInError) {
        newsUpdateInterval = 300000;
      } else {
        newsUpdateInterval = 600000;
      }

      // Segnala la fine del documento
      XML_Parse(parser, nullptr, 0, true);

      XML_ParserFree(parser);
    }
    else
    {
      // Serial.println("Error on HTTP request.");
      Serial.printf("Errore HTTP (%d): impossibile scaricare il feed.", httpCode);
    }

    http.end();
  }
}

void soundBuzzer()
{
  digitalWrite(BUZZER_PIN, HIGH); // Accende il buzzer
  delay(500);                     // Suona per 1 secondo
  digitalWrite(BUZZER_PIN, LOW);  // Spegne il buzzer
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

void visualizzaTemperaturaUmidita()
{
  dma_display->fillRect(0, 0, PANEL_RES_X, 8, 0);
  dma_display->setCursor(2, 0);
  dma_display->setTextSize(1);
  dma_display->setTextColor(dma_display->color565(255, 0, 0));
  dma_display->setTextWrap(false);
  dma_display->print(readTemperatureAndHumidity());
}

void IRAM_ATTR handleInterrupt()
{
  hourInterrupt = true; // Imposta un flag quando si verifica l'interrupt
}

void clockChanged(uint8_t lum)
{
  Serial.print("l: ");
  Serial.println(lum);

  if (lum)
  {
    Serial.println("Clock acceso");
    orologioAttivo = true; // Attiva l'orologio
  }
  else
  {
    Serial.println("Clock spento");
    orologioAttivo = false; // Disattiva l'orologio
  }
}

void setup()
{
  Serial.begin(115200);
  esp_reset_reason_t reason = esp_reset_reason();
  Serial.print("Reset reason: ");
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

  // Connessione WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int n = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(1000);
    n++;
    if (n >= 10)
    {
      ESP.restart();
      delay(3000);
      n = 0;
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nConnected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    espalexa.addDevice("Orologio", clockChanged);
    if (espalexa.begin())
    {
      Serial.println("Espalexa started");
    }
    else
    {
      Serial.println("Espalexa failed to start");
    }
    // delay(2000); // Attendi 2 secondi per stabilizzare la connessione
  }
  else
  {
    Serial.println("\nNot connected to WiFi");
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

  pinMode(LDR_PIN, INPUT);        // Imposta il pin LDR come ingresso
  pinMode(SQW_PIN, INPUT_PULLUP); // Configura il pin SQW come ingresso con resistenza pull-up
  pinMode(BUZZER_PIN, OUTPUT);    // Configura il pin del buzzer come uscita

  attachInterrupt(digitalPinToInterrupt(SQW_PIN), handleInterrupt, FALLING); // Configura l'interrupt sul pin SQW

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

  fetchRSSFeed();

  visualizzaTemperaturaUmidita();
}

void gestisciOrologio()
{

  DateTime now = rtc.now();
  String hours = (now.hour() < 10 ? "0" : "") + String(now.hour(), DEC);
  String minutes = (now.minute() < 10 ? "0" : "") + String(now.minute(), DEC);
  String seconds = (now.second() < 10 ? "0" : "") + String(now.second(), DEC);

  char timeStr[9];
  sprintf(timeStr, "%s:%s:%s", hours, minutes, seconds);

  if (strcmp(timeStr, lastTimeStr) != 0)
  {                                                  // Aggiorna solo se cambia
    dma_display->fillRect(0, 16, PANEL_RES_X, 8, 0); // Cancella solo l'area dell'orario
    dma_display->setCursor(8, 16);
    dma_display->setTextSize(1);
    dma_display->setTextColor(dma_display->color565(100, 200, 255));
    dma_display->print(timeStr);
    strcpy(lastTimeStr, timeStr);
  }

  // Reload news list
  static unsigned long lastUpdateNewsTime = 0;
  if (millis() > lastUpdateNewsTime + newsUpdateInterval)
  {
    Serial.println("Reload news...");
    lastUpdateNewsTime = millis();
    indiceNotizia = 0;
    textX = PANEL_RES_X;
    fetchRSSFeed();
  }

  static unsigned long lastUpdateDisplay = 0;
  if ((millis() > lastUpdateDisplay + scrollingSpeed) && (newsList.getSize() > 0))
  {
    lastUpdateDisplay = millis();
    String notizia = newsList.get(indiceNotizia);

    notizia.toCharArray(scrollingText, sizeof(scrollingText));
    // Testo scorrevole
    dma_display->fillRect(0, 24, PANEL_RES_X, 8, 0); // Cancella solo la riga del testo scorrevole
    dma_display->setCursor(textX, 24);
    dma_display->setTextSize(1);
    dma_display->setTextColor(dma_display->color565(255, 255, 0));
    dma_display->print(scrollingText);
    // dma_display->show();

    textX--;
    int limiteNegativo = 0 - ((notizia.length()) * 6);
    if (textX < limiteNegativo)
    {
      // Serial.print("Visualizzo la notizia numero ");
      // Serial.println(indiceNotizia);
      textX = PANEL_RES_X;
      indiceNotizia++;
      indiceNotizia = indiceNotizia % newsList.getSize();
    }
  }

  // Aggiorna la luminosità e la temperatura ogni 30 secondi
  static unsigned long lastUpdateTime = 0;
  if (millis() > lastUpdateTime + tempBrightnessUpdateInterval)
  {
    lastUpdateTime = millis();

    visualizzaTemperaturaUmidita();

    int ldrValue = analogRead(LDR_PIN); // Legge il valore analogico dal sensore LDR
    // Serial.print("LDR value: ");
    // Serial.println(ldrValue); // Stampa il valore letto nel monitor seriale

    // Adatta questi range in base al tuo ambiente reale
    brightness = map(ldrValue, 4000, 100, brightnessMin, brightnessMax);
    brightness = constrain(brightness, brightnessMin, brightnessMax);
    dma_display->setBrightness8(brightness); // 0-255

    // syncRTCwithNTP();
  }

  // Alterno la visualizzazione del giorno della settimana e della data odierna
  static unsigned long Last_UPDATE_DataGiorno = 0;
  if (dataGiornoState == DATA)
  {
    if (millis() > Last_UPDATE_DataGiorno + 5000)
    {
      int leftSpace = centraStringa((String)daysOfTheWeek[now.dayOfTheWeek()]);
      dma_display->fillRect(0, 8, PANEL_RES_X, 8, 0);
      dma_display->setCursor(leftSpace, 8);
      dma_display->setTextSize(1);
      dma_display->setTextColor(dma_display->color565(60, 180, 60));
      dma_display->setTextWrap(false);
      dma_display->print(daysOfTheWeek[now.dayOfTheWeek()]);
      Last_UPDATE_DataGiorno = millis();
      dataGiornoState = GIORNO;
    }
  }
  else
  {
    if (millis() > Last_UPDATE_DataGiorno + 5000)
    {
      int leftSpace = centraStringa("XXXXXXXXX");
      dma_display->fillRect(0, 8, PANEL_RES_X, 8, 0);
      dma_display->setCursor(leftSpace, 8);
      dma_display->setTextSize(1);
      dma_display->setTextColor(dma_display->color565(60, 180, 60));
      dma_display->print(leftPad(now.day(), 2));
      dma_display->setCursor(leftSpace + 12, 8);
      dma_display->print(months[now.month() - 1]);
      dma_display->setCursor(leftSpace + 30, 8);
      dma_display->print(now.year());
      Last_UPDATE_DataGiorno = millis();
      dataGiornoState = DATA;
    }
  }

  if (hourInterrupt)
  {
    hourInterrupt = false; // Resetta il flag dell'interrupt

    DateTime now = rtc.now();
    if (now.minute() == 30 && now.second() == 0)
    {
      syncRTCwithNTP(); // Sincronizza ogni mez'ora
      Serial.println("Sincronizzazione RTC con NTP eseguita con successo.");
    }

    if (now.minute() == 0 && now.second() == 0)
    { // Controlla se è l'inizio di una nuova ora
      // Serial.println("Nuova ora! Suona il buzzer.");
      soundBuzzer();
    }
  }
}

void loop()
{
  espalexa.loop();
  delay(1); // Piccola pausa per evitare blocchi
  static boolean nextState = true;

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
