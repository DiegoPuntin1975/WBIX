/*
 *  Smart Sprinkler controller(node):
 *  Can activate relay or TRIAC to allow AC voltage to drive irrigation
 *
 *  Triggers include:
 *      Webpage hosted through the ESP266 chip
 *      IOT subscrition feed
 *
 *  Scheduling to be done through centralized controller(Embedded Linux)
 *
 *  Author: Roberto Pasilas (svtanthony@gmail.com)
 *
 *  Credits:  Arduino Library Examples
 *            Adafruit Huzzah tutorial
 *
 */

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <Adafruit_MQTT.h>
#include <Adafruit_MQTT_Client.h>
#include <Ticker.h>

/******************* Scheduling & Activation Defaults *************************/

#define MAXDUR 600      // Maximum watering time
#define DEFAULT_DUR 120 // Default watering time
#define ON HIGH          // For Active (HIGH or LOW)
#define OFF LOW
#define RAINSENSOR 5

static unsigned int count = 0;

/************************ WiFi & Feed(s) variables ***************************/

// Adafruit IO variables
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    "Adafruit_IO_userName"
#define AIO_KEY         "key_obtained_from_Adafruit_IO"

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

// Feed publications
Adafruit_MQTT_Publish pubValve1 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/valve1");
Adafruit_MQTT_Publish pubValve2 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/valve2");
Adafruit_MQTT_Publish pubValve3 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/valve3");
Adafruit_MQTT_Publish pubValve4 = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/valve4");

// Feed Subscriptions
Adafruit_MQTT_Subscribe subValve1 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/valve1");
Adafruit_MQTT_Subscribe subValve2 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/valve2");
Adafruit_MQTT_Subscribe subValve3 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/valve3");
Adafruit_MQTT_Subscribe subValve4 = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/valve4");

//  Required definitions
MDNSResponder mdns;
ESP8266WebServer server(80);
Ticker chronos;
const char* ssid     = "Network Name";
const char* password = "Network Password";

typedef struct valve {
  bool change = 0;        //  Valve needs to change variables
  bool published = 0;     //  When change , has it been published
  bool state = 0;         //  On/Off state of valve
  unsigned char pin = 0;  //  Board Pin to control
  int duration = 0;       //  Run time
  Adafruit_MQTT_Publish *pubPtr;
  Adafruit_MQTT_Subscribe *subPtr;
} valve;

valve valves[4];

//Function declarations
void MQTT_connect();
void argProcess();
void webpagefun();
void subscriptionCheck();
void makeChanges(struct valve &ptr);
void scheduler ();

void setup() {
  Serial.begin(115200);
  delay(100);

  //  Attach feeds to publish
  valves[0].pubPtr = &pubValve1;
  valves[1].pubPtr = &pubValve2;
  valves[2].pubPtr = &pubValve3;
  valves[3].pubPtr = &pubValve4;

  //  Attach feeds for subscriptions
  valves[0].subPtr = &subValve1;
  valves[1].subPtr = &subValve2;
  valves[2].subPtr = &subValve3;
  valves[3].subPtr = &subValve4;

  //  Set pin outs
  valves[0].pin = 13;
  valves[1].pin = 12;
  valves[2].pin = 14;
  valves[3].pin = 16;

  // Connect to WIFI network
  Serial.print("/n/nConnecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("/nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Set PINS
  for(int i = 0; i < 4; i++){
    pinMode(valves[i].pin,OUTPUT);
    digitalWrite(valves[i].pin, OFF);
  }

  //set lights and sensor
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);
  pinMode(2, OUTPUT);
  digitalWrite(2, HIGH);
  pinMode(RAINSENSOR,INPUT);

  //  Set interrupt to 1 sec
  chronos.attach(1,scheduler);

  //  Webpages
  server.on("/", [](){
    argProcess();
    webpagefun();
  });

  server.begin();

  // Setup MQTT subscriptions
  mqtt.subscribe(&subValve1);
  mqtt.subscribe(&subValve2);
  mqtt.subscribe(&subValve3);
  mqtt.subscribe(&subValve4);

  //  Set local domain
  if (mdns.begin("irrigation", WiFi.localIP())) {
    Serial.println("MDNS responder started");
  }
  
  MDNS.addService("http", "tcp", 80);
}

void loop() {
  yield();
  //  Connect to MQTT broker and check for feeds.
  MQTT_connect();
  subscriptionCheck();

  //  Check for required changes
  for(char i = 0; i < 4; i++)
    if(valves[i].change)
      makeChanges(valves[i]);

  //  Process http requests
  server.handleClient();

  if(count == 240){
    count = 0;
    if( !mqtt.ping()){
      mqtt.disconnect();
    }
  }
}

