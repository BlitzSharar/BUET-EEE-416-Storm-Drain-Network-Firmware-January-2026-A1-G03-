#ifndef SD_LOG_H
#define SD_LOG_H
#include "lora_rx.h"
#include "blockage.h"

// Local archival log on microSD.
// The dashboard is a live view, not a system of record: its free tier keeps
// limited history and it is unreachable whenever the internet is. The season
// long dataset this project depends on is written here first, and uploaded
// second, so a network outage costs visibility and not data.
void sdlog_init();
bool sdlog_ok();
void sdlog_report(const NodeReport& rep, BlockageVerdict verdict, const char* reason);
void sdlog_event(const char* kind, const char* detail);
void sdlog_flush();
#endif
