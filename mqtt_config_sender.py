import tkinter as tk
from tkinter import ttk
from tkinter import messagebox
import json
import paho.mqtt.client as mqtt
import os

class MQTTConfigSenderApp:
    def __init__(self, master):
        self.master = master
        master.title("MQTT JSON Configuration Sender")

        # ウィンドウのリサイズを許可
        master.columnconfigure(0, weight=1)
        master.rowconfigure(1, weight=1)

        # MQTT設定セクション
        mqtt_frame = tk.LabelFrame(master, text="MQTT設定")
        mqtt_frame.pack(fill="x", padx=5, pady=5)

        for i in range(2):
            mqtt_frame.columnconfigure(i, weight=1)

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
        self.mqtt_topic_entry.insert(0, "silabs/aoa/config/<locator_id>")

        tk.Label(mqtt_frame, text="ロケータID:").grid(row=3, column=0, sticky="e")
        self.locator_id_entry = tk.Entry(mqtt_frame)
        self.locator_id_entry.grid(row=3, column=1, sticky="we")
        self.locator_id_entry.insert(0, "ble-pd-0C4314F46ABD")

        # 設定項目セクション
        config_frame = tk.LabelFrame(master, text="設定項目")
        config_frame.pack(fill="both", expand=True, padx=5, pady=5)

        # ウィンドウのリサイズに合わせて列の幅を調整
        config_frame.columnconfigure(0, weight=0)  # チェックボックス
        config_frame.columnconfigure(1, weight=0)  # ラベル
        config_frame.columnconfigure(2, weight=1)  # テキストボックス

        self.config_items = []

        # 外部ファイルから設定項目を読み込む
        self.load_config_items(config_frame)

        # 送信ボタン
        send_button = tk.Button(master, text="設定を送信", command=self.send_configuration)
        send_button.pack(pady=5)

    def load_config_items(self, parent):
        # 外部ファイルのパス
        config_file = 'config_items.json'

        if not os.path.exists(config_file):
            messagebox.showerror("エラー", f"設定項目ファイルが見つかりません: {config_file}")
            return

        with open(config_file, 'r', encoding='utf-8') as f:
            items = json.load(f)

        for idx, item in enumerate(items):
            name = item.get('name')
            widget_type = item.get('widget_type')
            default = item.get('default')
            values = item.get('values')

            if widget_type == 'Entry':
                widget_class = tk.Entry
            elif widget_type == 'Combobox':
                widget_class = ttk.Combobox
            else:
                messagebox.showerror("エラー", f"不明なウィジェットタイプ: {widget_type}")
                continue

            self.create_config_item(parent, idx, name, widget_class, default=default, values=values)

    def create_config_item(self, parent, row, name, widget_class, default=None, values=None):
        var = tk.IntVar()
        checkbutton = tk.Checkbutton(parent, variable=var)
        checkbutton.grid(row=row, column=0, sticky="w")
        label = tk.Label(parent, text=name)
        label.grid(row=row, column=1, sticky="w")
        if widget_class == ttk.Combobox:
            widget = ttk.Combobox(parent, values=values)
            if default:
                widget.set(default)
            else:
                widget.current(0)
        else:
            widget = widget_class(parent)
            if default:
                widget.insert(0, default)
        widget.grid(row=row, column=2, sticky="we", padx=5, pady=2)
        # 各行の列に対してリサイズを設定
        parent.rowconfigure(row, weight=0)
        self.config_items.append({
            'var': var,
            'name': name,
            'widget': widget,
            'widget_class': widget_class
        })

    def send_configuration(self):
        # MQTT設定を取得
        mqtt_broker = self.mqtt_broker_entry.get()
        mqtt_port = int(self.mqtt_port_entry.get())
        base_topic = self.mqtt_topic_entry.get()
        locator_id = self.locator_id_entry.get()

        # トピックの<locator_id>を置換
        topic = base_topic.replace("<locator_id>", locator_id)

        # 選択された設定項目を収集
        config = {}
        for item in self.config_items:
            if item['var'].get():
                name = item['name']
                value = item['widget'].get()
                # 値の検証と解析
                try:
                    if name in ["version", "angleCorrectionTimeout", "angleCorrectionDelay",
                                "cteSamplingInterval", "cteLength", "slotDuration"]:
                        config[name] = int(value)
                    elif name in ["angleFilteringWeight"]:
                        config[name] = float(value)
                    elif name in ["angleFiltering"]:
                        config[name] = value.lower() == "true"
                    elif name in ["aoxMode", "antennaMode", "cteMode", "reportMode"]:
                        config[name] = value
                    elif name in ["antennaArray", "allowlist", "azimuthMask", "elevationMask"]:
                        config[name] = json.loads(value)
                    else:
                        config[name] = value
                except Exception as e:
                    messagebox.showerror("入力エラー", f"{name}の値が無効です: {e}")
                    return

        # JSONに変換
        message = json.dumps(config)
        print(f"送信する設定: {message}")

        # MQTTで送信
        client = mqtt.Client()
        try:
            client.connect(mqtt_broker, mqtt_port)
            client.publish(topic, message)
            messagebox.showinfo("成功", f"設定を{topic}に送信しました")
        except Exception as e:
            messagebox.showerror("MQTTエラー", f"設定の送信に失敗しました: {e}")

if __name__ == "__main__":
    root = tk.Tk()
    app = MQTTConfigSenderApp(root)
    root.mainloop()
