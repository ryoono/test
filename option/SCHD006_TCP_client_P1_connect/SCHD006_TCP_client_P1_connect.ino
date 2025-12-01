/*
  SCHD006_TCP_client_P1_connect.ino
  ZJ2-Top 相当の M5Core2 用クライアントプログラム
  ホスト(M5-AP親機)へ TCP 接続し、TSZポーリングと状態送信、
  およびホストからのエス設定受信を行う。
*/

#include <Arduino.h>
#include <WiFi.h>
#include <M5Core2.h>
#include <Wire.h>

// ========= WiFi =========
const char* ssid     = "M5Stack-AP";
const char* password = "12345678";
const IPAddress localIP(192,168,4,2);
const IPAddress gateway(192,168,4,1);
const IPAddress subnet(255,255,255,0);

// ホスト(M5-AP親機: 192.168.4.1)へ TCP
const IPAddress hostIP(192,168,4,1);
const uint16_t hostPort = 50000;
WiFiClient hostClient;

// ========= I2C =========
// 外部(I2Cマスター)からRSSI/近接トリガを受け取る想定。自ボードは I2Cスレーブ。
#define I2C_SLAVE_ADDR 8
volatile int8_t receivedRSSI = -100;
volatile bool   i2cDataReceived = false;
int8_t rssiThreshold = -40; // しきい値
uint8_t is_near_top = 0;    // 0:遠い, 1:近い

void receiveEvent(int howMany) {
  if (Wire.available()) {
    receivedRSSI = Wire.read();     // 1バイト受信（例: RSSI or 特殊値）
    i2cDataReceived = true;
  }
}

// ========= Serial2 (エス本体) =========
#define RX_PIN 13
#define TX_PIN 14

// TSZ応答データ長（P1の返信仕様）
static const size_t FRAME_LEN    = 31;
static const size_t ERROR_BASE   = 15;  // 2byte x 8 個
static const size_t ERROR_STRIDE = 2;
static const uint8_t ERROR_NUM   = 8;

// 受信フィールド（サブ側の18フィールド構成はホスト側と完全一致させる）
uint16_t stepSpeed = 3000;
uint16_t handrailSpeedRight = 3050;
uint16_t handrailSpeedLeft  = 2950;
uint8_t  autoDriveSetting = 0;
uint8_t  aanSetting = 0;
uint8_t  lightingSetting = 0;
int8_t   upperLeftCSensor  = 0;
int8_t   upperRightCSensor = 0;
int8_t   lowerLeftCSensor  = 0;
int8_t   lowerRightCSensor = 0;
int8_t   upperLeftASensor  = 0;
int8_t   lowerRightASensor = 0;
int8_t   upperLeftBSensor  = 0;
int8_t   upperRightBSensor = 0;
int8_t   lowerLeftBSensor  = 0;
int8_t   lowerRightBSensor = 0;
String   errorString = "EEE";
int8_t   errorCount  = 1;

// ========= 下り(ホスト→この端末)のエス設定（8桁の数値文字列） =========
// 受信例: "20110201"  (方向/運転/速度/待機/照明/AAN/起動/指示)  ※各1桁
uint8_t directionVal = 0;   // 0:UP 1:DN 2:stop
uint8_t operationVal = 0;   // 0:manual 1:auto
uint8_t SpeedVal     = 0;   // 0:10 1:20 2:30
uint8_t standbyVal   = 0;   // 0:stop 1:low
uint8_t lightingVal  = 0;   // 0:on 1:off 2:linked
uint8_t aanVal       = 0;   // 0:on 1:off
uint8_t startVal     = 0;   // 0/1
uint8_t instructionVal = 0; // 0:none 1:top 2:bot

uint8_t cnt = 0;//debug

// ========= タイマ =========
static unsigned long prev250 = 0;  // TSZポーリング/状態送信
static unsigned long prev50  = 0;  // DVコマンド送信
static uint8_t       cmdPhase = 0; // 1..8 で巡回送信

// ========= ユーティリティ =========
static inline void ensureTcpConnected() {
  if (hostClient.connected()) return;
  hostClient.stop();
  // 再接続を試みる
  hostClient.connect(hostIP, hostPort);
}

static inline void applyI2CToNearFlag() {
  // 受信値==1 を「No Connection」扱いにしていた元実装に合わせつつ、
  // RSSIしきい値で近接判定
  if (receivedRSSI == 1) {
    is_near_top = 0;
  } else {
    is_near_top = (receivedRSSI >= rssiThreshold) ? 1 : 0;
  }
}

