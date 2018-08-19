#include "Arduino.h"
#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#define LED 13
#define RELAY 12
#define BUTTON 0

// WiFi Configuration
const char* cfg_wifi_ssid = "MeinWiFi";
const char* cfg_wifi_password = "geheim";

// MQTT Configuration
const char* mqtt_server = "meinmqtt";
const unsigned int mqtt_port = 1883;
const char* mqtt_user =   "sonoffbasic1";
const char* mqtt_pass =   "auchgeheim";
const char* mqtt_root =   "/esp/sonoffbasic1";

// echo | openssl s_client -connect localhost:1883 |& openssl x509 -fingerprint -noout
const char* mqtt_fprint = "aa bb cc dd ee ff 00 11 22 33 44 55 66 77 88 99 aa bb cc dd";

//periodic status reports
const unsigned int stats_interval = 60;  // Update statistics every 60 seconds

#define CONFIG_MQTT_PAYLOAD_ON "ON"
#define CONFIG_MQTT_PAYLOAD_OFF "OFF"

#define CONFIG_MQTT_TOPIC_GET "/get"
#define CONFIG_MQTT_TOPIC_SET "/set"
#define CONFIG_MQTT_TOPIC_SET_RESET "reset"
#define CONFIG_MQTT_TOPIC_SET_UPDATE "update"
#define CONFIG_MQTT_TOPIC_SET_PING "ping"
#define CONFIG_MQTT_TOPIC_SET_PONG "ping"
#define CONFIG_MQTT_TOPIC_STATUS "/status"
#define CONFIG_MQTT_TOPIC_STATUS_ONLINE "/online"
#define CONFIG_MQTT_TOPIC_STATUS_HARDWARE "/hardware"
#define CONFIG_MQTT_TOPIC_STATUS_VERSION "/version"
#define CONFIG_MQTT_TOPIC_STATUS_INTERVAL "/statsinterval"
#define CONFIG_MQTT_TOPIC_STATUS_IP "/ip"
#define CONFIG_MQTT_TOPIC_STATUS_MAC "/mac"
#define CONFIG_MQTT_TOPIC_STATUS_UPTIME "/uptime"
#define CONFIG_MQTT_TOPIC_STATUS_SIGNAL "/rssi"

#define MQJ_HARDWARE "esp8266tls-sonoff"
#define MQJ_VERSION  "0.0.1"

extern "C" {
  #include "user_interface.h"
}

// Relay status
bool relay_state = false;

class PubSubClientWrapper : public PubSubClient{
  private:
  public:
    PubSubClientWrapper(Client& espc);
    bool publish(StringSumHelper topic, String str);
    bool publish(StringSumHelper topic, unsigned int num);
    bool publish(const char* topic, String str);
    bool publish(const char* topic, unsigned int num);

    bool publish(StringSumHelper topic, String str, bool retain);
    bool publish(StringSumHelper topic, unsigned int num, bool retain);
    bool publish(const char* topic, String str, bool retain);
    bool publish(const char* topic, unsigned int num, bool retain);
};

PubSubClientWrapper::PubSubClientWrapper(Client& espc) : PubSubClient(espc){

}

bool PubSubClientWrapper::publish(StringSumHelper topic, String str) {
  return publish(topic.c_str(), str);
}

bool PubSubClientWrapper::publish(StringSumHelper topic, unsigned int num) {
  return publish(topic.c_str(), num);
}

bool PubSubClientWrapper::publish(const char* topic, String str) {
  return publish(topic, str, false);
}

bool PubSubClientWrapper::publish(const char* topic, unsigned int num) {
  return publish(topic, num, false);
}

bool PubSubClientWrapper::publish(StringSumHelper topic, String str, bool retain) {
  return publish(topic.c_str(), str, retain);
}

bool PubSubClientWrapper::publish(StringSumHelper topic, unsigned int num, bool retain) {
  return publish(topic.c_str(), num, retain);
}

bool PubSubClientWrapper::publish(const char* topic, String str, bool retain) {
  char buf[128];

  if(str.length() >= 128) return false;

  str.toCharArray(buf, 128);
  return PubSubClient::publish(topic, buf, retain);
}

bool PubSubClientWrapper::publish(const char* topic, unsigned int num, bool retain) {
  char buf[6];

  dtostrf(num, 0, 0, buf);
  return PubSubClient::publish(topic, buf, retain);
}

os_timer_t Timer1;
bool sendStats = true;

WiFiClientSecure espClient;
PubSubClientWrapper client(espClient);

void timerCallback(void *arg) {
  sendStats = true;
}

uint8_t rssiToPercentage(int32_t rssi) {
  //@author Marvin Roger - https://github.com/marvinroger/homie-esp8266/blob/ad876b2cd0aaddc7bc30f1c76bfc22cd815730d9/src/Homie/Utils/Helpers.cpp#L12
  uint8_t quality;
  if (rssi <= -100) {
    quality = 0;
  } else if (rssi >= -50) {
    quality = 100;
  } else {
    quality = 2 * (rssi + 100);
  }

  return quality;
}

