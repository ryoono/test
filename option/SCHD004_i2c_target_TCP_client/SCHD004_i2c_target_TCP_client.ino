/*
 * プログラム名: エスカレーター乗降口用 BLE + WiFi 通信デバイス（M5Stack Core2）
 * 
 * 【概要】
 * 本デバイスは、BLE セントラル装置から I2C で送られてくる RSSI 値を受信し、
 * WiFi TCP によりホスト側へ状態（0/1）を送信する役割を持ちます。
 * 起動時には画面を上下 2 分割し、ユーザーがタップした方の IP アドレス
 * （192.168.4.3 または 192.168.4.4）を本体の固定 IP として設定します。
 * 
 * 【主な機能】
 * 1. 起動時に IP アドレス選択画面を表示
 *    - 上半分: 黒背景 白字「192.168.4.3」
 *    - 下半分: 白背景 黒字「192.168.4.4」
 *    どちらかをタップすると、その IP を固定 IP として WiFi.config に設定します。
 * 
 * 2. I2C スレーブとして動作し、BLE セントラル装置から RSSI 1 バイトを受信
 *    - RSSI=1 → 接続無しを意味する特殊値
 *    - それ以外 → 電波強度として扱う
 * 
 * 3. WiFi（固定 IP）で AP「M5Stack-AP」に接続し、TCP サーバーへ接続
 * 
 * 4. I2C で受け取った RSSI に応じて 500ms ごとに TCP 送信
 *    - RSSI < 閾値 または receivedRSSI == 1 → 0 を送信
 *    - RSSI >= 閾値 → 1 を送信
 * 
 * 5. 閾値は M5Stack Core2 のボタンで調整可能
 *    - BtnA：閾値を下げる
 *    - BtnC：閾値を上げる
 * 
 * 【注意事項】
 * - IP 選択は起動時 1 回のみ実施。やり直しはリセットボタンで対応。
 * - I2C スレーブアドレスは 8。
 * - WiFi は固定 IP 設定（config）後に接続を開始。
 * - RSSI の値は TCP 送信ロジックに直接利用される。
 */

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

uint8_t tcp_cnt;
String deviceName = ""; // 選択されたデバイス名（グローバル）
String deviceIP = "";   // 選択された固定IPの文字列表現
String deviceName = ""; // 選択されたデバイス名（グローバル）

// I2C受信時の割り込みコールバック
void receiveEvent(int howMany) {
  if (Wire.available()) {
    receivedRSSI = Wire.read();
    i2cDataReceived = true; // データ受信フラグをセット
  }
}

void setup() {
  Serial.begin(115200);
  M5.begin();
  M5.Lcd.setTextSize(2);

  // --- 起動時 IP アドレス選択画面の表示 ---
  int16_t w = M5.Lcd.width();
  int16_t h = M5.Lcd.height();
  int16_t midY = h / 2;

  // 上半分: 黒背景＋白文字で 192.168.4.3
  M5.Lcd.fillRect(0, 0, w, midY, BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(10, midY / 2 - 10);
  M5.Lcd.print("192.168.4.3");

  // 下半分: 白背景＋黒文字で 192.168.4.4
  M5.Lcd.fillRect(0, midY, w, h - midY, WHITE);
  M5.Lcd.setTextColor(BLACK, WHITE);
  M5.Lcd.setCursor(10, midY + (midY / 2 - 10));
  M5.Lcd.print("192.168.4.4");

  // タップされた方の IP を選択
  uint8_t selectedIPLastOctet = 0; // 3 または 4 を入れる
  while (selectedIPLastOctet == 0) {
    M5.update();

    if (M5.Touch.ispressed()) {
      TouchPoint_t pos = M5.Touch.getPressPoint();

      // ★ 無効な座標（-1, -1など）は無視する
      if (pos.x < 0 || pos.y < 0) {
        continue;
      }

      if (pos.y < midY) {
        // 上半分タップ: 192.168.4.3
        selectedIPLastOctet = 3;
      } else {
        // 下半分タップ: 192.168.4.4
        selectedIPLastOctet = 4;
      }

      // ★ 指が離れるまで待つ（押しっぱなしでの誤判定防止）
      while (M5.Touch.ispressed()) {
        M5.update();
        delay(10);
      }
    }
  }

  // 選択された IP を設定
  IPAddress local_IP;
  if (selectedIPLastOctet == 3) {
    local_IP = IPAddress(192, 168, 4, 3); // ⑧
    deviceName = "ZJ2-Bot";
  } else {
    local_IP = IPAddress(192, 168, 4, 4); // ⑨
    deviceName = "u-Bot";
  }
  deviceIP = local_IP.toString();
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);

  // 画面リセットして接続メッセージ表示
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setTextColor(WHITE, BLACK);
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Connecting to WiFi...");
  M5.Lcd.print("Selected IP: ");
  M5.Lcd.println(local_IP);

  // 固定IPを設定（選択結果を使用）
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

  // 初期表示: 1行目 デバイス名, 2行目 IP, 3行目 RSSI, 4行目 閾値
  M5.Lcd.clear();
  M5.Lcd.setTextSize(2);
  int w = M5.Lcd.width();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println(deviceName);
  M5.Lcd.setCursor(0, 40);
  M5.Lcd.println(deviceIP);
  M5.Lcd.setCursor(0, 80);
  if (receivedRSSI == 1) {
    M5.Lcd.println("No Connection");
  } else {
    M5.Lcd.printf("RSSI: %d", receivedRSSI);
  }
  M5.Lcd.setCursor(0, 120);
  M5.Lcd.printf("Threshold: %d", rssiThreshold);
  tcp_cnt = 0;
}

void loop() {
  M5.update();

  // 閾値の変更
  if (M5.BtnA.wasPressed()) {
    rssiThreshold--;
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.printf("Threshold: %d  ", rssiThreshold); // 余分なスペースで上書き
  }
  if (M5.BtnC.wasPressed()) {
    rssiThreshold++;
    M5.Lcd.setCursor(0, 80);
    M5.Lcd.printf("Threshold: %d  ", rssiThreshold); // 余分なスペースで上書き
  }

  // I2Cデータ受信時の処理
  if (i2cDataReceived) {
    i2cDataReceived = false; // フラグをリセット
    // 部分更新: デバイス名/IPはそのまま、RSSI行と閾値行のみ更新してチラつきを抑える
    int w = M5.Lcd.width();
    // RSSI行を消して再描画（領域だけ消す）
    M5.Lcd.fillRect(0, 80, w, 40, TFT_BLACK);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 80);
    if (receivedRSSI == 1) {
      M5.Lcd.println("No Connection");
    } else {
      M5.Lcd.printf("RSSI: %d", receivedRSSI);
    }
    // 閾値行も更新
    M5.Lcd.fillRect(0, 120, w, 40, TFT_BLACK);
    M5.Lcd.setCursor(0, 120);
    M5.Lcd.printf("Threshold: %d", rssiThreshold);
  }

  // TCP送信（約500msごと）
  if (!(++tcp_cnt % 5)) {
    tcp_cnt = 0;
  
    if (client.connected()) {
      if (receivedRSSI == 1 || receivedRSSI < rssiThreshold) {
        client.println(0);
        Serial.println(0);
      } else {
        client.println(1);
        Serial.println(1);
      }
    } else {
      Serial.println("Disconnected. Reconnecting...");
      client.connect(hostIP, hostPort);
    }
  }

  delay(100); // 送信間隔を調整
}
