# seven-segment-counter


## secrets configuration

The secrets needed for the counter to connect to the Internet via an existing WiFi network and the settings for it's own local network used for OTA programming is found in a separate configuration file, the `secrets.ini` file.

The `secrets.ini` file is ignored in the Git configuration to keep it local and prevent it from being pushed to a public repository.

This file needs to be created before the project can be compiled. To do this there is a template file, `secrets.ini.temp`, that can be copied and modified. I e, copy `secrets.ini.temp` to `secrets.ini` and edit the latter.

There are four macros in the `secrets.ini` configuration file that needs to be given correct values: SOFT_AP_SSID, SOFT_AP_PWD, WIFI_SSID and WIFI_PWD.

