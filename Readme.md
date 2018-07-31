# ota_esphttpclient

This is a ota implementation for esp-idf's esp_http_client library.
___

As the original ota-example from espressive is not really usable as it is, I took it as a challange to impelemnt the new esp_http_client library.

This Library was tested with a ESP32_Core_Board_V2

There are plenty of unnescessary ESP_LOG* still in the sources. Feel free to remove them.

__Instructions:__

```
#include "otahandler.h"

initialise_wifi();
xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT,false, true,portMAX_DELAY);
startOTA("http://yourserver.redirectme.net", 8000, "ota.bin", OTAMODE_HOT);
```
