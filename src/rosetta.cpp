#include "rosetta.h"
#include "tessflush.h"
#include "sco/signatures.h"

// The sites, found by scanning StarCitizen.exe 4.10.196.36804 for the pattern in one basic block
// (a scalar conversion, square root or rounding into xmmN, a VEX load into xmmN, then a scalar
// operation reading xmmN as its second source). Each is a signature row "rosetta.stale_load.<k>":
// the load with 8 bytes either side (unique in .text). kFix says where the load starts in the
// row's match, its length and the SSE load that replaces it.
namespace {

const sco::SigDef kRows[] = {
    // vmovups xmm8, [rsp+0xF8]; then vaddsd xmm0, xmm2, xmm8 and vaddsd xmm3, xmm1, xmm8
    { "rosetta.stale_load.1", "EB 5C D8 C4 C1 53 58 C8 C5 78 10 84 24 F8 00 00 00 C5 F3 58 D5 C4 C1 6B 58", 0, 0, nullptr, {} },
    // vmovupd xmm1, [rbp+0xE0]; then vaddsd xmm0, xmm2, xmm1
    { "rosetta.stale_load.2", "C1 32 5A C9 C5 F3 58 D4 C5 F9 10 8D E0 00 00 00 C5 EB 58 C1 C5 FB 58 DC", 0, 0, nullptr, {} },
    // vmovups xmm2, [rbx+0x68]; then vmulsd xmm5, xmm0, xmm2
    { "rosetta.stale_load.3", "6B 59 D0 C5 F2 59 41 34 C5 F8 10 53 68 C5 9A 5D E0 C5 DA 5A C4", 0, 0, nullptr, {} },
    // vmovups xmm3, [rax+0x38]; then vmulsd xmm0, xmm7, xmm3
    { "rosetta.stale_load.4", "C5 73 58 40 18 48 8B 06 C5 F8 10 58 38 C5 FB 10 48 08 C5 C3 59", 0, 0, nullptr, {} },
    // vmovups xmm1, [rdi+0x20]; then vaddsd xmm3, xmm0, xmm1
    { "rosetta.stale_load.5", "22 5A CB C4 C1 73 59 D4 C5 F8 10 4F 20 C4 C1 12 5C C6 C5 FA 5A", 0, 0, nullptr, {} },
    // vmovupd xmm1, [rbp+0xC0]; then vmulsd xmm2, xmm4, xmm1
    { "rosetta.stale_load.6", "C5 F2 5A C9 C5 F3 58 E7 C5 F9 10 8D C0 00 00 00 C5 DB 59 D1 C5 CB 59 4D", 0, 0, nullptr, {} },
    // vmovups xmm13, [rdi]; then vaddsd xmm0, xmm1, xmm13
    { "rosetta.stale_load.7", "1A 5A E4 C4 C1 43 59 C5 C5 78 10 2F C5 E3 5C D0 C4 C1 6B 58", 0, 0, nullptr, {} },
    // vmovups xmm0, [rbp+0x40]; then vsubsd xmm5, xmm10, xmm0
    { "rosetta.stale_load.8", "EA 5A C2 C5 FB 58 4D 10 C5 F8 10 45 40 C4 41 53 5C C2 C5 AB 5C", 0, 0, nullptr, {} },
    // vmovupd xmm1, [rsp+0x70]; then vaddsd xmm3, xmm2, xmm1
    { "rosetta.stale_load.9", "C1 22 5A CB C5 EB 59 D1 C5 F9 10 4C 24 70 C5 EB 59 D8 C5 FB 59 54", 0, 0, nullptr, {} },
    // physics: vmovupd xmm1, [rsp+0x490]; then vaddsd xmm3, xmm0, xmm1
    { "rosetta.stale_load.10", "C5 F2 5A C9 C5 F3 58 D5 C5 F9 10 8C 24 90 04 00 00 C5 7B 5A D0 C5 EB 58 C5", 0, 0, nullptr, {} },
    // physics, rigid body damping: vmovups xmm1, [r13] (the angular momentum); then vmulsd xmm0, xmm0, xmm1
    { "rosetta.stale_load.11", "C1 7B 11 86 A0 06 00 00 C4 C1 78 10 4D 00 C4 C1 7B 10 55 10 C5 CA", 0, 0, nullptr, {} },
    // vmovups xmm0, [rsp+0x160]; then vaddsd xmm0, xmm3, xmm0
    { "rosetta.stale_load.12", "FA 5A C0 C4 C1 7B 59 CD C5 F8 10 84 24 60 01 00 00 C5 E3 58 E2 C5 DB 58 D1", 0, 0, nullptr, {} },
    // vmovups xmm0, [rbp+0x68]; then vsubsd xmm0, xmm1, xmm0
    { "rosetta.stale_load.13", "C5 FA 5A C0 C5 E3 59 E0 C5 F8 10 45 68 C5 F3 5C C0 C5 FB 59 CF", 0, 0, nullptr, {} },
    // vmovups xmm5, [r10+rax*8]; then vmulsd xmm0, xmm0, xmm5
    { "rosetta.stale_load.14", "C5 D9 15 E4 48 8D 04 49 C4 C1 78 10 2C C2 C5 FB 59 C5 C5 FB 59 CF", 0, 0, nullptr, {} },
    // vmovups xmm7, [r10+rax*8+0x198]; then vmulsd xmm0, xmm0, xmm7
    { "rosetta.stale_load.15", "C5 EB 59 D1 C5 FB 59 CF C4 C1 78 10 BC C2 98 01 00 00 C4 C1 6B 58 DD C5 F3 59", 0, 0, nullptr, {} },
    // vmovups xmm6, [r10+rax*8+0x1F8]; then vmulsd xmm1, xmm0, xmm6
    { "rosetta.stale_load.16", "C5 F2 5C C3 4B 8D 04 40 C4 C1 78 10 B4 C2 F8 01 00 00 C5 7A 5A E0 C5 FA 10 84", 0, 0, nullptr, {} },
    // vmovups xmm0, [rsp+0x20]; then vaddss xmm0, xmm15, xmm0 (single precision, after vcvtsd2ss)
    { "rosetta.stale_load.17", "C5 FB 5A C0 C5 72 58 D0 C5 F8 10 44 24 20 C5 82 58 C0 C5 78 28 7C", 0, 0, nullptr, {} },
};
constexpr int kRowCount = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));

