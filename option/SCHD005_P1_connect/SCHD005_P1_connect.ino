#include <Arduino.h>
#include <WiFi.h>
#include <M5Core2.h>
#include <WebSocketsServer.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Wire.h>

// SSIDとパスワードの設定
const char *ssid = "M5Stack-AP";
const char *password = "12345678";

// WebSocketサーバーの設定
const int webSocketPort = 81;
WebSocketsServer webSocket = WebSocketsServer(webSocketPort);

// HTTPサーバーの設定
AsyncWebServer server(80);

// WiFiサーバーの設定
WiFiServer wifiServer(50000); // 任意のポート

WiFiClient clientSlot;          // エスカレーター下部デバイスに対応するクライアント
String clientMessage[3] = {""}; // クライアントからのメッセージ格納用;

// ★ 追加: 192.168.4.2 へ 400ms ごとに送るため
WiFiClient client400;                 // 192.168.4.2用のクライアント
const uint16_t peerPort400 = 50000;   // 送信先ポート（既存と同じ）
String tcp400Payload = "";            // 送信内容（あとで中身を設定）
static unsigned long prev400 = 0;     // 400ms タイマ

IPAddress knownIP = IPAddress(192, 168, 4, 4);  // エスカレーター下部デバイス
IPAddress knownIP2 = IPAddress(192, 168, 4, 2); // サブエスカレーター(ZJ2)上部デバイス
IPAddress knownIP3 = IPAddress(192, 168, 4, 3); // サブエスカレーター(ZJ2)下部デバイス

/* ピンをここで定義します */
/* モジュールの設定と同じにする必要があります */
#define RX_PIN 13  // M5Stack Core2のRXピン
#define TX_PIN 14  // M5Stack Core2のTXピン

#define I2C_SLAVE_ADDR 8

// ---- Serial2 frame layout (TSZ response) ----
// error を 4byte × 8 -> 2byte × 8 に変更したため 47 -> 31 バイト
static const size_t FRAME_LEN    = 31;  // 受信フレーム長
static const size_t ERROR_BASE   = 15;  // error[0] の先頭オフセット
static const size_t ERROR_STRIDE = 2;   // 各エラー値のストライド（2バイト）
static const uint8_t ERROR_NUM   = 8;   // エラー要素数

// 各項目の変数をグローバルに宣言
uint16_t stepSpeed = 0;
uint16_t handrailSpeedRight = 0;
uint16_t handrailSpeedLeft = 0;
uint8_t autoDriveSetting = 0;
uint8_t aanSetting = 0;
uint8_t lightingSetting = 0;
int8_t upperLeftCSensor = 0;
int8_t upperRightCSensor = 0;
int8_t lowerLeftCSensor = 0;
int8_t lowerRightCSensor = 0;
int8_t upperLeftASensor = 0;
int8_t lowerRightASensor = 0;
int8_t upperLeftBSensor = 0;
int8_t upperRightBSensor = 0;
int8_t lowerLeftBSensor = 0;
int8_t lowerRightBSensor = 0;
String errorString = "";
int8_t errorCount = 0;

uint8_t directionVal    = 0;
uint8_t operationVal    = 0;
uint8_t SpeedVal        = 0;
uint8_t standbyVal      = 0;
uint8_t lightingVal     = 0;
uint8_t aanVal          = 0;
uint8_t startVal        = 0;
// ★ 追加: “2”系の受け取り値
uint8_t directionVal2   = 0;
uint8_t operationVal2   = 0;
uint8_t SpeedVal2       = 0;
uint8_t standbyVal2     = 0;
uint8_t lightingVal2    = 0;
uint8_t aanVal2         = 0;
uint8_t startVal2       = 0;

uint8_t instructionVal  = 0;
uint8_t is_near_distance_top = 0; // メイン(SAJ)_上部に接近しているか 遠い0、近い1
uint8_t is_near_distance_bot = 0; // メイン(SAJ)_下部に接近しているか 遠い0、近い1
uint8_t is_near_distance_top_sub = 0; // サブ(ZA2)_上部に接近しているか 遠い0、近い1
uint8_t is_near_distance_bot_sub = 0; // サブ(ZA2)_下部に接近しているか 遠い0、近い1

// ★ 追加: ZJ2上部(192.168.4.2)から受け取った追記データ（先頭1文字を除いた41文字）
String zj2TopAppend = "";

unsigned long loop_counter = 0;

