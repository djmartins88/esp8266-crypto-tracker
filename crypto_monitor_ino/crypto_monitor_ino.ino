#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <Wire.h>
#include "FS.h"
#include <LittleFS.h>
#include "SH1106Wire.h"

#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorWAV.h"
#include "AudioOutputI2SNoDAC.h"


// ----------------------------
// Configurations - Update these
// ----------------------------

char ssid[] = "NET-2044";      // your network SSID (name)
char password[] = "NHMRTECR";  // your network password

const char* binance_host = "api.binance.com";

// Update this line with the endpoint you want to fetch
const char* binance_endpoint = "/api/v3/ticker/24hr?symbol=";

// Pins based on wiring
#define SCK_PIN D5
#define SDA_PIN D3

#define ROTATE_BUTTON_PIN D0
#define MODE_BUTTON_PIN D1

#define ALERT_PERCENTAGE_LOWER_BOUND -5
#define ALERT_PERCENTAGE_UPPER_BOUND 5

unsigned long screenChangeDelay = 10000;  // Every 10 seconds

#define MAX_PRICE_LEN 10
#define MAX_HOLDINGS 5

// ----------------------------
// End of area you need to change
// ----------------------------

AudioGeneratorWAV *wav;
AudioFileSourceLittleFS *filesource;
AudioOutputI2SNoDAC *out;
File file;

WiFiClientSecure client;

SH1106Wire display(0x3c, SDA_PIN, SCK_PIN, GEOMETRY_128_64);

unsigned long screenChangeDue;

struct TickerInfo {
  String symbol;
  String lastPrice;
  String tfhPriceChange;
  String tfhPriceChangePercent;
};

struct Holding {
  String tickerId;
  TickerInfo tickerInfo;
};

Holding holdings[MAX_HOLDINGS];

int currentIndex = -1;
int holdingsIndex = 0;
int rotateButtonState = LOW;
int modeButtonState = LOW;
int prevRotateButtonState = LOW;
int prevModeButtonState = LOW;

#define TOTAL_MODES 2
// total modes 2
// 0 - rotating
// 1 - static holding
int currentMode = 0;

void addNewHolding(String tickerId) {
    holdings[holdingsIndex++].tickerId = tickerId;
}

