# ESP32-C3 BLE GATT Server & PC Central Template

一套精簡、低記憶體佔用的 ESP32-C3 (ESP-IDF + NimBLE) BLE Peripheral 範本，搭配 PC 端 Python (`bleak`) 與瀏覽器 (Web Bluetooth) 控制範例。支援 HTTP/Wi-Fi 與 BLE 雙向狀態同步（Read / Write / Notify）。

## 1. 關鍵 NimBLE 資源設定 (`sdkconfig`)

為了在 ESP32-C3（同時啟用 Wi-Fi + FATFS）上兼顧 SRAM 節省與穩定性，需特別留意以下參數：

| 設定項目 | 建議值 | 說明 |
| :--- | :---: | :--- |
| `CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE` | `3072` | **關鍵**：預設值在 Client 寫入 CCCD (`0x2902`) 開啟 Notify 時容易觸發 Stack Protection Fault，拉高至此數值可穩定運作。 |
| `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` | `12` | 縮減底層封包緩衝區以節省 SRAM。 |
| `CONFIG_BT_NIMBLE_MAX_CONNECTIONS` | `1` | 單一連線模式，降低記憶體開銷。 |
| `CONFIG_BT_NIMBLE_EXT_ADV_MAX_OR_INSTANCES` (`MAX_ACT`) | `2` | 限制最大廣播/活動實例數。 |

## 2. GATT 結構與廣播設定

* **廣播名稱前綴**：`CAN-`（例：`CAN-192.168.x.x`）
* **斷線處理**：於 `BLE_GAP_EVENT_DISCONNECT` 自動重啟廣播。
* **LED Control Characteristic**：
  * **UUID**：`12345678-1234-5678-1234-56789abcdef1`
  * **權限**：`READ` | `WRITE` | `WRITE_NO_RSP` | `NOTIFY`
  * **資料格式**：1 byte (`0x00` = OFF, `0x01` = ON)
  * **同步機制**：無論由 HTTP 或 BLE 觸發狀態變更，皆統一透過 `led_op()` 更新硬體並主動發送 Notify。

## 3. PC 端快速測試 (Python `bleak`)

不需手動建立虛擬環境，直接使用 `uv` 執行：

```bash
uv run --with bleak ble_client.py