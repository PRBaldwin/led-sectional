/* Make sure NUM_AIRPORTS matches actual number of LEDs.  The first Airport / LED in the string is ignored, and the last airport must NOT Have a comma afterward.
If using the NODEMCU and WS2812B, plug the WS2812B LEDs into the Vin and Ground directly, and the data pin */



#include <ESP8266WiFi.h>
#include <FastLED.h>
#include <vector>
using namespace std;

#define FASTLED_ESP8266_RAW_PIN_ORDER

#define NUM_AIRPORTS 17 // This is really the number of LEDs = airports plus one null led
#define WIND_THRESHOLD 25 // Maximum windspeed for green
#define LIGHTNING_INTERVAL 5000 // ms - how often should lightning strike; not precise because we sleep in-between
#define DO_LIGHTNING true // Lightning uses more power, but is cool.
#define DO_WINDS true // color LEDs for high winds

#define SERVER "www.aviationweather.gov"
#define BASE_URI "/adds/dataserver_current/httpparam?dataSource=metars&requestType=retrieve&format=xml&hoursBeforeNow=3&mostRecentForEachStation=true&stationString="

#define DEBUG false

const char ssid[] = "CHANGE-ME";        // your network SSID (name)
const char pass[] = "CHANGE-ME";    // your network password (use for WPA, or use as key for WEP)
boolean ledStatus = true; // used so leds only indicate connection status on first boot, or after failure
unsigned int lightningLoops;

int status = WL_IDLE_STATUS;

#define READ_TIMEOUT 15 // Cancel query if no data received (seconds)
#define WIFI_TIMEOUT 60 // in seconds
#define RETRY_TIMEOUT 15000 // in ms
#define REQUEST_INTERVAL 900000 // in ms (15 min is 900000)

// Define the array of leds
CRGB leds[NUM_AIRPORTS];
#define DATA_PIN    4
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB
#define BRIGHTNESS 175

std::vector<unsigned short int> lightningLeds;
std::vector<String> airports({
  "NULL", // 0 first LED is ignored
  "KBIE", // 1
  "KAFK", // 2
  "KPMV", // 3
  "KOFF", // 4
  "KCBF", // 5
  "KOMA", // 6
  "KMLE", // 7
  "KAHQ", // 8
  "KFET", // 9
  "KBTA", // 10
  "KTQE", // 11
  "KLCG", // 12
  "KOFK", // 13
  "KOLU", // 14
  "KJYR", // 15
  "KLNK" // 16 last airport does NOT have a comma after
});

