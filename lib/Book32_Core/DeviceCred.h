#pragma once
// Book32 v1.6.2 — fixed device credential.
//
// Rationale (security): v1.4.x brought up the management SoftAP with
// WiFi.softAP(AP_SSID) and no password, and served the whole HTTP API with no
// authentication. Anyone within radio range could upload and delete books,
// read the home network's SSID, and trigger an OTA update. v1.5.0 closed that
// with a credential derived from the WiFi MAC.
//
// v1.6.2 replaced the derivation with a fixed credential, by owner request:
// the derived value had to be read off the e-ink screen (or computed from the
// MAC) before the web interface could be used, which made the new /send page
// awkward on a phone. The value below is used both as the SoftAP WPA2
// passphrase and as the HTTP Basic Auth password.
//
// Threat model — be honest about what this does and does not achieve:
//   * It stops accidental connections and casual poking on the home LAN.
//   * It does NOT stop anyone who can read this source. The credential is a
//     literal in a source file, so if this repository is public the password
//     is public. HTTP Basic Auth also sends it base64-encoded over plaintext
//     HTTP. Treat it as a speed bump, not as authentication.
//
// WPA2 requires a passphrase of 8..63 characters, so BOOK32_DEVICE_PASSWORD
// must stay within that range or the offline hotspot will fail to start.

// Fixed HTTP Basic Auth username.
#define BOOK32_AUTH_USER "book32"

// Fixed password. Also the SoftAP WPA2 passphrase — keep it 8..63 characters.
#define BOOK32_DEVICE_PASSWORD "book32000"
