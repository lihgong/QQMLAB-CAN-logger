import struct
import os
import csv

# 根據你的 C 結構體定義
HEADER_SIZE = 1024
DATA_OFFSET = 1024
ENTRY_HEADER_SIZE = 16  # packed: B(1) + B(1) + H(2) + I(4) + Q(8)

def parse_sdlog_file(bin_path):
    if not os.path.exists(bin_path):
        print(f"找不到檔案: {bin_path}")
        return

    with open(bin_path, 'rb') as f:
        # 1. 解析 1024-byte Global Header (sdlog_header_sys_t)
        # 對應你的結構: magic[8], ver(I), h_sz(I), epoch(Q), sys_start(Q), type_ch(I), board[32], fw[16]...
        header_data = f.read(HEADER_SIZE)
        if len(header_data) < HEADER_SIZE:
            return

        # <8s: magic[8]
        # II: version(4), header_sz(4)
        # QQ: us_epoch_time(8), us_sys_time(8)
        # I:  type_ch(4)
        # 32s16s: board_name[32], firmware_ver[16]
        header_fmt = '<8sIIQQI32s16s'
        header_res = struct.unpack_from(header_fmt, header_data)
        
        magic = header_res[0].decode('ascii').strip('\x00')
        version = header_res[1]
        header_sz = header_res[2]
        epoch_us = header_res[3]
        sys_start_us = header_res[4]
        ch_type = header_res[5]  # 0:HTTP, 1:CAN
        
        if magic != "QQMLAB":
            print(f"Magic 錯誤: {magic}")
            return

        print(f"--- 檔案標頭解析成功 ---")
        print(f"版本: {version}, 頻道類型: {ch_type} (0:HTTP, 1:CAN)")
        print(f"基準 Epoch Time: {epoch_us}")
        print(f"錄製開始 SysTime: {sys_start_us}")
        print(f"------------------------")

        # 2. 準備輸出 CSV
        csv_path = bin_path.replace('.BIN', '.csv').replace('.bin', '.csv')
        f.seek(DATA_OFFSET)
        
        with open(csv_path, 'w', newline='', encoding='utf-8') as csvfile:
            writer = csv.writer(csvfile)
            # 欄位：相對微秒, 絕對微秒, 子類型, 內容
            writer.writerow(['rel_time_us', 'abs_epoch_us', 'type_data', 'content_view'])

            count = 0
            while True:
                # 3. 讀取 Packed Data Entry Header (16 bytes)
                # 對應你的結構: magic(B), type(B), res(H), len(I), time(Q)
                hdr_data = f.read(ENTRY_HEADER_SIZE)
                if len(hdr_data) < ENTRY_HEADER_SIZE:
                    break
                
                m_w, t_data, res, p_len, t_us = struct.unpack('<BBHIQ', hdr_data)

                if m_w != 0xA5:
                    # 如果同步位元錯位，這裡會發生問題，通常是因為 padding 沒算對
                    continue

                # 讀取 Payload
                payload = f.read(p_len)
                
                # 4. 處理 8-byte 對齊 Padding
                # C code: (p_cmd->length + 7) / 8 * 8 - p_cmd->length
                pad_len = (p_len + 7) // 8 * 8 - p_len
                if pad_len > 0:
                    f.read(pad_len)

                # --- 關鍵計算：時間戳 ---
                # rel_time = 現在的開機微秒 - 錄製開始時的開機微秒
                rel_time = t_us - sys_start_us
                abs_time = epoch_us + rel_time

                # 內容解碼
                if ch_type == 0: # HTTP
                    try:
                        display_content = payload.decode('utf-8', errors='ignore').strip('\x00')
                    except:
                        display_content = payload.hex()
                else:
                    display_content = payload.hex()

                writer.writerow([rel_time, abs_time, t_data, display_content])
                count += 1

    print(f"轉檔完成！共處理 {count} 筆資料，輸出至 {csv_path}")

if __name__ == "__main__":
    import sys
    target = sys.argv[1] if len(sys.argv) > 1 else 'LOG.bin'
    parse_sdlog_file(target)    