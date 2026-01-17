import struct
import csv
import os

def parse_qqmlab_log(input_file, csv_output):
    if not os.path.exists(input_file):
        print(f"錯誤: 找不到檔案 {input_file}")
        return

    results = []
    
    with open(input_file, "rb") as f:
        # --- 1. 解析 System Header (512 Bytes) ---
        sys_header_data = f.read(512)
        if len(sys_header_data) < 512:
            print("檔案長度不足，無法讀取 System Header")
            return

        # 格式串說明: 
        # < (Little-Endian), 8s(Magic), I(Ver), I(Sz), Q(Epoch), Q(SysBase), I(Ch), 32s(Board), 16s(FW), I(MetaOff), I(DataOff)
        sys_fmt = "<8sIIQQI32s16sII"
        sys_fields = struct.unpack_from(sys_fmt, sys_header_data)
        
        magic       = sys_fields[0].decode('ascii', errors='ignore').strip('\x00')
        version     = sys_fields[1]
        sys_base_us = sys_fields[4]  # 這是檔案建立時的 esp_timer_get_time()
        data_offset = sys_fields[9]  # 修正後的索引: 9

        # --- 2. 解析 User Metadata (512 Bytes) ---
        f.seek(512)
        meta_data = f.read(512).decode('ascii', errors='ignore').strip('\x00')

        print("="*70)
        print(f"QQMLAB CAN LOGGER - 檔案解析報告")
        print(f"  Magic String:  {magic}")
        print(f"  系統版本:      v{version}")
        print(f"  硬體平台:      {sys_fields[6].decode('ascii', errors='ignore').strip()}")
        print(f"  用戶描述:      {meta_data}")
        print(f"  數據起始偏移:  {data_offset} bytes")
        print("="*70)

        # --- 3. 解析 Data Entries ---
        f.seek(data_offset)
        entry_id = 1
        
        while True:
            current_pos = f.tell()
            prefix_data = f.read(16) # 16-byte sdlog_data_t
            if len(prefix_data) < 16:
                break
            
            # 格式串: <BB2xIQ -> Magic(1B), Type(1B), Reserved(2x), Len(4B), Time(8B)
            sync_byte, d_type, p_len, d_time = struct.unpack("<BB2xIQ", prefix_data)
            
            # 檢查同步字元 A5
            if sync_byte != 0xA5:
                # 若遺失同步，通常是因為檔案損毀或偏移錯誤
                print(f"警告: 偏移量 {current_pos:06X} 遺失同步字元 (Got {hex(sync_byte)})")
                break

            # 讀取 Payload
            payload = f.read(p_len)
            
            # 核心邏輯: 根據 (len+7)/8*8 計算並跳過對齊用的 Padding
            pad_len = ((p_len + 7) // 8 * 8) - p_len
            if pad_len > 0:
                f.read(pad_len)

            # 計算相對時間 (Microseconds & Milliseconds)
            rel_us = d_time - sys_base_us
            rel_ms = rel_us / 1000.0
            
            # 嘗試轉換文字內容 (針對 HTTP/Text 類型)
            try:
                content_str = payload.decode('ascii', errors='ignore').replace('\n', ' ').strip()
            except:
                content_str = payload.hex()

            results.append({
                'entry_id': entry_id,
                'offset_hex': f"0x{current_pos:X}",
                'rel_time_us': rel_us,      # 新增的微秒刻度 
                'rel_time_ms': f"{rel_ms:.3f}",
                'type': d_type,
                'length': p_len,
                'content': content_str
            })
            
            print(f"#{entry_id:03d} | +{rel_ms:10.2f}ms | Len: {p_len:>5} | {content_str[:50]}...")
            entry_id += 1

    # --- 4. 儲存至 CSV ---
    with open(csv_output, 'w', newline='', encoding='utf-8') as cf:
        headers = ['entry_id', 'offset_hex', 'rel_time_us', 'rel_time_ms', 'type', 'length', 'content']
        writer = csv.DictWriter(cf, fieldnames=headers)
        writer.writeheader()
        writer.writerows(results)
    
    print("="*70)
    print(f"解析完成! 共處理 {len(results)} 筆數據。")
    print(f"CSV 報表已儲存至: {csv_output}")

if __name__ == "__main__":
    # 將檔案路徑改為你的 LOG.TXT 實際位置
    parse_qqmlab_log("LOG.TXT", "log_analysis.csv")