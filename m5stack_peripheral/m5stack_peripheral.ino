#include <M5Core2.h>
#include <ArduinoBLE.h>
#include "esp_bt.h" // ESP32 Arduinoコアに含まれる

void setup() {
  // M5Stack Core2の初期化
  M5.begin();
  
  // BLEの初期化
  BLE.begin();

  // LCDの初期設定
  M5.Lcd.setTextSize(2);
  M5.Lcd.clear();
  M5.Lcd.setCursor(0, 0);

  // BLE出力パワーを最大に設定
  // ※効果が薄い場合もありますが試行しています
  if (esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9) == ESP_OK) {
    M5.Lcd.println("BLE Power Up");
  }

  // BLE設定
  BLE.setLocalName("M5Stack-Core2");
  BLE.setAdvertisedServiceUuid("180D"); // Heart Rate Service UUID
  BLE.setAdvertisingInterval(32); // 広告間隔（0.625ms単位）
  BLE.advertise();

  // デバイスアドレスをLCDに表示
  String address = BLE.address();
  M5.Lcd.printf("Device Address:\n%s", address.c_str());
}

void loop() {
  // BLE広告のみのため、ループ処理は不要
  delay(10);
}
