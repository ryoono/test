#include <M5Core2.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

const char* ssid = "M5StackTest";
const char* password = "01234567";

WiFiServer server(1234); // 任意のポート

WiFiClient clientSlots[3];          // 各IPに対応するクライアント
String clientMessages[3] = {"", "", ""};

IPAddress knownIPs[3] = {
  IPAddress(192, 168, 4, 2), // Client 1
  IPAddress(192, 168, 4, 3), // Client 2
  IPAddress(192, 168, 4, 4)  // Client 3
};

void setup() {
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Starting AP...");

  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  server.begin();
  M5.Lcd.println("AP Started: 192.168.4.1");
}

void loop() {
  // 新しいクライアントの接続処理
  if (server.hasClient()) {
    WiFiClient newClient = server.accept();
    IPAddress remoteIP = newClient.remoteIP();

    for (int i = 0; i < 3; i++) {
      if (remoteIP == knownIPs[i]) {
        if (clientSlots[i]) clientSlots[i].stop(); // 既存接続を切断
        clientSlots[i] = newClient;
        break;
      }
    }
  }

  // 各クライアントからの受信処理
  for (int i = 0; i < 3; i++) {
    if (clientSlots[i] && clientSlots[i].connected() && clientSlots[i].available()) {
      String msg = clientSlots[i].readStringUntil('\n');
      msg.trim();
      clientMessages[i] = "Client " + String(i + 1) + ": " + msg;
    }
  }

  // 表示
  M5.Lcd.fillScreen(BLACK);
  for (int i = 0; i < 3; i++) {
    M5.Lcd.setCursor(0, i * 30);
    M5.Lcd.println(clientMessages[i]);
  }

  delay(50);
}