static inline void pollTSZ() {
  // P1へ TSZ 要求
  Serial2.write("TSZ\r");
  delay(30); // 応答待ち（既存仕様）
  if (Serial2.available() >= FRAME_LEN) {
    uint8_t buffer[FRAME_LEN];
    Serial2.readBytes(buffer, FRAME_LEN);

    // for( uint8_t i=0; i<FRAME_LEN; ++i ) {//debug
    //   buffer[i] = cnt + i;
    // }
    // ++cnt;

    // パース
    // uint16_t preamble = (buffer[1] << 8) | buffer[0]; // 使わないが残しておくならコメントアウト
    stepSpeed          = (buffer[3] << 8) | buffer[2];
    handrailSpeedRight = (buffer[5] << 8) | buffer[4];
    handrailSpeedLeft  = (buffer[7] << 8) | buffer[6];
    autoDriveSetting   = buffer[8];
    aanSetting         = buffer[9];
    lightingSetting    = buffer[10];

    uint32_t sensorStatus = (buffer[14] << 24) | (buffer[13] << 16) | (buffer[12] << 8) | buffer[11];
    upperLeftCSensor   = (sensorStatus & 0x00000001) ? 1 : 0;
    upperRightCSensor  = (sensorStatus & 0x00000002) ? 1 : 0;
    lowerLeftCSensor   = (sensorStatus & 0x00000004) ? 1 : 0;
    lowerRightCSensor  = (sensorStatus & 0x00000008) ? 1 : 0;
    upperLeftASensor   = (sensorStatus & 0x00000100) ? 1 : 0;
    lowerRightASensor  = (sensorStatus & 0x00000800) ? 1 : 0;
    upperLeftBSensor   = (sensorStatus & 0x00010000) ? 1 : 0;
    upperRightBSensor  = (sensorStatus & 0x00020000) ? 1 : 0;
    lowerLeftBSensor   = (sensorStatus & 0x00040000) ? 1 : 0;
    lowerRightBSensor  = (sensorStatus & 0x00080000) ? 1 : 0;

    // エラー（2byte x 8、下位12bit使用, 0xFFF=未検出）
    errorCount  = 0;
    errorString = "";
    for (uint8_t i=0;i<ERROR_NUM;++i) {
      size_t ofs = ERROR_BASE + i*ERROR_STRIDE;
      uint16_t e = (((buffer[ofs+1] << 8) | buffer[ofs]) & 0x0FFF);
      if (e != 0x0FFF) {
        errorCount++;
        char hex3[4];
        snprintf(hex3,sizeof(hex3),"%03X", e);
        errorString += hex3;
      }
    }
  }
}

static inline String buildCsv18() {
  // ホストの index.html で使っている18フィールドの並びに完全一致
  String s = String(stepSpeed) + "," +
             String(handrailSpeedRight) + "," +
             String(handrailSpeedLeft) + "," +
             String(autoDriveSetting) + "," +
             String(aanSetting) + "," +
             String(lightingSetting) + "," +
             String(upperLeftCSensor) + "," +
             String(upperRightCSensor) + "," +
             String(lowerLeftCSensor) + "," +
             String(lowerRightCSensor) + "," +
             String(upperLeftASensor) + "," +
             String(lowerRightASensor) + "," +
             String(upperLeftBSensor) + "," +
             String(upperRightBSensor) + "," +
             String(lowerLeftBSensor) + "," +
             String(lowerRightBSensor) + "," +
             String(errorCount) + "," +
             errorString;
  return s;
}

static inline void sendStateToHost() {
  ensureTcpConnected();
  if (!hostClient.connected()) return;

  // debug
  if( !( ++cnt % 20 ) ){
    if(is_near_top) is_near_top=0;
    else            is_near_top=1;
  }

  // 1文字(近接) + 18フィールドCSV
  String line;
  if( is_near_top ) line = "1,";
  else              line = "0,";

  line += buildCsv18();
  hostClient.println(line);
  // Serial.println(line);  // debug
}

