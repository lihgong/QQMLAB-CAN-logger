import struct
import sys
import os
import csv

# 定義結構大小
SYS_HEADER_SIZE = 512
META_HEADER_SIZE = 512
ENTRY_HEADER_SIZE = 16  # sdlog_data_t

def parse_log(file_path):
    if not os.path.exists(file_path):
        print(f"找不到檔案: {file_path}")
        return

    # 準備 CSV 檔名 (例如 log.bin -> log.csv)
    csv_file_path = os.path.splitext(file_path)[0] + ".csv"

    with open(file_path, "rb") as f, open(csv_file_path, "w", newline='', encoding='utf-8') as csvfile:
        # 初始化 CSV Writer
        fieldnames = ['serial num', 'epoch time', 'relative time', 'stdout']
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()

        # 1. 讀取 System Header
        sys_header_raw = f.read(SYS_HEADER_SIZE)
        if len(sys_header_raw) < SYS_HEADER_SIZE:
            return

        # 解析關鍵時間欄位
        magic, version, _ = struct.unpack_from("<8sII", sys_header_raw, 0)
        us_epoch_time, us_sys_time, fmt = struct.unpack_from("<QQI", sys_header_raw, 16)
        
        if b"QQMLAB" not in magic:
            print("無效的 QQMLAB Log 檔案")
            return

        # 跳過 Meta Header
        f.seek(SYS_HEADER_SIZE + META_HEADER_SIZE)

        # 2. 循環讀取資料
        count = 0
        start_timestamp = None
        
        print(f"正在解析 {file_path} 並寫入 {csv_file_path}...")

        while True:
            entry_header_raw = f.read(ENTRY_HEADER_SIZE)
            if len(entry_header_raw) < ENTRY_HEADER_SIZE:
                break

            magic_byte, type_data, _, _, payload_len, entry_us_sys_time = struct.unpack("<BBBB I Q", entry_header_raw)
            
            if magic_byte != 0xA5:
                break

            payload = f.read(payload_len)
            
            # 處理 8-byte padding
            pad_len = (payload_len + 7) // 8 * 8 - payload_len
            if pad_len > 0:
                f.read(pad_len)

            # 時間計算
            abs_us = us_epoch_time + (entry_us_sys_time - us_sys_time)
            timestamp_sec = abs_us / 1000000.0
            
            # 記錄第一筆時間作為相對時間的基準
            if start_timestamp is None:
                start_timestamp = timestamp_sec
            
            relative_time = timestamp_sec - start_timestamp
            count += 1

            # 根據格式產生輸出字串 (stdout 內容)
            log_content = ""
            if fmt == 1: # CAN 模式
                flags, identifier, dlc = struct.unpack_from("<IIB", payload, 0)
                can_data = payload[9:9+dlc]
                data_hex = " ".join([f"{b:02X}" for b in can_data])
                id_fmt = f"{identifier:08X}" if (flags & 0x01) else f"{identifier:03X}"
                log_content = f"({timestamp_sec:.6f}) can1 {id_fmt} [{dlc}] {data_hex}"

            elif fmt == 0: # TEXT 模式
                text_data = payload.decode('utf-8', errors='ignore').strip()
                log_content = f"({timestamp_sec:.6f}) http_log: {text_data}"

            # 同時印到螢幕並寫入 CSV
            if log_content:
                # 1. Stdout
                print(log_content)
                
                # 2. CSV
                writer.writerow({
                    'serial num': count,
                    'epoch time': f"{timestamp_sec:.6f}",
                    'relative time': f"{relative_time:.6f}",
                    'stdout': log_content
                })

    print(f"\n解析完成！共處理 {count} 筆資料。")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python parse_log.py <file.bin>")
    else:
        parse_log(sys.argv[1])