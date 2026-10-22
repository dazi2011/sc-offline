#pragma once
#include "common.h"

// A workaround for Rosetta 2 (macOS 27, the Mac's x86-64 translator that Wine runs the game on):
// in one translated block, after a scalar conversion, square root or rounding into an XMM register
// ((v)cvtss2sd, sqrtsd, roundsd, cvtsd2ss, sqrtss, roundss, rcpss, rsqrtss), a VEX vector load into
// that register (vmovups, vmovupd, ...) doesn't reach the next scalar operation of the same
// precision that reads it as its second source: vmulsd/vaddsd/vsubsd/... still see the converted
// value. The physics damping step is one such block (exe+0x65c4cbd in 4.10.196): every damped rigid
// body's angular momentum x became F*F (F the damping factor, ~1), so props, debris and vehicles
// spun about their zone's x axis and couldn't keep a real spin about it.
//
// Each known site's load is rewritten as the same SSE load (movups/movupd, plus a nop), which
// Rosetta translates correctly. It's the same operation: the VEX conversion just before it zeroed
// the register's upper half and nothing in between writes it, so not zeroing it again changes
// nothing. A site whose row doesn't resolve is left alone.
//
// The loads are sco signature rows ("rosetta.stale_load.<k>", registered here). Only under Wine on
// macOS; SC_OFFLINE_ROSETTA_FIX=0 turns it off, =1 turns it on anywhere.
void RosettaFix_RegisterSignatures();   // StartOffline, before sco::ResolveAll
void RosettaFix_Apply();                // StartOffline, after it, before the game's own code runs
