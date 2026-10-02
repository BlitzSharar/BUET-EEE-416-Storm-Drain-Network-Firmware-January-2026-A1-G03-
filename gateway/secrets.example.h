// Copy this file to secrets.h in the same folder and fill in your own values.
// secrets.h is listed in .gitignore so real credentials never reach the
// repository.

#ifndef STORMDRAIN_SECRETS_H
#define STORMDRAIN_SECRETS_H

// Phone hotspot or router the gateway joins for internet. It must be 2.4 GHz.
#define WIFI_SSID           "your-hotspot-name"
#define WIFI_PASS           "your-hotspot-password"

// From the Blynk console, device info page.
#ifndef BLYNK_TEMPLATE_ID
#define BLYNK_TEMPLATE_ID   "TMPLxxxxxxxx"
#endif
#ifndef BLYNK_TEMPLATE_NAME
#define BLYNK_TEMPLATE_NAME "StormDrain"
#endif
#ifndef BLYNK_AUTH_TOKEN
#define BLYNK_AUTH_TOKEN    "your-device-auth-token"
#endif

#endif