void setup() {

  Serial.begin(115200);

  // ----------------------------
  // Holdings - Add your currencies here
  // ----------------------------
  // Go to the currencies coinmarketcap.com page
  // and take the tickerId from the URL (use bitcoin or ethereum as an example)

//  addNewHolding("TESTE");
  addNewHolding("BTCUSDT");
  addNewHolding("BNBUSDT");
  addNewHolding("XRPUSDT");
  addNewHolding("BTTCUSDT");

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  // Attempt to connect to Wifi network:
  Serial.print("Connecting Wifi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  IPAddress ip = WiFi.localIP();
  Serial.println(ip);

  client.setInsecure();

  // ----------------------------
  // Everything below can be thinkered with if you want but should work as is!
  // ----------------------------

  // Initialising the display
  display.init();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(64, 18, F("Crypto Ticker"));
  display.display();

  // Initialize sounds
  audioLogger = &Serial;
  out = new AudioOutputI2SNoDAC();
  wav = new AudioGeneratorWAV();

  LittleFS.begin();
  file = LittleFS.open("/ting-sound-197759-2.wav", "r");
  filesource = new AudioFileSourceLittleFS();

  // initialize buttons
  pinMode(ROTATE_BUTTON_PIN, INPUT);
  pinMode(MODE_BUTTON_PIN, INPUT);

  delay(5000);

}

void displayHolding(int index) {

  TickerInfo tickerInfo = holdings[index].tickerInfo;

  // Serial.println(" holding details: ");

  display.clear();

  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  if (currentMode == 0) {
    display.drawString(123, 0, ">");
  } else {
    display.drawString(123, 0, "||");
  }
  
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  // Serial.println(tickerInfo.symbol);
  display.drawString(64, 0, tickerInfo.symbol);

  display.setFont(ArialMT_Plain_10);
  // Serial.println(tickerInfo.lastPrice);
  display.drawString(64, 23, formatPrice(tickerInfo.lastPrice));

  //Serial.println(tickerInfo.tfhPriceChange);
  //Serial.println(tickerInfo.tfhPriceChangePercent);

  display.drawString(64, 41, String("24h: " + tickerInfo.tfhPriceChangePercent + " % change"));
  display.drawString(64, 51, String("24h: " + formatPrice(tickerInfo.tfhPriceChange) + " $ change"));

  display.display();
}

void displayMessage(String message) {
  display.clear();
  display.setFont(ArialMT_Plain_10);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawStringMaxWidth(0, 0, 128, message);
  display.display();
}

String formatPrice(String price) {

  if (price.length() > MAX_PRICE_LEN) {
    return price.substring(0, MAX_PRICE_LEN); 
  }
  return price;
}

void checkAndAlertFivePercentChange(int index) {

  double percentChange = holdings[index].tickerInfo.tfhPriceChangePercent.toDouble();
  // if not within reasonable interval, alert
  if (!((percentChange > ALERT_PERCENTAGE_LOWER_BOUND) && (percentChange < ALERT_PERCENTAGE_UPPER_BOUND))) {
    Serial.println(String(holdings[index].tickerId + " percent change bigger than +-5%! --->>> " + holdings[index].tickerInfo.tfhPriceChangePercent + " % !"));
    
    // play warning sound!
    filesource->open(file.name());
    wav->begin(filesource, out);
  
  }

}

bool getTickerInfo(int index) {

  // skip request for 'TESTE' ticker
  if (holdings[index].tickerId == "TESTE") {
    
    holdings[index].tickerInfo.symbol = "symbol";
    holdings[index].tickerInfo.lastPrice = "420.69";
    holdings[index].tickerInfo.tfhPriceChange = "0.666";
    holdings[index].tickerInfo.tfhPriceChangePercent = "10.01";

    return true;
  }

  // Serial.print("Connecting to binance .. ");
  if (!client.connect(binance_host, 443)) {
    Serial.println("connection failed");
    return false;
  }
  
  String url = String(binance_endpoint + holdings[index].tickerId);

  //Serial.print("Requesting URL: ");
  //Serial.println(url);

  client.print("GET " + url + " HTTP/1.1\r\nHost: " + binance_hoc:\projects\arduino_crypto_monitor\README.mdst + "\r\nConnection: close\r\n\r\n");

  //Serial.println("Request sent");

  // Check HTTP response
  char status[32] = {0};
  client.readBytesUntil('\r', status, sizeof(status));
  Serial.print(url);
  Serial.print(" :: ");
  Serial.print(status);

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!client.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    return false;
  }

  // Allocate JsonBuffer
  DynamicJsonDocument jsonBuffer(1024);

  // Parse JSON object
  DeserializationError error = deserializeJson(jsonBuffer, client);

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  JsonObject tickerInfo = jsonBuffer.as<JsonObject>();

  holdings[index].tickerInfo.symbol = String(tickerInfo["symbol"]);
  holdings[index].tickerInfo.lastPrice = String(tickerInfo["lastPrice"]);
  holdings[index].tickerInfo.tfhPriceChange = String(tickerInfo["priceChange"]);
  holdings[index].tickerInfo.tfhPriceChangePercent = String(tickerInfo["priceChangePercent"]);
  
  Serial.print(" :: ");
  Serial.println(String(tickerInfo["lastPrice"]));

  return true;

}

void loop() {
  unsigned long timeNow = millis();

  rotateButtonState = digitalRead(ROTATE_BUTTON_PIN);
  modeButtonState = digitalRead(MODE_BUTTON_PIN);

  // if someones click the button, show the next holding, so, the time to change the screen is now!
  if (rotateButtonState == HIGH && prevRotateButtonState == LOW) {
    screenChangeDue = timeNow;
  }

  if (modeButtonState == HIGH && prevModeButtonState == LOW) {
    currentMode = (currentMode + 1) % TOTAL_MODES;
    
    screenChangeDue = timeNow;
  }

  if ((timeNow > screenChangeDue)) {

    // if mode is rotating, get the next index, else keep the same index
    // or, if someone just clicked on rotate button
    if (currentMode == 0 || rotateButtonState == HIGH) {
      currentIndex = (currentIndex + 1) % holdingsIndex;
    }
    
    if (currentIndex > -1) {
      if (getTickerInfo(currentIndex)) {
        //Serial.print("Show holding ");
        //Serial.print(currentIndex);

        checkAndAlertFivePercentChange(currentIndex);
        displayHolding(currentIndex);
      } else {
        Serial.println("Error loading ticker info.");
        displayMessage(F("Error loading data."));
      }
    } else {
      Serial.println("No funds to display");
      displayMessage(F("No funds to display. Edit the setup to add them"));
    }

    screenChangeDue = timeNow + screenChangeDelay;
  }

  if (wav->isRunning()) {
    
    if (!wav->loop()) {
      filesource->close();
      wav->stop();
      out->flush();
      out->stop();
      out->begin();
    } 
  }

  prevRotateButtonState = rotateButtonState;
  prevModeButtonState = modeButtonState;

}