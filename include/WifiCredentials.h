// WifiCredentials.h

#include <stddef.h>

struct WifiNetworkCredential
{
  const char *ssid;
  const char *password;
};

const WifiNetworkCredential WIFI_NETWORKS[] = {
    // Credenziali modem Roma
    {"FASTWEB-F7D7CB", "48WJAR7FWJ"},

    // Credenziali modem Ladispoli
    {"TIM_2rN62P", "4JTW3BQA66"},
};

const size_t WIFI_NETWORK_COUNT = sizeof(WIFI_NETWORKS) / sizeof(WIFI_NETWORKS[0]);