void setup() {
  //Initialize serial and wait for port to open:
  Serial.begin(115200);
  //pinMode(D1, OUTPUT); //Declare Pin mode
  //while (!Serial) {
  //    ; // wait for serial port to connect. Needed for native USB
  //}

  pinMode(LED_BUILTIN, OUTPUT); // give us control of the onboard LED
  digitalWrite(LED_BUILTIN, LOW);

  // Initialize LEDs
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_AIRPORTS).setCorrection(TypicalLEDStrip);
  FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW); // on if we're awake
  int c;
  const unsigned int lightningLoopThreshold = REQUEST_INTERVAL / LIGHTNING_INTERVAL;

  // Connect to WiFi. We always want a wifi connection for the ESP8266
  if (WiFi.status() != WL_CONNECTED) {
    if (ledStatus) fill_solid(leds, NUM_AIRPORTS, CRGB::Orange); // indicate status with LEDs, but only on first run or error
    FastLED.show();
    WiFi.mode(WIFI_STA);
    WiFi.hostname("LED-Sectional-" + WiFi.macAddress());
    //wifi_set_sleep_type(LIGHT_SLEEP_T); // use light sleep mode for all delays
    Serial.print("WiFi connecting..");
    WiFi.begin(ssid, pass);
    // Wait up to 1 minute for connection...
    for (c = 0; (c < WIFI_TIMEOUT) && (WiFi.status() != WL_CONNECTED); c++) {
      Serial.write('.');
      delay(1000);
    }
    if (c >= WIFI_TIMEOUT) { // If it didn't connect within WIFI_TIMEOUT
      Serial.println("Failed. Will retry...");
      fill_solid(leds, NUM_AIRPORTS, CRGB::Orange);
      FastLED.show();
      ledStatus = true;
      return;
    }
    Serial.println("OK!");
    if (ledStatus) fill_solid(leds, NUM_AIRPORTS, CRGB::Purple); // indicate status with LEDs
    FastLED.show();
    ledStatus = false;
  }

  // Do some lightning
  if (DO_LIGHTNING && lightningLeds.size() > 0) {
    lightningLoops++;
    Serial.print("Loop: ");
    Serial.println(lightningLoops);
    std::vector<CRGB> lightning(lightningLeds.size());
    for (unsigned short int i = 0; i < lightningLeds.size(); ++i) {
      unsigned short int currentLed = lightningLeds[i];
      lightning[i] = leds[currentLed]; // temporarily store original color
      leds[currentLed] = CRGB::White; // set to white briefly
    }
    FastLED.show();
    delay(25);
    for (unsigned short int i = 0; i < lightningLeds.size(); ++i) {
      unsigned short int currentLed = lightningLeds[i];
      leds[currentLed] = lightning[i]; // restore original color
    }
    FastLED.show();
    digitalWrite(LED_BUILTIN, HIGH);
    delay(LIGHTNING_INTERVAL); // pause during the interval
  }

  if (lightningLeds.size() == 0 || !DO_LIGHTNING || lightningLoops >= lightningLoopThreshold) {
    if (DEBUG) {
      fill_gradient_RGB(leds, NUM_AIRPORTS, CRGB::Red, CRGB::Blue); // Just let us know we're running
      FastLED.show();
    }

    Serial.println("Getting METARs ...");
    if (getMetars()) {
      Serial.println("Refreshing LEDs.");
      FastLED.show();
      if (DO_LIGHTNING && lightningLeds.size() > 0) {
        lightningLoops = 0;
        Serial.println("There is lightning, so no long sleep.");
      } else {
        Serial.print("No lightning; Going into sleep for: ");
        Serial.println(REQUEST_INTERVAL);
        digitalWrite(LED_BUILTIN, HIGH);
        delay(REQUEST_INTERVAL); // delay 10 seconds to give it time to go
      }
    } else {
      delay(RETRY_TIMEOUT); // try again if unsuccessful
    }
  }
}