void webpagefun() {

  static int rain = 0;
  rain = !digitalRead(RAINSENSOR);
  Serial.println(rain);

  server.sendContent(F("<title>Sprinkler Controller</title><h1>Irrigation Controller Web Server</h1>"));
  if((!valves[0].state && !valves[0].change) || (valves[0].state && valves[0].change))
    server.sendContent(F("<p>Sprinkler 1 <a href=\"?valve=1\"><button style='color: red'>OFF</button></a>&nbsp;</p>"));
  else
    server.sendContent(F("<p>Sprinkler 1 <a href=\"?valve=1\"><button style='color: green'>ON</button></a>&nbsp;</p>"));
  if((!valves[1].state && !valves[1].change) || (valves[1].state && valves[1].change))
    server.sendContent(F("<p>Sprinkler 2 <a href=\"?valve=2\"><button style='color: red'>OFF</button></a>&nbsp;</p>"));
  else
    server.sendContent(F("<p>Sprinkler 2 <a href=\"?valve=2\"><button style='color: green'>ON</button></a>&nbsp;</p>"));
  if((!valves[2].state && !valves[2].change) || (valves[2].state && valves[2].change))
    server.sendContent(F("<p>Sprinkler 3 <a href=\"?valve=3\"><button style='color: red'>OFF</button></a>&nbsp;</p>"));
  else
    server.sendContent(F("<p>Sprinkler 3 <a href=\"?valve=3\"><button style='color: green'>ON</button></a>&nbsp;</p>"));
  if((!valves[3].state && !valves[3].change) || (valves[3].state && valves[3].change))
    server.sendContent(F("<p>Sprinkler 4 <a href=\"?valve=4\"><button style='color: red'>OFF</button></a>&nbsp;</p>"));
  else
    server.sendContent(F("<p>Sprinkler 4 <a href=\"?valve=4\"><button style='color: green'>ON</button></a>&nbsp;</p>"));

  if(rain)
    server.sendContent(F("<p>Rain: Detected</p>"));
  else
    server.sendContent(F("<p>Rain: None</p>"));

  digitalWrite(2, HIGH);
}

void argProcess(){

  digitalWrite(2, LOW);

  //  Variables
  char vChar[2];
  String var;
  int vTemp;
  char num[4];
  int dur;

  //  get valve
  var = server.arg("valve");
  var.toCharArray(vChar,sizeof(vChar));
  vTemp = atoi(vChar);

  //  Get run time of the selected valve.
  var = server.arg("dur");
  var.toCharArray(num, sizeof(num));
  dur = atoi(num);

  // Error checking
  if(dur > MAXDUR || dur < 1){
    dur = DEFAULT_DUR;
    Serial.println("dur has been set to DEFAULT");
  }

  //  If vTemp is a valid valve, process variables.
  if(vTemp > 0 && vTemp < 5)
  {
    valves[vTemp-1].change = 1;
    valves[vTemp-1].published = 1;
    valves[vTemp-1].duration = (!valves[vTemp-1].state) ?  dur : 0;
    if(valves[vTemp-1].duration){
      Serial.print("Valve ");
      Serial.print(vTemp);
      Serial.print(" is set to run for ");
      Serial.print(dur);
      Serial.println(" seconds.");
    }
    else{
      Serial.print("Valve ");
      Serial.print(vTemp);
      Serial.println(" has been deactivated.");
    }
  }
}

void subscriptionCheck(){
  //  Check if there's any incoming feeds
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(450))) {
    for(char i = 0; i < 4; i++){
      if(subscription == valves[i].subPtr){

        //  If ON was received for this valve, turn the valve on if its not already on
        if(strcmp ((char*)valves[i].subPtr->lastread,"ON") == 0){
          if(!valves[i].state){
            valves[i].change = 1;
            valves[i].duration = DEFAULT_DUR;
            Serial.print("Valve ");
            Serial.print(i+1);
            Serial.println(" has been activated.");
          }
        }

        //  If ON was NOT received and the valve state is on, turn it off
        else{
          if(valves[i].state){
            valves[i].change = 1;
            valves[i].duration = 0;
            Serial.print("Valve ");
            Serial.print(i+1);
            Serial.println(" has been deactivated.");
          }
        }
      }
    }
  }
}

void makeChanges(struct valve &ptr) {
  //  toggle change and state
  ptr.change = 0;
  ptr.state = !ptr.state;

  //  publish if needed
  if(ptr.published){
    (ptr.state) ? ptr.pubPtr->publish("ON"):ptr.pubPtr->publish("OFF");
    ptr.published = 0;
  }

  //  set pin based on state
  (ptr.state) ? digitalWrite(ptr.pin,ON) : digitalWrite(ptr.pin,OFF);
}

void scheduler (){
  
  bool toggle = 0;

  //  Ping MQTT broker to keep connection alive
  count++;
  //Serial.println(count);


  // Timing for valves that are on
  for(unsigned char i = 0; i < 4; i++){
    if(valves[i].state){
      toggle = 1;
      valves[i].duration--;
      if(valves[i].duration <= 0){
        valves[i].change = 1;
        valves[i].published = 1;
      }
    }
  }

  // Only toggle when valve is on
  if(toggle){
    digitalWrite(0, !digitalRead(0));
  }
  else{
    digitalWrite(0, HIGH);
  }
}

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.println(mqtt.connectErrorString(ret));
       Serial.println("Retrying MQTT connection in 5 seconds...");
       mqtt.disconnect();
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  }
  Serial.println("MQTT Connected!");
}