void ipToString(const IPAddress& ip, char * str) {
  //@author Marvin Roger - https://github.com/marvinroger/homie-esp8266/blob/ad876b2cd0aaddc7bc30f1c76bfc22cd815730d9/src/Homie/Utils/Helpers.cpp#L82
  snprintf(str, 16, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

void sendStatsBoot(void) {
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_HARDWARE), MQJ_HARDWARE, true);
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_VERSION), MQJ_VERSION, true);
  char buf[5];
  sprintf(buf, "%d", stats_interval);
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_INTERVAL), buf, true);
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_MAC), WiFi.macAddress(), true);
}

void sendStatsInterval(void) {
  char buf[16]; //v4 only atm
  ipToString(WiFi.localIP(), buf);
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_IP), buf);
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_UPTIME), (uint32_t)(millis()/1000));
  client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS_SIGNAL), rssiToPercentage(WiFi.RSSI()));
}

void cmdSwitch(bool state) {
    digitalWrite(RELAY, state);
    digitalWrite(LED, !state);
    relay_state = state;
    Serial.print("Switched output to ");
    Serial.println(state);
    client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_GET), (relay_state ? CONFIG_MQTT_PAYLOAD_ON : CONFIG_MQTT_PAYLOAD_OFF), true);
}



void verifyFingerprint() {
  if(client.connected() || espClient.connected()) return; //Already connected

  Serial.print("Checking TLS @ ");
  Serial.print(mqtt_server);
  Serial.print("...");

  if (!espClient.connect(mqtt_server, mqtt_port)) {
    Serial.println("Connection failed. Rebooting.");
    Serial.flush();
    ESP.restart();
  }
  if (espClient.verify(mqtt_fprint, mqtt_server)) {
    Serial.print("Connection secure -> .");
  } else {
    Serial.println("Connection insecure! Rebooting.");
    Serial.flush();
    ESP.restart();
  }

  espClient.stop();

  delay(100);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    verifyFingerprint();
    // Attempt to connect
    if (client.connect(WiFi.macAddress().c_str(), mqtt_user, mqtt_pass, ((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS + CONFIG_MQTT_TOPIC_STATUS_ONLINE).c_str(), 0, 1, "0")) {
      Serial.println("connected");
      client.subscribe(((String)mqtt_root + CONFIG_MQTT_TOPIC_SET + "/#").c_str());
      client.publish((String)mqtt_root + CONFIG_MQTT_TOPIC_STATUS + CONFIG_MQTT_TOPIC_STATUS_ONLINE, "1");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(cfg_wifi_ssid);

  WiFi.mode(WIFI_STA); // Disable the built-in WiFi access point.
  WiFi.begin(cfg_wifi_ssid, cfg_wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char message[length + 1];
  for (unsigned int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  Serial.println(message);

  String topicStr = topic;
  String check = ((String)mqtt_root + CONFIG_MQTT_TOPIC_SET);

  if(topicStr == check) {
    Serial.println("MQTT SET!");
    Serial.flush();
    String strMsg = (String)message;
    if((strMsg.startsWith(CONFIG_MQTT_PAYLOAD_ON)) || (strMsg.startsWith(CONFIG_MQTT_PAYLOAD_OFF))) {
        cmdSwitch((strMsg.startsWith(CONFIG_MQTT_PAYLOAD_ON)) ? true : false);
    }
  }

  if(topicStr.startsWith((String)check + "/" + CONFIG_MQTT_TOPIC_SET_RESET)) {
    Serial.println("MQTT RESET!");
    Serial.flush();
    ESP.restart();
  }

  if(topicStr.startsWith((String)check + "/" + CONFIG_MQTT_TOPIC_SET_PING)) {
    Serial.println("PING");
    client.publish(((String)mqtt_root + CONFIG_MQTT_TOPIC_GET + "/" + CONFIG_MQTT_TOPIC_SET_PONG), message, false);
    return;
  }

  if(topicStr.startsWith((String)check + "/" + CONFIG_MQTT_TOPIC_SET_UPDATE)) {
    Serial.println("OTA REQUESTED!");
    Serial.flush();
    ArduinoOTA.begin();

    unsigned long timeout = millis() + (120*1000); // Max 2 minutes
    os_timer_disarm(&Timer1);

    while(true) {
      yield();
      ArduinoOTA.handle();
      if(millis() > timeout) break;
    }

    os_timer_arm(&Timer1, stats_interval*1000, true);
    return;
  }

  return;
}

void setup()
{
  //LED
  pinMode(LED, OUTPUT);
  digitalWrite(LED, LOW);

  //Relay
  pinMode(RELAY, OUTPUT);
  digitalWrite(RELAY, LOW);

  //Buttom
  pinMode(BUTTON, INPUT_PULLUP);

  // start serial port
  Serial.begin(115200);
  Serial.println("ESP8266 MQTT start...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  ArduinoOTA.setHostname(mqtt_user);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  if (!client.connected()) {
    reconnect();
  }

  // Send boot info
  Serial.println(F("Announcing boot..."));
  sendStatsBoot();

  os_timer_setfn(&Timer1, timerCallback, (void *)0);
  os_timer_arm(&Timer1, stats_interval*1000, true);

  digitalWrite(LED, HIGH);
}

/*
   Main function, calls the temperatures in a loop.
*/
void loop(void)
{
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  if(digitalRead(BUTTON) == LOW) {
    cmdSwitch(!relay_state);
    delay(200);
  }

  if(sendStats) {
    sendStatsInterval();
    sendStats=false;
  }
}
