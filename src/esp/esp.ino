#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// WiFi variables
const char *ssid = "ssid";          // The WiFi SSID/name this ESP8266 should connect to
const char *password = "password";  // The corresponding WiFi password for the SSID above
const char *mqtt_username = "";
const char *mqtt_password = "";

// MQTT Broker variables
const char *mqtt_broker = "broker.hivemq.com";
const char *topic = "ping";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);
String msg = "";

void setup() {
  Serial.begin(9600);   // 9600 baudrate
  Serial.println("Setting up connections...\n");

  // Connect to a WiFi network
  Serial.print("Connecting to WiFi... ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  Serial.println("Success!\n");

  // Connect to an MQTT broker
  client.setServer(mqtt_broker, mqtt_port);
  while (!client.connected()) {
    String client_id = "esp8266-client-";
    client_id += String(WiFi.macAddress());

    Serial.print("Connecting to broker... ");
    if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
      Serial.println("Success!\n");
    } 
    else {
      Serial.println("Failed");
      Serial.println(client.state());
      Serial.println();
      delay(2000);
    }
  }

  Serial.println("Setup finished.\n");
}

void loop() {
  // Read messages from Arduino Uno via USART
  msg = Serial.readStringUntil('\n');

  // Null check
  if (msg[0] != '\0' || msg != "1"){
    // Publish serialized JSON document to ThingsBoard via MQTT
    client.publish(topic, msg.c_str());
  }
}
