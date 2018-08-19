# sonoff-mqtt
Simple MQTT-TLS-Firmware for Sonoff Basic and similar devices allowing for switching the relay via MQTT and switch, state reporting and OTA firmware update.

## Configuration

Open src/main.cpp and edit WiFi and MQTT-Configuration as desired. If you're not running a Sonoff Basic you may have to change the pin definitions just above hte WiFi configuration.

## Building

When using [PlatformIO](https://www.platformio.org) just download/clone and open the project folder. You should be able to build everything right away.
On Arduino ensure [PubSubClient](https://github.com/knolleary/pubsubclient) is installed.

## Functionality

Every of the following MQTT topics is relative to the configured mqtt_root.

Upon boot the device will connect to WiFi and MQTT. While connecting the LED is on. Once the connection is established the LED will turn off and and the system announces the used Hardware (/hardware), Version (/version), Statistic interval (/statsinterval) and MAC-Address (/mac). Additionally /online will be set to 1. This will change to 0 once the device no longer responds (LWT).

Every 60 seconds (if not changed) variable system data will be published. This includes the current IPv4-address (/ip), system uptime in seconds (/uptime) and WiFi signal strength (/rssi).

When receiving a message on /set the device will check if the value reads "ON" or "OFF". If so the relay and led will turn on or off accordingly.

Additionally the button can be used to toggle the relay manually.

In either case the new relay state is published to /get.

Additional commands are:
* /set/ping -> Received value is requblished to /get/pong
* /set/reset -> Device reboots
* /set/update -> Device enters ArduinoOTA mode (untested)
