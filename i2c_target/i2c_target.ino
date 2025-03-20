#include <M5Core2.h>
#include <Wire.h>

#define I2C_SLAVE_ADDR 8

// I2C受信時の割り込みコールバック
// 受信した1バイトのデータをLCDに表示します。
void receiveEvent(int howMany) {
  if (Wire.available()) {
    int8_t data = Wire.read();
    M5.Lcd.clear();
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    
    if (data == 1) {
      // 1の場合は接続無しを意味する
      M5.Lcd.println("No Connection");
    } else {
      // それ以外は受信したRSSI値を表示
      M5.Lcd.printf("RSSI: %d", data);
    }
  }
}

void setup() {
  // M5Stack Core2の初期化
  M5.begin();
  
  // I2Cスレーブとして初期化
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(receiveEvent);
  
  // LCD初期設定
  M5.Lcd.setTextSize(2);
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Target Ready");
}

void loop() {
  // I2C受信はWire.onReceive()で割り込み処理されるため、ループ内処理は不要
}
