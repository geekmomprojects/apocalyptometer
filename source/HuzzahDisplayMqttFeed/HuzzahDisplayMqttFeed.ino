#include <SoftwareSerial.h>

#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

#define DECIMAL4    3 // For 7 segment display
#define DECIMAL3    2
#define DECIMAL2    1
#define DECIMAL1    0

enum {MODE_DEM = 0, MODE_REP};
enum {UNINITIALIZED = 0, DOWN, UP};

#define WLAN_SSID         "XXXXXXXXXXXXXXXXXX"   // Replace this with your SSID
#define WLAN_PASS         "XXXXXXXXXXXXXXXXXX"   // Replace this with your pasword

const char HOME_SERVER[] PROGMEM = "192.168.1.26";  // Replace this with your MQTT server IP
#define HOME_SERVERPORT   1883                      // Replace this with your MQTT server port
#define HOME_USERNAME     "XXXXXX"   // Replace this with your MQTT Server Username
#define HOME_PASSWORD     "XXXXXX"   // Replace this with your MQTT Server Password

#define RX_PIN   11  // Not actually used
#define TX_PIN   13  // Transmits to 7 segment display

#define DEM_PIN   4  // LED pins
#define REP_PIN   5
#define UP_PIN    2
#define DOWN_PIN  15

#define SWITCH_PIN 14 // Slide switch connects here

// Store last values - initialize pct with negative numbers
float demPct = -1.0;  
float repPct = -1.0;

int demRising = UNINITIALIZED;
int repRising = UNINITIALIZED;

int displayMode = MODE_DEM;
long lastPollTime = 0;

// MQTT Client class
WiFiClient client;

// Store the MQTT server, client ID, username and password in flash memory
// This is required for using the Adafruit MQTT library
const char* MQTT_SERVER = HOME_SERVER;

// Set a unique MQTT client id
const char MQTT_CLIENTID[] PROGMEM = __TIME__ HOME_USERNAME;
const char MQTT_USERNAME[] PROGMEM = HOME_USERNAME;
const char MQTT_PASSWORD[] PROGMEM = HOME_PASSWORD; 


// Setup the MQTT client class by passing  in the WiFi client and MQTT server and login details
Adafruit_MQTT_Client mqtt(&client, MQTT_SERVER, HOME_SERVERPORT, MQTT_CLIENTID, MQTT_USERNAME, MQTT_PASSWORD);

// Setup a feed for subscribing to the data
const char PollFeed[] PROGMEM = "polls/current";
Adafruit_MQTT_Subscribe PollResults = Adafruit_MQTT_Subscribe(&mqtt, PollFeed);

// Setup a feed called GetPoll to which we write to prompt for Poll Results
const char GetPollFeed[] PROGMEM = "polls/get";
Adafruit_MQTT_Publish GetPoll = Adafruit_MQTT_Publish(&mqtt, GetPollFeed);


SoftwareSerial  numDisplay(RX_PIN, TX_PIN);
int cycles = 0;

void setup() {
  pinMode(DEM_PIN, OUTPUT); // Setup LED pins
  pinMode(REP_PIN, OUTPUT);
  pinMode(UP_PIN, OUTPUT);
  pinMode(DOWN_PIN, OUTPUT);
  digitalWrite(DEM_PIN, LOW);
  digitalWrite(REP_PIN, LOW);
  digitalWrite(UP_PIN, LOW);
  digitalWrite(DOWN_PIN, LOW);

  pinMode(SWITCH_PIN, INPUT); // Setup switchPin
  
  
  // put your setup code here, to run once
  Serial.begin(9600);
  Serial.println("starting...");
  numDisplay.begin(9600);
  numDisplay.write('v');  // Reset display

  // Setup MQTT
  // Connect to WiFi access point
  Serial.print("Connecting to ");
  Serial.println(WLAN_SSID);

  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  // Listen for events on the poll feed
  mqtt.subscribe(&PollResults);

  // Connect to the server
  connect();
}

//Warning - alters string passed in
void displayPollResults(int polMode, char* str) {

  // Set lights for correct political party
  if (polMode == MODE_DEM) {
    digitalWrite(DEM_PIN, HIGH);
    digitalWrite(REP_PIN, LOW);
  } else if (polMode == MODE_REP) {
    digitalWrite(DEM_PIN, LOW);
    digitalWrite(REP_PIN, HIGH);
  } else {
    Serial.print(F("Mode "));
    Serial.print(polMode);
    Serial.println(F(" not recognized."));
  }

  // Set the display
  numDisplay.write('v');        // reset display
  numDisplay.write(0x77);       // Control character for decimal point
  numDisplay.write(0b00000100); // Place decimal after 3rd digit
  numDisplay.write(0x79);       // Reset cursor to 2nd digit
  numDisplay.write(1);
  
  numDisplay.print(str);        // Print numbers to display
}

