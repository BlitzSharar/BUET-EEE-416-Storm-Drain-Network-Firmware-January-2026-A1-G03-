#ifndef NET_REPORT_H
#define NET_REPORT_H
#include "lora_rx.h"
#include "blockage.h"
void net_init();
bool net_have_internet();   // true only when there is a route out
void net_run();
void net_publish(const NodeReport& rep, BlockageVerdict verdict);   // datastreams + push alerts
void net_check_offline();                  // alert when a node goes silent
#endif
