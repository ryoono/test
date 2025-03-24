#include <M5Core2.h>
#include <WiFi.h>

const char* ssid = "M5StackTest";
const char* password = "01234567";

const char* hostIP = "192.168.4.1";
const uint16_t hostPort = 1234;


WiFiClient client;
unsigned long lastSendTime = 0;
uint32_t counter = 0;

void setup() {
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Connecting to WiFi...");

  // ★ 固定IPを設定
  IPAddress local_IP(192, 168, 4, 4);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  // ★ WiFi接続
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    M5.Lcd.print(".");
  }

  M5.Lcd.println("\nWiFi connected");
  M5.Lcd.print("IP: ");
  M5.Lcd.println(WiFi.localIP());

  // TCP接続
  while (!client.connect(hostIP, hostPort)) {
    M5.Lcd.println("Connecting to host...");
    delay(1000);
  }
  M5.Lcd.println("Connected to host!");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSendTime >= 100) {
    lastSendTime = now;
    if (client.connected()) {
      client.println(counter++);
    } else {
      M5.Lcd.println("Disconnected. Reconnecting...");
      client.connect(hostIP, hostPort);
    }
  }
}
