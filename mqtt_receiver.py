import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import json
import csv
import time
import os
import threading
import paho.mqtt.client as mqtt

class MQTTDataReceiverApp:
    def __init__(self, master):
        self.master = master
        master.title("MQTT JSON Data Receiver")

        # MQTT設定セクション
        mqtt_frame = tk.LabelFrame(master, text="MQTT設定")
        mqtt_frame.pack(fill="x", padx=5, pady=5)

        tk.Label(mqtt_frame, text="MQTTブローカー:").grid(row=0, column=0, sticky="e")
        self.mqtt_broker_entry = tk.Entry(mqtt_frame)
        self.mqtt_broker_entry.grid(row=0, column=1, sticky="we")
        self.mqtt_broker_entry.insert(0, "localhost")

        tk.Label(mqtt_frame, text="MQTTポート:").grid(row=1, column=0, sticky="e")
        self.mqtt_port_entry = tk.Entry(mqtt_frame)
        self.mqtt_port_entry.grid(row=1, column=1, sticky="we")
        self.mqtt_port_entry.insert(0, "1883")

        tk.Label(mqtt_frame, text="トピック:").grid(row=2, column=0, sticky="e")
        self.mqtt_topic_entry = tk.Entry(mqtt_frame)
        self.mqtt_topic_entry.grid(row=2, column=1, sticky="we")
        self.mqtt_topic_entry.insert(0, "silabs/aoa/iq_report/ble-pd-0C4314F46ABD/ble-pd-0C4314F45FCB")

        # CSV設定セクション
        csv_frame = tk.LabelFrame(master, text="CSV設定")
        csv_frame.pack(fill="x", padx=5, pady=5)

        tk.Label(csv_frame, text="CSVファイル名:").grid(row=0, column=0, sticky="e")
        self.csv_file_entry = tk.Entry(csv_frame)
        self.csv_file_entry.grid(row=0, column=1, sticky="we")
        self.csv_file_entry.insert(0, "data.csv")

        # ★ 計測回数の入力を追加
        tk.Label(csv_frame, text="計測回数:").grid(row=1, column=0, sticky="e")
        self.measurement_count_entry = tk.Entry(csv_frame)
        self.measurement_count_entry.grid(row=1, column=1, sticky="we")
        self.measurement_count_entry.insert(0, "0")  # 0は無制限（上限なし）

        # 開始・停止ボタン
        button_frame = tk.Frame(master)
        button_frame.pack(pady=5)

        self.start_button = tk.Button(button_frame, text="受信開始", command=self.start_receiving)
        self.start_button.pack(side="left", padx=5)

        self.stop_button = tk.Button(button_frame, text="受信停止", command=self.stop_receiving, state="disabled")
        self.stop_button.pack(side="left", padx=5)

        # ステータス表示
        self.status_label = tk.Label(master, text="ステータス: 停止中")
        self.status_label.pack(pady=5)

        # ★ 追加表示項目ラベル
        self.start_time_label = tk.Label(master, text="計測開始時刻: -")
        self.start_time_label.pack(pady=2)

        self.now_time_label = tk.Label(master, text="現在時刻: -")
        self.now_time_label.pack(pady=2)

        self.received_count_label = tk.Label(master, text="受信回数: 0")
        self.received_count_label.pack(pady=2)

        # MQTTクライアント
        self.client = None
        self.receiving = False

        # メッセージ数カウント用
        self.message_count = 0
        self.measurement_limit = 0

        # 計測開始時刻
        self.start_time_str = ""

        # GUIのリサイズ設定
        for i in range(2):
            mqtt_frame.columnconfigure(i, weight=1)
            csv_frame.columnconfigure(i, weight=1)

    def start_receiving(self):
        mqtt_broker = self.mqtt_broker_entry.get()
        mqtt_port = int(self.mqtt_port_entry.get())
        topic = self.mqtt_topic_entry.get()
        csv_file = self.csv_file_entry.get()

        if not mqtt_broker or not topic or not csv_file:
            messagebox.showerror("エラー", "すべての設定を入力してください")
            return

        # ★ 計測回数を取得
        try:
            self.measurement_limit = int(self.measurement_count_entry.get())
        except ValueError:
            messagebox.showerror("エラー", "計測回数には整数を入力してください")
            return

        # メッセージカウントをリセット
        self.message_count = 0

        # ★ 計測開始時刻を記録して表示
        self.start_time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
        self.start_time_label.config(text=f"計測開始時刻: {self.start_time_str}")

        # MQTTクライアントの設定
        self.client = mqtt.Client()
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

        try:
            self.client.connect(mqtt_broker, mqtt_port)
        except Exception as e:
            messagebox.showerror("MQTTエラー", f"MQTTブローカーに接続できませんでした: {e}")
            return

        # CSVファイルの準備
        try:
            if not os.path.exists(csv_file):
                with open(csv_file, mode='w', newline='') as csvfile:
                    csvwriter = csv.writer(csvfile)
                    # ヘッダーはデータ受信時に動的に設定
            self.csv_file = csv_file
        except Exception as e:
            messagebox.showerror("ファイルエラー", f"CSVファイルを作成できませんでした: {e}")
            return

        self.receiving = True
        self.start_button.config(state="disabled")
        self.stop_button.config(state="normal")
        self.status_label.config(text="ステータス: 受信中")

        # MQTT受信を別スレッドで開始
        threading.Thread(target=self.client.loop_forever, daemon=True).start()

    def stop_receiving(self):
        self.receiving = False
        if self.client:
            self.client.disconnect()
        self.start_button.config(state="normal")
        self.stop_button.config(state="disabled")
        self.status_label.config(text="ステータス: 停止中")

    def on_connect(self, client, userdata, flags, respons_code):
        topic = self.mqtt_topic_entry.get()
        client.subscribe(topic)
        print(f"トピック {topic} にサブスクライブしました")

    def on_message(self, client, userdata, msg):
        if not self.receiving:
            return
        json_str = msg.payload.decode('utf-8')
        self.process_message(json_str)

    def process_message(self, json_str):
        try:
            data = json.loads(json_str)
            timestamp = time.time()
            row = [timestamp]

            # 'channel', 'rssi', 'sequence' を追加
            for key in ['channel', 'rssi', 'sequence']:
                if key in data:
                    row.append(data[key])
                else:
                    row.append('')

            # 'samples' の要素を追加
            if 'samples' in data:
                row.extend(data['samples'])

            # CSVに書き込み
            with open(self.csv_file, mode='a', newline='') as csvfile:
                csvwriter = csv.writer(csvfile)
                csvwriter.writerow(row)

            # ★ メッセージ数カウントアップ
            self.message_count += 1

            # ★ 現在時刻と受信回数を表示更新
            current_time_str = time.strftime('%Y-%m-%d %H:%M:%S', time.localtime())
            self.now_time_label.config(text=f"現在時刻: {current_time_str}")
            self.received_count_label.config(text=f"受信回数: {self.message_count}")

            # ★ 計測回数に達したかどうかをチェック
            if self.measurement_limit > 0 and self.message_count >= self.measurement_limit:
                self.stop_receiving()

        except Exception as e:
            print(f"データ処理中にエラーが発生しました: {e}")

    def on_closing(self):
        self.stop_receiving()
        self.master.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = MQTTDataReceiverApp(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()
