#ifndef DATA_EXPORT_H
#define DATA_EXPORT_H

// Pull the archive off the gateway without touching the card.
//
// Two routes, both over hardware that is already wired. No connection changes.
//
//   WiFi   browse to http://<gateway-ip>/ and click the file. This is the one
//          to use. The gateway prints its address at boot.
//   Serial type 'd' into the Serial Monitor and the file is printed. Slower and
//          it needs copy and paste, but it works with no network at all.
//
// Why not USB mass storage: the ESP32-WROOM-32 has no USB peripheral. Its USB
// socket is wired to a CP2102 serial bridge, so the chip can only ever speak
// serial over that cable. Appearing as a removable drive needs an ESP32-S2, S3
// or C3. Nothing in firmware can work around it on this board.

void dexport_begin();     // call once in setup(), after net_init()
void dexport_poll();      // call every loop(), cheap when idle

#endif
