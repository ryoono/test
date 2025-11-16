/*
 * プログラム名: M5Stack Core2 BLE Advertiser
 * 説明: このプログラムは、M5Stack Core2デバイスを使用してBLEアドバタイズを行います。
 *       デバイスは20msごとにアドバタイズを行い、位置情報として電波を発信し続けます。
 *       BLE出力パワーを最大に設定し、デバイスアドレスをLCDに表示します。
 * 
 * ハードウェア: M5Stack Core2
 * ライブラリ: M5Core2, ArduinoBLE, esp_bt (ESP32 Arduinoコアに含まれる)
 * 
 * 作成者: [小野]
 * 作成日: [2025年4月頃]
 * 
 * 仕様:
 * - ペリフェラルとして動作
 * - 20msごとにアドバタイズ
 * - Heart Rate Service UUID (180D) を使用
 * - BLE出力パワーを最大に設定
 * 
 * 使い方:
 * 1. M5Stack Core2を準備し、プログラムをアップロードします。
 * 2. デバイスが起動すると、BLEアドバタイズが開始されます。
 * 3. デバイスアドレスがLCDに表示されます。
 * 
 * 注意事項:
 * - BLE出力パワーの設定は環境によって効果が異なる場合があります。
 * - 広告間隔は32（0.625ms単位）に設定されています。
 */

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
