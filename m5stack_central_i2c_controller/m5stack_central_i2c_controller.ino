#include <M5Core2.h>
#include <ArduinoBLE.h>
#include <Wire.h>

// I2C送信先（ターゲット側）のスレーブアドレス
#define I2C_SLAVE_ADDR 8

// グローバル変数（BLE RSSI値蓄積用）
// 100ms間に受信した全RSSI値の合計とカウントを保持し、後で平均値を算出します。
volatile int rssiSum = 0;
volatile int rssiCount = 0;

// 100msタイマー割り込みによりI2C送信処理を行うフラグ
volatile bool i2cSendFlag = false;

// ESP32のハードウェアタイマー（タイマー0を利用）
hw_timer_t * timer = NULL;

// RSSI受信間隔表示用変数
unsigned long milli_second;

int rssi_buf[4];  // RSSIのローパス用バッファ
int rssi_buf_idx; // バッファのインデックス

// タイマー割り込みハンドラ
// ※割り込み内ではできるだけ処理を軽くするため、フラグをセットするのみとします。
void IRAM_ATTR onTimer(){
  i2cSendFlag = true;
}

void setup() {

  // RSSIのローパス用バッファを初期化(初期値:-100)
  for (int i = 0; i < 4; ++i) {
    rssi_buf[i] = -100;
  }
  rssi_buf_idx = 0;
  
  // M5Stack Core2初期化
  M5.begin();
  M5.Lcd.setTextSize(2);
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  
  // BLE初期化＆Heart Rate Service UUID "180D"にフィルタしてスキャン開始
  BLE.begin();
  BLE.scanForUuid("180D");
  
  // I2Cマスター初期化
  Wire.begin();
  
  // ESP32ハードウェアタイマー初期化（80分周で1μs単位、カウントアップ）
  timer = timerBegin(0, 80, true);
  // 100ms = 100,000μs毎にonTimer()を呼ぶ
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 100000, true);
  timerAlarmEnable(timer);
  
  // LCDに初期メッセージ（デバッグ用）
  M5.Lcd.println("Controller Ready");

  milli_second = millis();
}

void loop() {
  // BLEスキャンで受信したデバイスを取得
  BLEDevice peripheral = BLE.available();
  if (peripheral) {
    // 一度Scanを止めないと連続でscanできない
    // 接続できた場合はscanをリセットする
    BLE.stopScan();
    BLE.scanForUuid("180D");

    // RSSIのローパス処理: 過去4回分の平均値を算出
    int rssi_low_pass = 0;
    rssi_buf[rssi_buf_idx++] = peripheral.rssi();
    if (rssi_buf_idx >= 4) {
      rssi_buf_idx = 0;
    }
    for (int i = 0; i < 4; ++i) {
      rssi_low_pass += rssi_buf[i];
    }
    rssi_low_pass /= 4;
    
    // ※割り込み内と競合しないように一時的に割り込みを停止
    noInterrupts();
    rssiSum += rssi_low_pass;
    rssiCount++;
    interrupts();
    
    // ※必要に応じてLCDにデバッグ表示（更新間隔等）
    M5.Lcd.clear();
    M5.Lcd.setCursor(0, 0);
//    M5.Lcd.printf("Dev: %s\n", peripheral.address().c_str());
    M5.Lcd.printf("RSSI: %d\n", rssi_low_pass);
    M5.Lcd.printf("Time: %d ms\n", millis() - milli_second);  // 更新間隔を表示
    milli_second = millis();
  }
  
  // タイマー割り込みで送信フラグが立っている場合、I2C送信処理を実施
  if (i2cSendFlag) {
    int8_t sendVal;
    // クリティカルセクション内で値をコピーし、変数をリセット
    noInterrupts();
    int sum = rssiSum;
    int count = rssiCount;
    rssiSum = 0;
    rssiCount = 0;
    i2cSendFlag = false;
    interrupts();
    
    // 100ms間に1回もRSSI更新がなければ count == 0
    if (count == 0) {
      sendVal = 1;  // 接続無しを意味する値
    } else {
      // 平均値（四捨五入はしません）
      sendVal = (int8_t)(sum / count);
    }
    
    // I2Cマスターとしてターゲット側に1バイト送信
    Wire.beginTransmission(I2C_SLAVE_ADDR);
    Wire.write(sendVal);
    Wire.endTransmission();
    
    // デバッグ用：LCDに送信値表示（任意）
//    M5.Lcd.clear();
//    M5.Lcd.setCursor(0, 0);
//    if (sendVal == 1) {
//      M5.Lcd.println("I2C TX: No BLE");
//    } else {
//      M5.Lcd.printf("I2C TX: %d", sendVal);
//    }
  }
  
  // ループ内ではdelayを使わず、BLEの監視とI2C送信フラグ確認のみを行います。
}