int8_t rssiThreshold = -40; // 初期閾値
volatile int8_t receivedRSSI = -100; // 初期値として非常に低いRSSIを設定
volatile bool i2cDataReceived = false; // I2Cデータ受信フラグ

// ★ 追加: 周期制御用タイマ
static unsigned long prev50  = 0;  // 50msごと（ボタン、7コマンド）
static unsigned long prev250 = 0;  // 250msごと（TSZ）

// ★ 追加: WebSocketの購読先（どのページが開いているか）を記録
enum SubPage : uint8_t { SUB_NONE = 0, SUB_INDEX = 1, SUB_CONFIG = 2 };
static volatile SubPage activeSub = SUB_NONE;
static volatile int activeWsClient = -1; // 現在有効なクライアント番号（1台前提）

// I2C受信時の割り込みコールバック
void receiveEvent(int howMany) {
  if (Wire.available()) {
    receivedRSSI = Wire.read();
    i2cDataReceived = true; // データ受信フラグをセット
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
  switch (type)
  {
    case WStype_CONNECTED:
      Serial.printf("WebSocket connected: %u\n", num);
      // ★ 追加: 新たに接続してきたクライアントを“有効”にする（1台前提）
      activeWsClient = num;
      activeSub = SUB_NONE;  // ページ未申告の間は未設定
      break;

    case WStype_DISCONNECTED:
      Serial.printf("WebSocket disconnected: %u\n", num);
      // ★ 追加: 切断されたクライアントが有効クライアントなら購読解除
      if ((int)num == activeWsClient) {
        activeWsClient = -1;
        activeSub = SUB_NONE;
      }
      break;

    // WebSocket受信時の処理
    case WStype_TEXT:
    {
      /*
      受信ペイロード例(決定ボタン):
      {
        "direction": "UP",
        "operation": "auto",
        "speed": "30",
        "standby": "stop",
        "lighting": "linked",
        "aan": "on",
        "direction2": "DN",
        "operation2": "manual",
        "speed2": "20",
        "standby2": "low",
        "lighting2": "off",
        "aan2": "off",
        "start": false,
        "start2": false
      }
      受信ペイロード例(起動ボタン(エス2選択中)):
      {
        "direction": "UP",
        "operation": "auto",
        "speed": "30",
        "standby": "stop",
        "lighting": "linked",
        "aan": "on",
        "direction2": "DN",
        "operation2": "manual",
        "speed2": "20",
        "standby2": "low",
        "lighting2": "off",
        "aan2": "off",
        "start": false,
        "start2": true
      }
      */
      String msg = String((char *)payload);
      Serial.printf("Received JSON: %s\n", msg.c_str()); // 受信したJSONデータをそのまま表示

      // ★ 追加: ページ購読の申告（config.html / index.html 側で接続時に送る）
      if (msg == "SUB:INDEX")  { activeSub = SUB_INDEX;  Serial.println("[WS] subscribe INDEX");  return; }
      if (msg == "SUB:CONFIG") { activeSub = SUB_CONFIG; Serial.println("[WS] subscribe CONFIG"); return; }

      // JSONデータのサイズに応じたStaticJsonDocumentを作成
      StaticJsonDocument<256> doc;

      // JSONデータをパース
      DeserializationError error = deserializeJson(doc, msg);

      // パースエラーのチェック
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }

      // 必要な要素を変数に代入
      String direction = doc["direction"].as<String>();
      String operation = doc["operation"].as<String>();
      String speed     = doc["speed"].as<String>();
      String standby   = doc["standby"].as<String>();
      String lighting  = doc["lighting"].as<String>();
      String aan       = doc["aan"].as<String>();
      String start     = doc["start"].as<String>();
      // ★ 追加: “2”系の受信 → 同じ変換ルールで格納
      String direction2 = doc["direction2"].as<String>();
      String operation2 = doc["operation2"].as<String>();
      String speed2     = doc["speed2"].as<String>();
      String standby2   = doc["standby2"].as<String>();
      String lighting2  = doc["lighting2"].as<String>();
      String aan2       = doc["aan2"].as<String>();
      String start2     = doc["start2"].as<String>();

      // 文字列の内容を見て、数値に変換して代入
      if (direction == "UP") {
        directionVal = 0;
      } else if (direction == "DN") {
        directionVal = 1;
      } else if (direction == "stop") {
        directionVal = 2;
      }

      if (operation == "manual") {
        operationVal = 0;
      } else if (operation == "auto") {
        operationVal = 1;
      }

      if (speed == "10") {
        SpeedVal = 0;
      } else if (speed == "20") {
        SpeedVal = 1;
      } else if (speed == "30") {
        SpeedVal = 2;
      }

      if (standby == "stop") {
        standbyVal = 0;
      } else if (standby == "low") {
        standbyVal = 1;
      }

      if (lighting == "on") {
        lightingVal = 0;
      } else if (lighting == "off") {
        lightingVal = 1;
      } else if (lighting == "linked") {
        lightingVal = 2;
      }

      if (aan == "on") {
        aanVal = 0;
      } else if (aan == "off") {
        aanVal = 1;
      }

      if (start == "true") {
        startVal = 1;
      } else {
        startVal = 0;
      }

      // direction2
      if (direction2 == "UP") {
        directionVal2 = 0;
      } else if (direction2 == "DN") {
        directionVal2 = 1;
      } else if (direction2 == "stop") {
        directionVal2 = 2;
      }

      // operation2
      if (operation2 == "manual") {
        operationVal2 = 0;
      } else if (operation2 == "auto") {
        operationVal2 = 1;
      }

      // speed2
      if (speed2 == "10") {
        SpeedVal2 = 0;
      } else if (speed2 == "20") {
        SpeedVal2 = 1;
      } else if (speed2 == "30") {
        SpeedVal2 = 2;
      }

      // standby2
      if (standby2 == "stop") {
        standbyVal2 = 0;
      } else if (standby2 == "low") {
        standbyVal2 = 1;
      }

      // lighting2
      if (lighting2 == "on") {
        lightingVal2 = 0;
      } else if (lighting2 == "off") {
        lightingVal2 = 1;
      } else if (lighting2 == "linked") {
        lightingVal2 = 2;
      }

      // aan2
      if (aan2 == "on") {
        aanVal2 = 0;
      } else if (aan2 == "off") {
        aanVal2 = 1;
      }

      if (start2 == "true") {
        startVal2 = 1;
      } else {
        startVal2 = 0;
      }
    }
    break;
  }
}