// Per row: the VEX load's offset in the match and length, and the SSE load (plus a nop) of the same length.
struct Fix { int at; int len; uint8_t repl[10]; };
const Fix kFix[kRowCount] = {
    { 8, 9,  { 0x44, 0x0F, 0x10, 0x84, 0x24, 0xF8, 0x00, 0x00, 0x00 } },        // movups xmm8, [rsp+0xF8]
    { 8, 8,  { 0x66, 0x0F, 0x10, 0x8D, 0xE0, 0x00, 0x00, 0x00 } },              // movupd xmm1, [rbp+0xE0]
    { 8, 5,  { 0x0F, 0x10, 0x53, 0x68, 0x90 } },                                // movups xmm2, [rbx+0x68]; nop
    { 8, 5,  { 0x0F, 0x10, 0x58, 0x38, 0x90 } },                                // movups xmm3, [rax+0x38]; nop
    { 8, 5,  { 0x0F, 0x10, 0x4F, 0x20, 0x90 } },                                // movups xmm1, [rdi+0x20]; nop
    { 8, 8,  { 0x66, 0x0F, 0x10, 0x8D, 0xC0, 0x00, 0x00, 0x00 } },              // movupd xmm1, [rbp+0xC0]
    { 8, 4,  { 0x44, 0x0F, 0x10, 0x2F } },                                      // movups xmm13, [rdi]
    { 8, 5,  { 0x0F, 0x10, 0x45, 0x40, 0x90 } },                                // movups xmm0, [rbp+0x40]; nop
    { 8, 6,  { 0x66, 0x0F, 0x10, 0x4C, 0x24, 0x70 } },                          // movupd xmm1, [rsp+0x70]
    { 8, 9,  { 0x66, 0x0F, 0x10, 0x8C, 0x24, 0x90, 0x04, 0x00, 0x00 } },        // movupd xmm1, [rsp+0x490]
    { 8, 6,  { 0x41, 0x0F, 0x10, 0x4D, 0x00, 0x90 } },                          // movups xmm1, [r13]; nop
    { 8, 9,  { 0x0F, 0x10, 0x84, 0x24, 0x60, 0x01, 0x00, 0x00, 0x90 } },        // movups xmm0, [rsp+0x160]; nop
    { 8, 5,  { 0x0F, 0x10, 0x45, 0x68, 0x90 } },                                // movups xmm0, [rbp+0x68]; nop
    { 8, 6,  { 0x41, 0x0F, 0x10, 0x2C, 0xC2, 0x90 } },                          // movups xmm5, [r10+rax*8]; nop
    { 8, 10, { 0x41, 0x0F, 0x10, 0xBC, 0xC2, 0x98, 0x01, 0x00, 0x00, 0x90 } },  // movups xmm7, [r10+rax*8+0x198]; nop
    { 8, 10, { 0x41, 0x0F, 0x10, 0xB4, 0xC2, 0xF8, 0x01, 0x00, 0x00, 0x90 } },  // movups xmm6, [r10+rax*8+0x1F8]; nop
    { 8, 6,  { 0x0F, 0x10, 0x44, 0x24, 0x20, 0x90 } },                          // movups xmm0, [rsp+0x20]; nop
};

