#pragma once
#include "common.h"

// A workaround for Apple's D3DMetal (CrossOver, Game Porting Toolkit): in a frame that draws planet
// terrain, what the render targets held before the terrain's tessellated G-buffer pass is gone by
// the time the next pass starts. Ships and cockpits in view of a planet are then lit from an empty
// G-buffer (flat purple, glowing panel lines). Flushing the device context just before the first
// tessellated draw of each pass keeps them; it costs a few percent of frame rate.
//
// Only under Wine on macOS (D3D11 translated to Metal); SC_OFFLINE_TESS_FLUSH=0 turns it off, =1
// turns it on anywhere.
void TessFlush_Install();   // ModThread, early; makes a throwaway device on a thread of its own
bool TessFlush_WineOnMac();  // Wine on macOS (ntdll's wine_get_host_version says Darwin)
