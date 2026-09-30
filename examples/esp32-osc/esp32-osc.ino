// Arduino ESP32 Wi-Fi OSC demo; only bundled WiFi/WiFiUDP libraries required.
#include <WiFi.h>
#include <WiFiUdp.h>
#include <cstring>
const char *SSID = "YOUR_WIFI";
const char *PASSWORD = "YOUR_PASSWORD";
IPAddress COMPUTER(192, 168, 1, 100); // Afterimage computer's LAN IPv4 address
constexpr int SENSOR_PIN = 34; // select an ADC pin suitable for your ESP32 board
constexpr unsigned short PORT = 9000;
WiFiUDP udp;
void setup() {
  analogReadResolution(12);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(250);
  udp.begin(9001);
}
void loop() {
  float value = analogRead(SENSOR_PIN) / 4095.0f;
  // /sensor/knob + aligned OSC ,f type string + big-endian IEEE float.
  unsigned char packet[24] = {};
  memcpy(packet, "/sensor/knob", 12);
  memcpy(packet + 16, ",f", 2);
  uint32_t bits; memcpy(&bits, &value, sizeof(bits));
  for (int i = 0; i < 4; ++i) packet[20+i] = (bits >> (24-i*8)) & 255;
  udp.beginPacket(COMPUTER, PORT); udp.write(packet, sizeof(packet)); udp.endPacket();
  delay(20);
}
