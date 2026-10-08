#pragma once
#include "common.h"

extern const uint8_t* g_isOnlineFlag;

bool ApplyOfflinePatches();
void LogOfflinePatches();
bool AllSystemsBooted();   // the PU_All boot map went in: Pyro and Nyx are loaded too
