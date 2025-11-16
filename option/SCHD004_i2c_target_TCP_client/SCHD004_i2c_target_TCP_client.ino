#include <M5Core2.h>
#include <Wire.h>
#include <WiFi.h>

#define I2C_SLAVE_ADDR 8

const char* ssid = "M5Stack-AP";
const char* password = "12345678";
const char* hostIP = "192.168.4.1";
const uint16_t hostPort = 50000;

WiFiClient client;
int8_t rssiThreshold = -40; // 初期閾値
volatile int8_t receivedRSSI = -100; // 初期値として非常に低いRSSIを設定
volatile bool i2cDataReceived = false; // I2Cデータ受信フラグ

// I2C受信時の割り込みコールバック
void receiveEvent(int howMany) {
  if (Wire.available()) {
    receivedRSSI = Wire.read();
    i2cDataReceived = true; // データ受信フラグをセット
  }
}

void setup() {
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.println("Connecting to WiFi...");

  // 固定IPを設定
  IPAddress local_IP(192, 168, 4, 3);     // ⑧
  // IPAddress local_IP(192, 168, 4, 4);  // ⑨
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  // WiFi接続
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

  // I2Cスレーブとして初期化
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);

  // 初期表示
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Target Ready");
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.printf("Threshold: %d", rssiThreshold);
}

void loop() {
  M5.update();

  // 閾値の変更
  if (M5.BtnA.wasPressed()) {
    rssiThreshold--;
    M5.Lcd.setCursor(0, 40);
    M5.Lcd.printf("Threshold: %d  ", rssiThreshold); // 余分なスペースで上書き
  }
  if (M5.BtnC.wasPressed()) {
    rssiThreshold++;
    M5.Lcd.setCursor(0, 40);
    M5.Lcd.printf("Threshold: %d  ", rssiThreshold); // 余分なスペースで上書き
  }

  // I2Cデータ受信時の処理
  if (i2cDataReceived) {
    i2cDataReceived = false; // フラグをリセット
    M5.Lcd.clear();
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    
    if (receivedRSSI == 1) {
      M5.Lcd.println("No Connection");
    } else {
      M5.Lcd.printf("RSSI: %d", receivedRSSI);
    }
  }

  // TCP送信
  if (client.connected()) {
    if (receivedRSSI == 1 || receivedRSSI < rssiThreshold) {
      client.println(0);
    } else {
      client.println(1);
    }
  } else {
    M5.Lcd.println("Disconnected. Reconnecting...");
    client.connect(hostIP, hostPort);
  }

  delay(100); // 送信間隔を調整
}