// ホスト→本機：8桁（direction/operation/speed/standby/lighting/aan/start/instruction）
static inline void readSettingsFromHost() {
  if (!hostClient.connected()) return;
  while (hostClient.connected() && hostClient.available()) {
    String msg = hostClient.readStringUntil('\n');
    msg.trim();
    if (msg.length() < 8) continue; // 想定外
    // 先頭8文字だけ使用（余分は無視）
    directionVal   = (uint8_t)(msg[0] - '0') & 0x03;
    operationVal   = (uint8_t)(msg[1] - '0') & 0x01;
    SpeedVal       = (uint8_t)(msg[2] - '0') % 3;
    standbyVal     = (uint8_t)(msg[3] - '0') & 0x01;
    lightingVal    = (uint8_t)(msg[4] - '0') % 3;
    aanVal         = (uint8_t)(msg[5] - '0') & 0x01;
    startVal       = (uint8_t)(msg[6] - '0') & 0x01;
    instructionVal = (uint8_t)(msg[7] - '0') % 3;

    String debug_print = "";
    debug_print =  String(directionVal) + "," +
                  String(operationVal) + "," +
                  String(SpeedVal) + "," +
                  String(standbyVal) + "," +
                  String(lightingVal) + "," +
                  String(aanVal) + "," +
                  String(startVal) + "," +
                  String(instructionVal);
    Serial.println("Settings received: " + debug_print); // debug
  }
}

// 50msごとに 1コマンドずつ送る（7+1=8ステップ循環）
static inline void sendDvCommandTick() {
  cmdPhase++;
  if (cmdPhase > 8) cmdPhase = 1;

  String cmd;
  switch (cmdPhase) {
    case 1: cmd = "DVDR" + String(directionVal) + "\r"; break;
    case 2: cmd = "DVCF" + String(operationVal) + "\r"; break;
    case 3: cmd = "DVSP" + String(SpeedVal)     + "\r"; break;
    case 4: cmd = "DVSB" + String(standbyVal)   + "\r"; break;
    case 5: cmd = "DVLG" + String(lightingVal)  + "\r"; break;
    case 6: cmd = "DVAA" + String(aanVal)       + "\r"; break;
    case 7: cmd = "DVST" + String(startVal)     + "\r"; break;
    case 8: cmd = "DVIS" + String(instructionVal) + "\r"; break;
  }
  Serial2.write(cmd.c_str());
}

// ========= SETUP / LOOP =========
void setup() {
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setCursor(8,10); M5.Lcd.println("ZJ2-Top (192.168.4.2)");

  // WiFi (STA固定IP)
  WiFi.mode(WIFI_STA);
  WiFi.config(localIP, gateway, subnet);
  WiFi.begin(ssid, password);

  M5.Lcd.setCursor(8,34); M5.Lcd.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(200);
    M5.Lcd.print(".");
  }
  M5.Lcd.println();
  M5.Lcd.setCursor(8,58); M5.Lcd.printf("IP: %s\n", WiFi.localIP().toString().c_str());

  // TCP 接続開始
  ensureTcpConnected();

  // Serial2
  Serial.begin(115200);
  Serial2.begin(38400, SERIAL_8E1, RX_PIN, TX_PIN);
  delay(300);

  // I2C スレーブ
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);

  // 画面
  M5.Lcd.setCursor(8,82);  M5.Lcd.print("RSSI/near: ");
  M5.Lcd.setCursor(8,106); M5.Lcd.print("TCP: ");

  Serial.begin(115200);
}

void loop() {
  M5.update();
  const unsigned long now = millis();

  // I2C 受信 → 近接判定
  if (i2cDataReceived) {
    i2cDataReceived = false;
    applyI2CToNearFlag();
    // 軽いモニタ
    M5.Lcd.setCursor(8,82);
    if (receivedRSSI == 1) M5.Lcd.printf("NoConn     ");
    else                   M5.Lcd.printf("RSSI=%4d %c ", receivedRSSI, is_near_top ? 'N' : '-');
  }

  // ホストから設定を受信（随時）
  readSettingsFromHost();

  // 250msごと：TSZ取得→状態行をホストへ送信
  if (now - prev250 >= 250) {
    prev250 += 250;
    pollTSZ();
    sendStateToHost();
    // TCP状態を軽く表示
    M5.Lcd.setCursor(8,106);
    M5.Lcd.printf("conn=%d        ", hostClient.connected() ? 1 : 0);
  }

  // 50msごと：DV系コマンドを循環送信
  if (now - prev50 >= 50) {
    prev50 += 50;

    // 閾値の変更（50ms毎にチェック）
    if (M5.BtnA.wasPressed()) {
      --rssiThreshold;
      M5.Lcd.setCursor(0, 80);
      M5.Lcd.printf("Threshold: %d  ", rssiThreshold);
    }
    if (M5.BtnC.wasPressed()) {
      ++rssiThreshold;
      M5.Lcd.setCursor(0, 80);
      M5.Lcd.printf("Threshold: %d  ", rssiThreshold);
    }

    sendDvCommandTick();
  }

  delay(1); // 1ms刻みベース
}