bool getMetars(){
  lightningLeds.clear(); // clear out existing lightning LEDs since they're global
  fill_solid(leds, NUM_AIRPORTS, CRGB::Black); // Set everything to black just in case there is no report
  uint32_t t;
  char c;
  boolean readingAirport = false;
  boolean readingCondition = false;
  boolean readingWind = false;
  boolean readingGusts = false;
  boolean readingWxstring = false;

  unsigned short int led = 99;
  String currentAirport = "";
  String currentCondition = "";
  String currentLine = "";
  String currentWind = "";
  String currentGusts = "";
  String currentWxstring = "";
  String airportString = "";
  bool firstAirport = true;
  for (int i = 1; i < (NUM_AIRPORTS); i++) {
    if (airports[i] != "NULL" && airports[i] != "VFR" && airports[i] != "MVFR" && airports[i] != "WVFR" && airports[i] != "IFR" && airports[i] != "LIFR") {
      if (firstAirport) {
        firstAirport = false;
        airportString = airports[i];
      } else airportString = airportString + "," + airports[i];
    }
  }

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  Serial.println("\nStarting connection to server...");
  // if you get a connection, report back via serial:
  if (!client.connect(SERVER, 443)) {
    Serial.println("Connection failed!");
    client.stop();
    return false;
  } else {
    Serial.println("Connected ...");
    Serial.print("GET ");
    Serial.print(BASE_URI);
    Serial.print(airportString);
    Serial.println(" HTTP/1.1");
    Serial.print("Host: ");
    Serial.println(SERVER);
    Serial.println("Connection: close");
    Serial.println();
    // Make a HTTP request, and print it to console:
    client.print("GET ");
    client.print(BASE_URI);
    client.print(airportString);
    client.println(" HTTP/1.1");
    client.print("Host: ");
    client.println(SERVER);
    client.println("Connection: close");
    client.println();
    client.flush();
    t = millis(); // start time
    FastLED.clear();

    Serial.print("Getting data");

    while (!client.connected()) {
      if ((millis() - t) >= (READ_TIMEOUT * 1000)) {
        Serial.println("---Timeout---");
        client.stop();
        return false;
      }
      Serial.print(".");
      delay(1000);
    }

    Serial.println();

    while (client.connected()) {
      if ((c = client.read()) >= 0) {
        yield(); // Otherwise the WiFi stack can crash
        currentLine += c;
        if (c == '\n') currentLine = "";
        if (currentLine.endsWith("<station_id>")) { // start paying attention
          if (led != 99) { // we assume we are recording results at each change in airport; 99 means no airport
            doColor(currentAirport, led, currentWind.toInt(), currentGusts.toInt(), currentCondition, currentWxstring);
          }
          currentAirport = ""; // Reset everything when the airport changes
          readingAirport = true;
          currentCondition = "";
          currentWind = "";
          currentGusts = "";
          currentWxstring = "";
        } else if (readingAirport) {
          if (!currentLine.endsWith("<")) {
            currentAirport += c;
          } else {
            readingAirport = false;
            for (unsigned short int i = 0; i < NUM_AIRPORTS; i++) {
              if (airports[i] == currentAirport) {
                led = i;
              }
            }
          }
        } else if (currentLine.endsWith("<wind_speed_kt>")) {
          readingWind = true;
        } else if (readingWind) {
          if (!currentLine.endsWith("<")) {
            currentWind += c;
          } else {
            readingWind = false;
          }
        } else if (currentLine.endsWith("<wind_gust_kt>")) {
          readingGusts = true;
        } else if (readingGusts) {
          if (!currentLine.endsWith("<")) {
            currentGusts += c;
          } else {
            readingGusts = false;
          }
        } else if (currentLine.endsWith("<flight_category>")) {
          readingCondition = true;
        } else if (readingCondition) {
          if (!currentLine.endsWith("<")) {
            currentCondition += c;
          } else {
            readingCondition = false;
          }
        } else if (currentLine.endsWith("<wx_string>")) {
          readingWxstring = true;
        } else if (readingWxstring) {
          if (!currentLine.endsWith("<")) {
            currentWxstring += c;
          } else {
            readingWxstring = false;
          }
        }
        t = millis(); // Reset timeout clock
      } else if ((millis() - t) >= (READ_TIMEOUT * 1000)) {
        Serial.println("---Timeout---");
        fill_solid(leds, NUM_AIRPORTS, CRGB::Cyan); // indicate status with LEDs
        FastLED.show();
        ledStatus = true;
        client.stop();
        return false;
      }
    }
  }
  // need to doColor this for the last airport
  doColor(currentAirport, led, currentWind.toInt(), currentGusts.toInt(), currentCondition, currentWxstring);

  // Do the key LEDs now if they exist
  for (int i = 0; i < (NUM_AIRPORTS); i++) {
    // Use this opportunity to set colors for LEDs in our key then build the request string
    if (airports[i] == "VFR") leds[i] = CRGB::Green;
    else if (airports[i] == "WVFR") leds[i] = CRGB::Yellow;
    else if (airports[i] == "MVFR") leds[i] = CRGB::Blue;
    else if (airports[i] == "IFR") leds[i] = CRGB::Red;
    else if (airports[i] == "LIFR") leds[i] = CRGB::Magenta;
  }

  client.stop();
  return true;
}

void doColor(String identifier, unsigned short int led, int wind, int gusts, String condition, String wxstring) {
  CRGB color;
  Serial.print(identifier);
  Serial.print(": ");
  Serial.print(condition);
  Serial.print(" ");
  Serial.print(wind);
  Serial.print("G");
  Serial.print(gusts);
  Serial.print("kts LED ");
  Serial.print(led);
  Serial.print(" WX: ");
  Serial.println(wxstring);
  if (wxstring.indexOf("TS") != -1) {
    Serial.println("... found lightning!");
    lightningLeds.push_back(led);
  }
  if (condition == "LIFR" || identifier == "LIFR") color = CRGB::Magenta;
  else if (condition == "IFR") color = CRGB::Red;
  else if (condition == "MVFR") color = CRGB::Blue;
  else if (condition == "VFR") {
    if ((wind > WIND_THRESHOLD || gusts > WIND_THRESHOLD) && DO_WINDS) {
      color = CRGB::Yellow;
    } else {
      color = CRGB::Green;
    }
  } else color = CRGB::Black;

  leds[led] = color;
}