void setup() {
  // M5Stack Core2の初期化
  M5.begin();
  M5.Lcd.setTextSize(2); // 文字サイズを大きく設定
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK); // 文字色と背景色を設定
  M5.Lcd.fillScreen(TFT_BLACK); // 画面を黒でクリア

  // WiFiアクセスポイントの設定
  // https://docs.m5stack.com/ja/arduino/m5core/wifi
  WiFi.softAP(ssid, password);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));

  // アクセスポイントのIPアドレスを取得
  IPAddress IP = WiFi.softAPIP();
  
  // IPアドレスをディスプレイに表示
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.printf("IP address:%s\n", IP.toString().c_str());

  // SPIFFS（ファイルシステム）を初期化ここから
  if (SPIFFS.begin()){
    M5.Lcd.printf("SPIFFS initialized.\n");
  }
  else{
    M5.Lcd.printf("Error initializing SPIFFS.\n");
  }
  // SPIFFS（ファイルシステム）を初期化ここまで
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/index.html", String(), false); });
  
  server.on("/config", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/config.html", String(), false); });
  
  server.begin();

  // Webサーバーを開始
  server.begin();

  // Webソケットを開始
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // シリアル通信の初期化
  Serial.begin(115200);  // デバッグ用シリアルポート
  Serial2.begin(38400, SERIAL_8E1, RX_PIN, TX_PIN);  // シリアルポート2のボーレートを38400、データビットを8、パリティビットを偶数、ストップビットを1に設定し、RXを13、TXを14に設定します。
  delay(1000);

  // WiFiサーバーを開始
  wifiServer.begin();

  // I2Cスレーブとして初期化
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
}

