#ifndef __WIFI_PASSWD_H__
#define __WIFI_PASSWD_H__

#define WIFI_PASSWD_FROM_CODE

#if defined(WIFI_PASSWD_FROM_CODE)
#define WIFI_SSID "SSID_GOES_HERE"
#define WIFI_PASS "PASSWORD_GOES_HERE"
#endif

#define WIFI_PASSWD_FROM_SD_CARD
// During boot, we will load /sdcard/wifi.txt
// FILE format
// ssid1
// password-for-ssid1
// ssid2
// password-for-ssid2

#endif // __WIFI_PASSWD_H__
