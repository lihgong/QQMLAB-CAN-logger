==== Introduction ====
1. The board: https://wiki.luatos.com/chips/esp32c3/board.html (with no CH340)
2. The SDK version v5.5.2 (2026/1/1, the latest version)
3. Build with VSCODE + ESP-IDF extension


==== TIPS ====
1. In VSCODE ESP-IDF terminal, enable ccache to speed up build
idf.py ccache-enable

2. In Windows security, antivirus & threat protection, Manages setting, Exclude, 
Add following folders to make build speed faster (avoid scanning SDK related things)
C:\Espressif
C:\Users\[name]\.espressif
C:\Users\[name]\esp
C:\Users\[name]\proj_root

3. Temporal fix, I put WIFI password temporally here and execute following command
to avoid submitting WIFI password
git update-index --assume-unchanged main/wifi_passwd.h

to rever the setting above
git update-index --no-assume-unchanged main/wifi_passwd.h

4. VSCODE always tried to add COM port setting, that I don't want to submit
I also run following command to exclude it
git update-index --assume-unchanged .vscode/settings.json

5. Add .clang-format that lets the tool arrange K&R style that I expect to see

==== Development Journal ====
2026/1/1
Initial bring up of the board with WIFI

2026/1/2
1. Add project formatter, I don't want to adjust indent by hand
2. Commit the WIFI code, without explicitly submitting the WIFI password
3. Add cheatsheet of everything special I made


2026/1/3
1. With SD card and WIFI, I can implement OTA (which is convenient if I deploy to somewhere)
2. Add SD card to ESP32C3 board may fail (soldering), and I want to avoid it now
3. I have ESP32-CAM with micro-SD interface, and switch to that board to develop it temporally
4. The most convenient method to burn flash is still in VSCODE + Espresisf
5. ESPCONNECT site can burn the image over web, we need to merge binary first
esptool.py --chip esp32 merge_bin -o factory.bin --flash_mode dio --flash_size 4MB 0x1000 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/esp32_can_logger.bin


mdns sometimes were removed from core package and we will have build error
execute following line to include it
idf.py add-dependency "espressif/mdns^1.2.0"