void loop() {
  // 1ms相当で回り続ける処理
  M5.update();
  webSocket.loop();

  // I2Cデータ受信時の処理（毎ループチェック → 実質1ms間隔）
  if (i2cDataReceived) {
    i2cDataReceived = false; // フラグをリセット
    
    if (receivedRSSI == 1) {
      Serial.printf("No Connection\n");
    } else {
      Serial.printf("RSSI: %d\n", receivedRSSI);
    }

    if (receivedRSSI == 1 || receivedRSSI < rssiThreshold) {
      is_near_distance_top = 0;
      Serial.printf("esu top: 0\n");
    }
    else{
      is_near_distance_top = 1;
      Serial.printf("esu top: 1\n");
    }
  }

  // ★ WiFi受信処理：毎ループ（≈1ms）
  // 新しいクライアントの接続処理
  if (wifiServer.hasClient()) {
    WiFiClient newClient = wifiServer.accept();
    IPAddress remoteIP = newClient.remoteIP();

    // 192.168.4.2 / .3 / .4 のみ許可
    if (remoteIP == knownIP || remoteIP == knownIP2 || remoteIP == knownIP3) {
      if (clientSlot) clientSlot.stop(); // 既存接続を切断
      clientSlot = newClient;
    }
  }

  // クライアントからの受信処理（IPごとに分岐）
  if (clientSlot && clientSlot.connected() && clientSlot.available()) {
    String msg = clientSlot.readStringUntil('\n');
    msg.trim();

    IPAddress rip = clientSlot.remoteIP();

    if (rip == knownIP2) {
      // 192.168.4.2: ZJ2上部
      if (msg[0] == '1')  is_near_distance_top_sub  = 1;
      else                is_near_distance_top_sub  = 0;

      // ★ 追加: 先頭1文字以降（=41文字）を保持
      if (msg.length() >= 59) {
        zj2TopAppend = msg.substring(1);  // 区切りは付けず、そのまま41文字を保持
      }
    }
    else if (rip == knownIP3) {
      // 192.168.4.3: ZJ2下部
      if (msg == "1") is_near_distance_bot_sub  = 1;
      else            is_near_distance_bot_sub  = 0;
    }
    else if (rip == knownIP) {
      // 192.168.4.4: SAJ下部
      if (msg == "1") is_near_distance_bot  = 1;
      else            is_near_distance_bot  = 0;
    }
    // 想定外IPは無視
    Serial.printf("%s%d\n", "esu top:", is_near_distance_top);
    Serial.printf("%s%d\n", "esu bot:", is_near_distance_bot);
    Serial.printf("%s%d\n", "zj2 top:", is_near_distance_top_sub);
    Serial.printf("%s%d\n", "zj2 bot:", is_near_distance_bot_sub);
  }

  // ★ 250msごと：TSZ送受信（内部delay(30)はそのまま）
  unsigned long now = millis();
  if (now - prev250 >= 250) {
    prev250 += 250;

    // シリアル通信でデータを送信
    Serial2.write("TSZ\r");
    Serial.println("Sent: TSZ\\r");  // デバッグ用に送信データを表示
    delay(30);  // P1からの返信待ち(必要・既存通り)

    if (Serial2.available() >= FRAME_LEN) {
      uint8_t buffer[FRAME_LEN];
      Serial2.readBytes(buffer, FRAME_LEN);

      // 各項目の変数を更新
      uint16_t preamble = (buffer[1] << 8) | buffer[0];
      stepSpeed = (buffer[3] << 8) | buffer[2];
      handrailSpeedRight = (buffer[5] << 8) | buffer[4];
      handrailSpeedLeft = (buffer[7] << 8) | buffer[6];
      autoDriveSetting = buffer[8];
      aanSetting = buffer[9];
      lightingSetting = buffer[10];

      // センサ状態の変数を更新
      uint32_t sensorStatus = (buffer[14] << 24) | (buffer[13] << 16) | (buffer[12] << 8) | buffer[11];
      upperLeftCSensor = (sensorStatus & 0x00000001) ? 1 : 0;
      upperRightCSensor = (sensorStatus & 0x00000002) ? 1 : 0;
      lowerLeftCSensor = (sensorStatus & 0x00000004) ? 1 : 0;
      lowerRightCSensor = (sensorStatus & 0x00000008) ? 1 : 0;
      upperLeftASensor = (sensorStatus & 0x00000100) ? 1 : 0;
      lowerRightASensor = (sensorStatus & 0x00000800) ? 1 : 0;
      upperLeftBSensor = (sensorStatus & 0x00010000) ? 1 : 0;
      upperRightBSensor = (sensorStatus & 0x00020000) ? 1 : 0;
      lowerLeftBSensor = (sensorStatus & 0x00040000) ? 1 : 0;
      lowerRightBSensor = (sensorStatus & 0x00080000) ? 1 : 0;

      // エラー情報の変数を更新（2byte x 8個）
      uint16_t error[ERROR_NUM];
      for (uint8_t i = 0; i < ERROR_NUM; ++i) {
        size_t buf = ERROR_BASE + (i * ERROR_STRIDE);
        error[i] = (((buffer[buf + 1] << 8) | buffer[buf]) & 0x0FFF);  // 下位12bit使用
      }

      // エラー検出中の個数とエラー文字列を作成
      errorCount = 0;
      errorString = "";

      for (uint8_t i = 0; i < ERROR_NUM; ++i) {
        // error[i]が0xFFF以外であれば異常カウントをインクリメント
        // また、errorStringに3桁を追加していく
        if (error[i] != 0xFFF) {
          errorCount++;
          char errorHex[4];
          snprintf(errorHex, sizeof(errorHex), "%03X", error[i]);
          errorString += errorHex;
        }
      }
    }

    // カンマ区切りで連結し、WebSocketで送信
    String data = String(stepSpeed) + "," +
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
                  errorString + ",";
    // ★ 追加: 受信した58文字をそのまま“お尻に”結合（データ+","分）
    data += zj2TopAppend;
    
    // ★ 送信先の出し分け（1台前提）
    //   - SUB_INDEX  : 既存の CSV “data”（index.html 向け）
    //   - SUB_CONFIG : 「近接フラグ 3byte（'0' or '1'）, カンマ, 3byte」の“2文字+区切り1文字”＝"0,1" を送る
    //                  今回の要件では「エス1接近データ + ',' + エス2接近データ」= 3byte
    //                  ここでは「上/下いずれかが1なら1」としてまとめています（必要なら top のみ等に変更可）
    if (activeWsClient >= 0) {
      if (activeSub == SUB_CONFIG) {
        uint8_t near1 = (is_near_distance_top || is_near_distance_bot) ? 1 : 0;           // メイン(エス1)
        uint8_t near2 = (is_near_distance_top_sub || is_near_distance_bot_sub) ? 1 : 0;   // サブ(エス2)
        String nearMsg = String(near1) + "," + String(near2); // 例: "0,1"
        webSocket.sendTXT(activeWsClient, nearMsg);
      } else {
        // 未申告 or SUB_INDEX は従来どおり index 用CSV を送る（後方互換）
        webSocket.sendTXT(activeWsClient, data);
      }
    } else {
      // クライアント未接続時は従来の動作（念のため）
      webSocket.broadcastTXT(data);
    }
  }

  // ★ 400msごとに 192.168.4.2 へ tcp400Payload を送信
  if (now - prev400 >= 400) {
    prev400 += 400;

    if (!client400.connected()) {
      client400.stop(); // 念のため
      client400.connect(knownIP2, peerPort400); // 192.168.4.2:50000 に接続
    }
    if (client400.connected()) {
      tcp400Payload = "";
      tcp400Payload += String(directionVal2);
      tcp400Payload += String(operationVal2);
      tcp400Payload += String(SpeedVal2);;
      tcp400Payload += String(standbyVal2);
      tcp400Payload += String(lightingVal2);
      tcp400Payload += String(aanVal2);
      tcp400Payload += String(startVal2);
      if( is_near_distance_top_sub == 1 )       instructionVal = 1;
      else if( is_near_distance_bot_sub == 1 )  instructionVal = 2;
      else                                    instructionVal = 0;
      tcp400Payload += String(instructionVal);
      client400.println(tcp400Payload);
    }
  }

  // ★ 50msごと：ボタン処理／7コマンド送信
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

  // P1にコマンドを送信する。
  // 7コマンドあるが、50msごとに1つずつ送り続ける
  // 連続で送った時にP1側で処理できるのかがよく分からないので、間をおいて送信する
  // 最大で400msのラグが発生する場合があるが、短いので良しとした。
    ++loop_counter;
    if( loop_counter == 1 ){
      String sendCmd = "DVDR" + String(directionVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 2 ){
      String sendCmd = "DVCF" + String(operationVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 3 ){
      String sendCmd = "DVSP" + String(SpeedVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 4 ){
      String sendCmd = "DVSB" + String(standbyVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 5 ){
      String sendCmd = "DVLG" + String(lightingVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 6 ){
      String sendCmd = "DVAA" + String(aanVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 7 ){
      String sendCmd = "DVST" + String(startVal) + "\r";
      Serial2.write(sendCmd.c_str());
    }
    else if( loop_counter == 8 ){
      if( is_near_distance_top == 1 )       instructionVal = 1;
      else if( is_near_distance_bot == 1 )  instructionVal = 2;
      else                                  instructionVal = 0;
      
      String sendCmd = "DVIS" + String(instructionVal) + "\r";
      Serial2.write(sendCmd.c_str());
      loop_counter = 0; // カウンタのリセット
    }
  }

  // ループ間隔（要求仕様）：1ms
  delay(1);
}