void setUpDownPins(int dir) {
  if (dir == UP) {
    digitalWrite(UP_PIN, HIGH);
    digitalWrite(DOWN_PIN, LOW);
  } else if (dir == DOWN) {
    digitalWrite(UP_PIN, LOW);
    digitalWrite(DOWN_PIN, HIGH);
  } else {
    digitalWrite(UP_PIN, LOW);
    digitalWrite(DOWN_PIN, LOW);
  }
}

void loop() {

  // Check to see if the user changed the display switch
  int result = digitalRead(SWITCH_PIN);
  int oldMode = displayMode;

  displayMode = (result == HIGH ? MODE_REP : MODE_DEM);

  // Set display value and up/down LED if mode has changed and values are initialized
  if (displayMode != oldMode  && demPct > 0) {
    setUpDownPins(displayMode == MODE_DEM ? demRising : repRising);
    char tempStr[8];
    int num = (int) 10*(displayMode == MODE_DEM ? demPct : repPct);
    sprintf(tempStr, "%3d", num);
    displayPollResults(displayMode, tempStr);
  }
  
  // put your main code here, to run repeatedly:
  Adafruit_MQTT_Subscribe *subscription;

  // Ping the server a few times to make sure we remain connected
  if (!mqtt.ping(3)) {
    // reconnect to server
    if(!mqtt.connected()) {
      connect();
    }
  }

  // Let the MQTT server know we want updated data - default to checking every hour
  long curTime = millis();
  if (lastPollTime == 0 || (curTime - lastPollTime)/1000 > 3600) {
    lastPollTime = curTime;
    if (! GetPoll.publish("data please")) {  // Content doesn't matter - any message will get results
      Serial.println(F("Poll request not transmitted"));
    } else {
      Serial.println(F("Poll request transmitted"));
    }
  }

  // Wait for incoming messages
  while (subscription = mqtt.readSubscription(1000)) {
    // We only care about poll events
    if (subscription == &PollResults) {

      char* value = (char *) PollResults.lastread;
      Serial.println(value);

      // Get Dem/Rep values from string
      char demStr[5], repStr[5];
      float newDemPct, newRepPct;
      
      memcpy(demStr, &value[2], 4);
      demStr[4] = '\0';
      newDemPct = atof(demStr);
      memcpy(repStr, &value[9], 4);
      repStr[4] = '\0';
      newRepPct = atof(repStr);

      /*
      Serial.print("Dem pct = ");
      Serial.print(newDemPct);
      Serial.print(" Rep pct = ");
      Serial.println(newRepPct);
      */
 
      if (demPct > 0 && newDemPct > demPct) demRising = UP;
      else if (demPct > 0 && newDemPct < demPct) demRising = DOWN;

      if (demPct > 0 && newRepPct > repPct) repRising = UP;
      else if (demPct > 0 && newRepPct < repPct) repRising = DOWN;

      if (displayMode == MODE_DEM) {
        demStr[2] = demStr[3];    // Get rid of decimal
        demStr[3] = '\0';
        displayPollResults(displayMode, demStr);
        setUpDownPins(demRising);
      } else {
        repStr[2] = repStr[3];
        repStr[3] = '\0';  
        displayPollResults(displayMode, repStr);
        setUpDownPins(repRising);
      }

      demPct = newDemPct;
      repPct = newRepPct;
    }
  }
  
}

// connect to adafruit io via MQTT
void connect() {
 
  Serial.print(F("Connecting to MQTT SERVER ... "));
 
  int8_t ret;
 
  while ((ret = mqtt.connect()) != 0) {
 
    switch (ret) {
      case 1: Serial.println(F("Wrong protocol")); break;
      case 2: Serial.println(F("ID rejected")); break;
      case 3: Serial.println(F("Server unavail")); break;
      case 4: Serial.println(F("Bad user/pass")); break;
      case 5: Serial.println(F("Not authed")); break;
      case 6: Serial.println(F("Failed to subscribe")); break;
      default: Serial.println(F("Connection failed")); break;
    }
 
    if(ret >= 0)
      mqtt.disconnect();
 
    Serial.println(F("Retrying connection..."));
    delay(5000);
 
  }
 
  Serial.println(F("MQTT Server IO Connected!"));
 
}
