# CYD_Min-Max_TempLogger
Min/Max Temperature Logger using a Cheap Yellow Display (CYD) &amp; DS18B20 Temperature Sensor Module

I used a CYD which I purchased from Temu and will link to the exact product page below to create a Minimum / Maximum temperature logger using a DS18B20 Temperature Sensor Module and SD card. The product listing states the use of an ILI9341 panel, but after some back and forth it turned out to actually be an ST7789 panel.
<img width="1781" height="1327" alt="image" src="https://github.com/user-attachments/assets/84c03b75-caee-41ea-9ff5-b22431ab001c" />

Temperature is logged and can be reset on one of the 3 screens which can be scrolled through form left to right, a brightness slider as well as automatic brightness toggle is also included. On the 3rd screen you will find daily minimum and maximum temperatures which were logged for the current day as well as the past 7 days.
<img width="1708" height="1278" alt="image" src="https://github.com/user-attachments/assets/4d57dcce-b8bb-42b6-93b9-8542ab7f0609" />
<img width="1910" height="1433" alt="image" src="https://github.com/user-attachments/assets/42099265-88b8-4433-a8da-bde1e50f3807" />

As part of the functionality, I have also added an access point to the device (this device will be used at a location where network access is not available), for which the default SSID is "TempLoggerAP" with password "tempLoggerPassword2025", feel free to change these or to add in WiFiManager for extra functionality if a network will be available for your device. The default IP address for the webserver is 192.168.4.1 (or you can access it at http://templogger.local once connected to the AP), doing so will sync your device's time to the TempLogger which adds the loggings functionality.

The device logs temperatures every 10 seconds, the data gets stored in CSV files on an inserted SD card which can be deleted or downloaded to a device connected to the webserver. The CSV files are stored per month and the oldest CSV file / month gets deleted when space becomes an issue which allows for continuous use.

Additional functionality which will probably be added in the future includes a lithium battery with charge controller, possibly an RTC clock as well. I played around with adding charts to both the device itself as well as the webserver, but I became severely constrained by not having PSRAM and the limited SRAM the device has, so removed the functionality in lieu of stability.

The onboard RGB LED serves a few functions:
A fast red blinking LED indicates that the temperature is not being sensed (possibly sensor disconnect)
A slow red blinking LED indicates that the SD card is not available for whatever reason
A solid blue LED indicates that the time is not set/synced (without the time the logging function is useless)
When things function normally, the LED should light up green intermittently indicating every time data gets written to the SD card (every 10 seconds)

This is the exact device I purchased (the link might become unavailable in the future):
https://www.temu.com/goods.html?_bg_fs=1&goods_id=601101590266969&_oak_stage=6&_oak_page_source=703&top_gallery_url=https%3A%2F%2Fimg.kwcdn.com%2Fproduct%2Ffancy%2F8426ccc7-f7f3-468e-870e-680f9f65aeef.jpg&refer_page_name=home&refer_page_id=10005_1766426406051_fpfrn48qh8&refer_page_sn=10005&_x_sessn_id=dip2alamwp&no_cache_id=mx76d

And of course, I would've have been able to make this without the extensive knowledge on offer from Brian Lough on his ESP32-Cheap-Yellow-Display repo:
https://github.com/witnessmenow/ESP32-Cheap-Yellow-Display/tree/main?tab=readme-ov-file