// Off, on, or forced on anywhere, from SC_OFFLINE_ROSETTA_FIX and the host.
const char* g_skip = nullptr;   // why the fix is off; nullptr when on
bool        g_force = false;

}  // namespace

void RosettaFix_RegisterSignatures() {
    char env[8] = "";
    const DWORD len = GetEnvironmentVariableA("SC_OFFLINE_ROSETTA_FIX", env, sizeof(env));
    const bool set = len > 0 && len < sizeof(env);
    g_force = set && strcmp(env, "1") == 0;
    if (set && strcmp(env, "0") == 0) g_skip = "SC_OFFLINE_ROSETTA_FIX=0";
    else if (!g_force && !TessFlush_WineOnMac()) g_skip = "not Wine on macOS";
    if (g_skip) return;
    if (!sco::RegisterSignatures(kRows, kRowCount)) g_skip = "its signature rows did not register";
}

void RosettaFix_Apply() {
    if (g_skip) { Log("[cpu] Rosetta stale-scalar fix: off (%s)", g_skip); return; }
    int patched = 0, missing = 0;
    uint8_t* first = nullptr;
    for (int k = 0; k < kRowCount; ++k) {
        uint8_t* hit = sco::Sig(kRows[k].id);
        if (!hit) { ++missing; continue; }
        uint8_t* at = hit + kFix[k].at;
        DWORD err = 0;
        if (!WriteCode(at, kFix[k].repl, static_cast<size_t>(kFix[k].len), err)) {
            Log("[!] Rosetta stale-scalar fix: VirtualProtect failed at 0x%p (%lu)", static_cast<void*>(at), err);
            continue;
        }
        if (!first) first = at;
        ++patched;
    }
    Log("[cpu] Rosetta stale-scalar fix: %d of %d loads patched%s, first at 0x%p (%s; props and debris spun about "
        "their zone's x axis); SC_OFFLINE_ROSETTA_FIX=0 turns it off", patched, kRowCount,
        missing ? " (the other rows don't match this game build)" : "", static_cast<void*>(first),
        g_force ? "forced" : "Wine on macOS");
}
