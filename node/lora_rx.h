#ifndef LORA_RX_H
#define LORA_RX_H
void lora_rx_listen();               // put radio back into continuous receive
void lora_rx_window(unsigned long ms); // short blocking listen after a TX
void lora_rx_poll();                 // parse a pending gateway broadcast, if any
#endif
