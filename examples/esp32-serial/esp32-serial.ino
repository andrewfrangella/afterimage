// Arduino ESP32: USB serial sensor demo. Select a board-appropriate ADC pin.
// Classic ESP32 GPIO34 is input-only; change SENSOR_PIN for S3/C3 boards.
constexpr int SENSOR_PIN = 34;
void setup() { Serial.begin(115200); analogReadResolution(12); }
void loop() {
  float knob = analogRead(SENSOR_PIN) / 4095.0f;
  Serial.printf("{\"knob\":%.4f}\n", knob);
  delay(20);
}
// For an ESP-NOW receiver, print the same newline-delimited JSON after receiving
// peer data, preferably from loop() rather than the ESP-NOW receive callback.
// Packet layouts and callback signatures depend on your existing transmitter.
