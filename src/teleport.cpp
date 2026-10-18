#include "teleport.h"
#include "menu.h"
#include "builtins/builtin_store.h"
#include "sco/game/teleport.h"
#include "sco/signatures.h"
#include <cmath>
#include <share.h>

TeleportApi g_tp;

// The addresses are sco-core's teleport.* signature rows (src/game/teleport_sigs.cpp: the scan
// and checks this file used to do, moved byte for byte), resolved by ResolveAll in StartOffline.
bool ResolveTeleportApi() {
    sco::game::TeleportAddrs a;
    if (!sco::game::TeleportAddresses(a)) {
        // to_camera holds the checks; the other rows only fail when it does.
        const sco::SigResult* r = sco::SigLookup("teleport.to_camera");
        Log("[tp] TeleportToCamera not found (teleport.to_camera %s: %s); teleport disabled",
            r ? sco::SigStateName(r->state) : "unknown", r && r->why ? r->why : "see [core] below");
        return false;
    }
    g_tp.clientMgr    = a.clientMgr;
    g_tp.handleFromId = a.handleFromId;
    g_tp.entitySystem = a.entitySystem;
    g_tp.ok = true;
    return true;
}

constexpr double kMaxLocalCoord = 1.0e12;

// Returned when the only zone a spot shares with you is the universe root: the spot is in another
// star system, which isn't loaded, so teleporting there leaves you floating in nothing.
static const char* const kOtherSystem = "这个位置在另一个星系，请先前往那个星系";

static bool PositionLooksValid(const double p[3]) {
    for (int i = 0; i < 3; ++i)
        if (!(p[i] > -kMaxLocalCoord && p[i] < kMaxLocalCoord)) return false;
    return true;
}

bool GetLocalPlayer(uintptr_t& actor, uintptr_t& entity) {
    const uintptr_t mgr = *g_tp.clientMgr;
    if (!mgr) return false;
    const uintptr_t sub = Rd<uintptr_t>(mgr + 0xE0);
    if (!sub) return false;
    const uintptr_t info = VCall<uintptr_t>(sub, 0x2E0);
    if (!info) return false;
    uint64_t handle = 0;
    reinterpret_cast<void(__fastcall*)(uint64_t*, uint64_t)>(g_tp.handleFromId)(&handle, Rd<uint64_t>(info + 8));
    actor = handle & kPtrMask;
    if (!actor) return false;
    const uintptr_t life = VCall<uintptr_t>(actor, 0xA08);
    if (!life || !VCall<uintptr_t>(life, 0x28)) return false;
    entity = Rd<uintptr_t>(actor + 8) & kPtrMask;
    return entity != 0;
}

uintptr_t ZoneParent(uintptr_t zone) { return VCall<uintptr_t>(zone, 0x08); }
const char* ZoneName(uintptr_t zone) { return VCall<const char*>(zone, 0x218); }

uint64_t ZoneId(uintptr_t zone) {
    uint8_t tmp[16] = {};
    const uint64_t* id = VCall<const uint64_t*>(zone, 0x58, tmp);
    return id ? *id : 0;
}

uintptr_t ZoneFromId(uint64_t zoneId) {
    const uintptr_t es = *g_tp.entitySystem;
    if (!es || !zoneId) return 0;
    const uintptr_t ent = VCall<uintptr_t>(es, 0x120, zoneId);
    return ent ? VCall<uintptr_t>(ent, 0x6E0) : 0;
}

void Vec3Out(uintptr_t obj, size_t off, double out[3]) {
    double buf[4] = {};
    const double* r = VCall<const double*>(obj, off, buf, uintptr_t(0));
    out[0] = r[0]; out[1] = r[1]; out[2] = r[2];
}

void LocalToWorld(uintptr_t zone, const double local[3], double world[3]) {
    double buf[4] = {};
    const double* r = VCall<const double*>(zone, 0x198, buf, local);
    world[0] = r[0]; world[1] = r[1]; world[2] = r[2];
}

static double Dot(const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A zone's frame in the world: its origin and the world directions of its x, y and z axes, probed
// through LocalToWorld. False when the axes aren't orthonormal (a frame this code can't invert).
static bool ZoneAxes(uintptr_t zone, double origin[3], double axis[3][3]) {
    constexpr double kArm = 1.0e6;
    const double zero[3] = {};
    LocalToWorld(zone, zero, origin);
    for (int i = 0; i < 3; ++i) {
        double probe[3] = {}, w[3];
        probe[i] = kArm;
        LocalToWorld(zone, probe, w);
        for (int k = 0; k < 3; ++k) axis[i][k] = (w[k] - origin[k]) / kArm;
    }
    for (int i = 0; i < 3; ++i) {
        if (fabs(Dot(axis[i], axis[i]) - 1.0) > 1e-6) return false;
        for (int j = i + 1; j < 3; ++j)
            if (fabs(Dot(axis[i], axis[j])) > 1e-6) return false;
    }
    return true;
}

bool WorldToLocal(uintptr_t zone, const double world[3], double local[3]) {
    double origin[3], axis[3][3];
    if (!ZoneAxes(zone, origin, axis)) return false;
    const double d[3] = { world[0] - origin[0], world[1] - origin[1], world[2] - origin[2] };
    for (int i = 0; i < 3; ++i) local[i] = Dot(axis[i], d);
    return PositionLooksValid(local);
}

int ReadZoneChain(uintptr_t zone, ZoneFrame* out, int max) {
    int n = 0;
    // steps bounds the walk on a parent cycle, since skipped zones don't count towards max.
    int steps = 0;
    for (uintptr_t z = zone; z && n < max && steps < 64; z = ZoneParent(z), ++steps) {
        const uint64_t id = ZoneId(z);
        if (!id || ZoneFromId(id) != z) continue;   // can't be named by id, so can't be queried
        ZoneFrame& f = out[n];
        if (!ZoneAxes(z, f.origin, f.axis)) continue;
        f.id = id;
        const char* name = ZoneName(z);
        strncpy_s(f.name, name ? name : "", _TRUNCATE);
        ++n;
    }
    return n;
}

static const char* CaptureSpot(Spot& s, double world[3]) {
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return "角色还没有生成";
        const uintptr_t zone = VCall<uintptr_t>(entity, 0x6B8);
        if (!zone) return "不在任何区域内";
        double local[3];
        Vec3Out(entity, 0x2B8, local);
        if (!PositionLooksValid(local)) return "位置超出范围";
        LocalToWorld(zone, local, world);
        s.n = 0;
        for (uintptr_t z = zone; z && s.n < kMaxZoneDepth; z = ZoneParent(z)) {
            const char* name = ZoneName(z);
            if (!name || !*name) continue;
            ZoneSpot& e = s.z[s.n];
            if (z == zone) memcpy(e.local, local, sizeof(local));
            else if (!WorldToLocal(z, world, e.local)) continue;
            strncpy_s(e.name, name, _TRUNCATE);
            ++s.n;
        }
        return s.n ? nullptr : "区域没有名称";
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "读取角色信息时出错";
    }
}

static int FindSavedZone(const Spot& s, uintptr_t entity, uintptr_t& zoneOut) {
    uintptr_t chain[kMaxZoneDepth];
    const char* names[kMaxZoneDepth];
    int n = 0;
    for (uintptr_t z = VCall<uintptr_t>(entity, 0x6B8); z && n < kMaxZoneDepth; z = ZoneParent(z)) {
        chain[n] = z;
        names[n++] = ZoneName(z);
    }
    for (int i = 0; i < s.n; ++i)
        for (int k = 0; k < n; ++k)
            if (names[k] && strcmp(names[k], s.z[i].name) == 0) { zoneOut = chain[k]; return i; }
    return -1;
}

static int SavedZoneLevel(const Spot& s) {
    __try {
        uintptr_t actor, entity, zone;
        return GetLocalPlayer(actor, entity) ? FindSavedZone(s, entity, zone) : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

static const char* TeleportToSpot(const Spot& s, int& level) {
    if (!s.n) return "还没有保存的位置";
    __try {
        uintptr_t actor, entity, zone = 0;
        if (!GetLocalPlayer(actor, entity)) return "角色还没有生成";
        level = FindSavedZone(s, entity, zone);
        if (level < 0) return "你不在保存位置所在的任何区域（换了星球或星系？）";
        if (level > 0 && !ZoneParent(zone)) return kOtherSystem;
        const uint64_t zoneId = ZoneId(zone);
        if (!zoneId || ZoneFromId(zoneId) != zone) return "区域 ID 不匹配";
        const double* local = s.z[level].local;
        double world[3];
        LocalToWorld(zone, local, world);
        alignas(16) uint8_t params[0x80] = {};
        *reinterpret_cast<uint64_t*>(params + 0x00) = zoneId;
        reinterpret_cast<double*>(params + 0x08)[3] = 1.0;
        memcpy(params + 0x28, local, 3 * sizeof(double));
        *reinterpret_cast<double*>(params + 0x40) = 1.0;
        memcpy(params + 0x48, world, sizeof(world));
        reinterpret_cast<float*>(params + 0x60)[3] = 1.0f;
        params[0x7D] = level > 0;
        const uintptr_t comp = VCall<uintptr_t>(actor, 0x9D8);
        if (!comp) return "找不到传送组件";
        VCall<void>(comp, 0x158, params);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "传送时出错";
    }
}

const char* TeleportToEntity(uint64_t entityId, double up) {
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return "角色还没有生成";
        const uintptr_t es = *g_tp.entitySystem;
        const uintptr_t target = es && entityId ? VCall<uintptr_t>(es, 0x120, entityId) : 0;
        if (!target) return "这里没有这个实体（可能还没加载进来）";
        const uintptr_t zone = VCall<uintptr_t>(target, 0x6B8);
        if (!zone) return "这个实体不在任何区域内";
        const uint64_t zoneId = ZoneId(zone);
        if (!zoneId || ZoneFromId(zoneId) != zone) return "区域 ID 不匹配";
        double local[3];
        Vec3Out(target, 0x2B8, local);
        const double r = sqrt(Dot(local, local));
        if (r > 100000.0) for (int i = 0; i < 3; ++i) local[i] += local[i] / r * up;
        else local[2] += up;
        if (!PositionLooksValid(local)) return "位置超出范围";
        double world[3];
        LocalToWorld(zone, local, world);
        alignas(16) uint8_t params[0x80] = {};
        *reinterpret_cast<uint64_t*>(params + 0x00) = zoneId;
        reinterpret_cast<double*>(params + 0x08)[3] = 1.0;
        memcpy(params + 0x28, local, 3 * sizeof(double));
        *reinterpret_cast<double*>(params + 0x40) = 1.0;
        memcpy(params + 0x48, world, sizeof(world));
        reinterpret_cast<float*>(params + 0x60)[3] = 1.0f;
        params[0x7D] = 1;
        const uintptr_t comp = VCall<uintptr_t>(actor, 0x9D8);
        if (!comp) return "找不到传送组件";
        VCall<void>(comp, 0x158, params);
        Log("[tp] to entity %llu in '%s' (%.0f, %.0f, %.0f)", static_cast<unsigned long long>(entityId), ZoneName(zone),
            local[0], local[1], local[2]);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "传送时出错";
    }
}

static bool SpotFilePath(char* path, DWORD n) {
    const DWORD len = GetEnvironmentVariableA("SC_OFFLINE_SPAWN_FILE", path, n);
    return len > 0 && len < n;
}

// One "spot x y z zone" line into s (when it parses, fits and, with checkPosition, is in range).
static void AddSpotLine(const char* line, Spot& s, bool checkPosition) {
    if (s.n >= kMaxZoneDepth) return;
    ZoneSpot& e = s.z[s.n];
    if (sscanf_s(line, "spot %lf %lf %lf %95[^\r\n]", &e.local[0], &e.local[1], &e.local[2],  // secret-scan: allow e.local is a struct member, not a hostname
                 e.name, static_cast<unsigned>(sizeof(e.name))) == 4 && (!checkPosition || PositionLooksValid(e.local)))
        ++s.n;
}

void SpotToText(const Spot& s, std::string& out) {
    out.clear();
    char line[256];
    for (int i = 0; i < s.n; ++i) {
        snprintf(line, sizeof(line), "spot %.6f %.6f %.6f %s\n", s.z[i].local[0], s.z[i].local[1], s.z[i].local[2], s.z[i].name);
        out += line;
    }
}

int SpotFromText(const char* text, Spot& s, bool checkPosition) {
    s = Spot{};
    char line[256];
    while (*text) {
        const size_t len = strcspn(text, "\n");
        const size_t n = len < sizeof(line) - 1 ? len : sizeof(line) - 1;
        memcpy(line, text, n);
        line[n] = 0;
        AddSpotLine(line, s, checkPosition);
        text += len + (text[len] ? 1 : 0);
    }
    return s.n;
}

static bool LoadSpot(Spot& s, bool& oldFormat) {
    s.n = 0;
    oldFormat = false;
    char path[MAX_PATH];
    if (!SpotFilePath(path, sizeof(path))) return false;
    FILE* f = _fsopen(path, "r", _SH_DENYNO);
    if (!f) return false;
    char line[256];
    while (s.n < kMaxZoneDepth && fgets(line, sizeof(line), f)) {
        if (strncmp(line, "zone=", 5) == 0) { oldFormat = true; continue; }
        AddSpotLine(line, s, true);
    }
    fclose(f);
    return s.n > 0;
}

static bool SaveSpot(const Spot& s) {
    char path[MAX_PATH];
    if (!SpotFilePath(path, sizeof(path))) return false;
    FILE* f = _fsopen(path, "w", _SH_DENYNO);
    if (!f) return false;
    std::string text;
    SpotToText(s, text);
    fprintf(f, "# starcitzenofflinemods spawn spot (F7). Position in each zone, innermost first.\n%s", text.c_str());
    fclose(f);
    return true;
}

// The spot in the teleport built-in's storage (data/storage/teleport.db): key "spot", spawn.txt's
// lines as text. "spawn.txt.time" is spawn.txt's last-write time when it was imported
// (builtins/builtin_store.h has the rule). spawn.txt is no longer written while storage works.
constexpr const char* kSpotKey = "spot";
constexpr const char* kSpotTimeKey = "spawn.txt.time";

// Both keys in one transaction; fileTime 0 leaves the recorded time as it is.
static bool StoreSpot(const Spot& s, int64_t fileTime) {
    BuiltinStore& st = g_teleportStore;
    std::string text;
    SpotToText(s, text);
    sco::sdk::StorageTransaction tx(st.S());
    sco_result r = tx.Result();
    if (r == SCO_OK) r = st.S().Put(kSpotKey, text);
    if (r == SCO_OK && fileTime) r = st.PutInt(kSpotTimeKey, fileTime);
    if (r == SCO_OK) r = tx.Commit();
    if (r != SCO_OK) st.Failed("saving the spot", r, "using spawn.txt");
    return r == SCO_OK;
}

static void DescribeChain(const Spot& s, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < s.n; ++i) {
        if (i) strncat_s(out, n, " > ", _TRUNCATE);
        strncat_s(out, n, s.z[i].name, _TRUNCATE);
    }
}

static Spot  g_spot;
static bool  g_autoTeleportPending = false;
static DWORD g_playerReadySince = 0;

static Spot  g_refineSpot;     // the spot being refined (F8's or a bookmark's)
static int   g_refineLevel = 0;
static DWORD g_refineSince = 0;

static void StartRefine(const Spot& spot, int level, DWORD now, const char* why) {
    if (level == 0) { Log("[tp] %s -> teleported to saved spot in '%s'", why, spot.z[0].name); g_refineLevel = 0; return; }
    Log("[tp] %s -> teleported via '%s' (waiting for '%s' to stream in)", why, spot.z[level].name, spot.z[0].name);
    g_refineSpot = spot;
    g_refineLevel = level;
    g_refineSince = now;
}

const char* GoToSpot(const Spot& s, DWORD now, const char* why) {
    int level = -1;
    if (const char* err = TeleportToSpot(s, level)) return err;
    StartRefine(s, level, now, why);
    return nullptr;
}

const char* CaptureCurrentSpot(Spot& s) {
    double world[3] = {};
    return CaptureSpot(s, world);
}

bool SaveSpotHere(const char* why, char* reply, size_t n) {
    Spot s;
    double world[3] = {};
    if (const char* err = CaptureSpot(s, world)) {
        Log("[tp] %s save failed: %s", why, err);
        snprintf(reply, n, "Couldn't save the spot: %s", err);
        return false;
    }
    const bool stored = g_teleportStore && StoreSpot(s, 0);
    if (!stored && !SaveSpot(s)) {
        Log("[tp] %s: could not write the spawn file (SC_OFFLINE_SPAWN_FILE)", why);
        snprintf(reply, n, "Couldn't write the spawn file (SC_OFFLINE_SPAWN_FILE)");
        return false;
    }
    g_spot = s;
    char chain[512];
    DescribeChain(s, chain, sizeof(chain));
    Log("[tp] %s saved spot: '%s' pos (%.2f, %.2f, %.2f) m; zones: %s; in %s", why, s.z[0].name,
        s.z[0].local[0], s.z[0].local[1], s.z[0].local[2], chain, stored ? "storage" : "spawn.txt");
    snprintf(reply, n, "Saved spot: '%s' pos (%.2f, %.2f, %.2f) m; zones: %s", s.z[0].name,
             s.z[0].local[0], s.z[0].local[1], s.z[0].local[2], chain);
    return true;
}

bool GoToSavedSpot(const char* why, char* reply, size_t n) {
    if (!g_spot.n) {
        Log("[tp] %s: no saved spot yet (stand somewhere and press F7)", why);
        snprintf(reply, n, "No saved spot yet (stand somewhere and press F7)");
        return false;
    }
    if (const char* err = GoToSpot(g_spot, GetTickCount(), why)) {
        Log("[tp] %s teleport failed: %s", why, err);
        SetMenuStatus("%s：%s。", why, err);
        snprintf(reply, n, "%s", err);
        return false;
    }
    if (g_refineLevel > 0)
        snprintf(reply, n, "Teleported via '%s' (waiting for '%s' to stream in)", g_spot.z[g_refineLevel].name, g_spot.z[0].name);
    else
        snprintf(reply, n, "Teleported to saved spot in '%s'", g_spot.z[0].name);
    return true;
}

static void SystemFromZoneName(const char* name, char* out, size_t n) {
    // OOC_Stanton_2b_Daymar -> Stanton
    if (!name || _strnicmp(name, "OOC_", 4) != 0) return;
    const char* sys = name + 4;
    const size_t len = strcspn(sys, "_");
    if (len && len < n) { memcpy(out, sys, len); out[len] = 0; }
}

void SpotSystemName(const Spot& s, char* out, size_t n) {
    out[0] = 0;
    for (int i = 0; i < s.n && !out[0]; ++i) SystemFromZoneName(s.z[i].name, out, n);
}

void CurrentSystemName(char* out, size_t n) {
    out[0] = 0;
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return;
        for (uintptr_t z = VCall<uintptr_t>(entity, 0x6B8); z && !out[0]; z = ZoneParent(z))
            SystemFromZoneName(ZoneName(z), out, n);
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

void CurrentSystemZoneName(char* out, size_t n) {
    out[0] = 0;
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return;
        if (const uintptr_t sys = SystemZoneOf(VCall<uintptr_t>(entity, 0x6B8)))
            if (const char* name = ZoneName(sys)) strncpy_s(out, n, name, _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

// The "SolarSystem_*" zone a zone belongs to (or 0 for the root / unknown).
uintptr_t SystemZoneOf(uintptr_t zone) {
    for (uintptr_t z = zone; z; z = ZoneParent(z)) {
        const char* name = ZoneName(z);
        if (name && _strnicmp(name, "SolarSystem", 11) == 0) return z;
    }
    return 0;
}

// ---- upright arrivals ----------------------------------------------------------------------------
// The teleport takes your rotation twice: in the zone's frame (4 doubles at +0x08) and in the world
// (4 floats at +0x60), each x, y, z, w. With the identity your up is the zone's z axis, which on a
// planet is only "up" over its north pole: anywhere else you arrived tilted (upside down in the far
// south) and tumbled while the game righted you.

static void QuatFromColumns(const double c[3][3], double q[4]) {   // c[i] = column i (the image of axis i)
    const double m00 = c[0][0], m11 = c[1][1], m22 = c[2][2];
    const double m01 = c[1][0], m02 = c[2][0], m10 = c[0][1], m12 = c[2][1], m20 = c[0][2], m21 = c[1][2];
    const double tr = m00 + m11 + m22;
    if (tr > 0) {
        const double s = 0.5 / sqrt(tr + 1.0);
        q[3] = 0.25 / s; q[0] = (m21 - m12) * s; q[1] = (m02 - m20) * s; q[2] = (m10 - m01) * s;
    } else if (m00 > m11 && m00 > m22) {
        const double s = 2.0 * sqrt(1.0 + m00 - m11 - m22);
        q[3] = (m21 - m12) / s; q[0] = 0.25 * s; q[1] = (m01 + m10) / s; q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        const double s = 2.0 * sqrt(1.0 + m11 - m00 - m22);
        q[3] = (m02 - m20) / s; q[0] = (m01 + m10) / s; q[1] = 0.25 * s; q[2] = (m12 + m21) / s;
    } else {
        const double s = 2.0 * sqrt(1.0 + m22 - m00 - m11);
        q[3] = (m10 - m01) / s; q[0] = (m02 + m20) / s; q[1] = (m12 + m21) / s; q[2] = 0.25 * s;
    }
    const double n = sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; ++i) q[i] /= n;
}

static void ColumnsFromQuat(const double q[4], double c[3][3]) {
    const double x = q[0], y = q[1], z = q[2], w = q[3];
    c[0][0] = 1 - 2 * (y * y + z * z); c[0][1] = 2 * (x * y + z * w);     c[0][2] = 2 * (x * z - y * w);
    c[1][0] = 2 * (x * y - z * w);     c[1][1] = 1 - 2 * (x * x + z * z); c[1][2] = 2 * (y * z + x * w);
    c[2][0] = 2 * (x * z + y * w);     c[2][1] = 2 * (y * z - x * w);     c[2][2] = 1 - 2 * (x * x + y * y);
}

// The same rotation in the world: the zone's axes (their world directions) applied to it.
static bool WorldRotation(uintptr_t zone, const double qLocal[4], double qWorld[4]) {
    double origin[3], axis[3][3], r[3][3], w[3][3];
    if (!ZoneAxes(zone, origin, axis)) return false;
    ColumnsFromQuat(qLocal, r);
    for (int c = 0; c < 3; ++c)
        for (int k = 0; k < 3; ++k) w[c][k] = axis[0][k] * r[c][0] + axis[1][k] * r[c][1] + axis[2][k] * r[c][2];
    QuatFromColumns(w, qWorld);
    return true;
}

// qLocal: your rotation in the zone's frame (x, y, z, w); null for the zone's own axes, as every
// teleport used to arrive.
static const char* TeleportIntoZoneRotated(uintptr_t zone, const double local[3], const double* qLocal) {
    if (!zone) return "没有区域";
    if (!PositionLooksValid(local)) return "位置超出范围";
    __try {
        uintptr_t actor, entity;
        if (!GetLocalPlayer(actor, entity)) return "角色还没有生成";
        const uintptr_t mine = SystemZoneOf(VCall<uintptr_t>(entity, 0x6B8));
        const uintptr_t theirs = SystemZoneOf(zone);
        // With PU_All every star system is loaded, and teleporting into a zone of another system
        // works (seen on 4.10.1: Stanton -> Pyro I L1 -> Pyro IV). Only refuse when the target's
        // system is unknown.
        if (!theirs) return "找不到目标所在的星系";
        if (mine && mine != theirs)
            Log("[tp] crossing star systems: %s -> %s", ZoneName(mine) ? ZoneName(mine) : "?", ZoneName(theirs) ? ZoneName(theirs) : "?");
        const uint64_t zoneId = ZoneId(zone);
        if (!zoneId || ZoneFromId(zoneId) != zone) return "区域 ID 不匹配";
        double world[3];
        LocalToWorld(zone, local, world);
        alignas(16) uint8_t params[0x80] = {};
        *reinterpret_cast<uint64_t*>(params + 0x00) = zoneId;
        reinterpret_cast<double*>(params + 0x08)[3] = 1.0;
        memcpy(params + 0x28, local, 3 * sizeof(double));
        *reinterpret_cast<double*>(params + 0x40) = 1.0;
        memcpy(params + 0x48, world, sizeof(world));
        reinterpret_cast<float*>(params + 0x60)[3] = 1.0f;
        double qWorld[4];
        if (qLocal && WorldRotation(zone, qLocal, qWorld)) {
            memcpy(params + 0x08, qLocal, 4 * sizeof(double));
            for (int i = 0; i < 4; ++i) reinterpret_cast<float*>(params + 0x60)[i] = static_cast<float>(qWorld[i]);
        }
        params[0x7D] = 1;
        const uintptr_t comp = VCall<uintptr_t>(actor, 0x9D8);
        if (!comp) return "找不到传送组件";
        VCall<void>(comp, 0x158, params);
        Log("[tp] into zone '%s' at (%.0f, %.0f, %.0f)", ZoneName(zone), local[0], local[1], local[2]);
        return nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return "传送时出错";
    }
}


const char* TeleportIntoZone(uintptr_t zone, const double local[3]) { return TeleportIntoZoneRotated(zone, local, nullptr); }

const char* TeleportIntoZoneUpright(uintptr_t zone, const double local[3]) {
    const double r = sqrt(Dot(local, local));
    if (r < 1.0) return TeleportIntoZone(zone, local);
    const double up[3] = { local[0] / r, local[1] / r, local[2] / r };
    // Forward (y) towards the north pole along the surface; over a pole, the zone's y axis.
    double fwd[3] = { -up[2] * up[0], -up[2] * up[1], 1.0 - up[2] * up[2] };
    double len = sqrt(Dot(fwd, fwd));
    if (len < 1e-6) {
        fwd[0] = -up[1] * up[0]; fwd[1] = 1.0 - up[1] * up[1]; fwd[2] = -up[1] * up[2];
        len = sqrt(Dot(fwd, fwd));
    }
    for (double& v : fwd) v /= len;
    const double right[3] = { fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0] };
    const double cols[3][3] = { { right[0], right[1], right[2] }, { fwd[0], fwd[1], fwd[2] }, { up[0], up[1], up[2] } };
    double q[4];
    QuatFromColumns(cols, q);
    return TeleportIntoZoneRotated(zone, local, q);
}

static bool g_spotLoadPending = false;   // LoadSavedSpot ran; the first TeleportTick loads the spot
static bool g_startingOverDaymar = false;

// Called before the host kit starts, so the built-in's storage isn't open yet: the spot is loaded
// on the first TeleportTick, after the teleport built-in has loaded (or failed to).
void LoadSavedSpot(bool startingOverDaymar) {
    g_startingOverDaymar = startingOverDaymar;
    g_spotLoadPending = true;
}

// spawn.txt, then storage: a spawn.txt storage hasn't seen is imported; otherwise storage's spot
// is the saved one. Without storage, spawn.txt as before.
static void LoadSpotNow() {
    bool oldFormat = false;
    Spot file;
    const bool haveFile = LoadSpot(file, oldFormat);
    g_spot = file;
    const char* from = "spawn.txt";
    BuiltinStore& st = g_teleportStore;
    if (st) {
        char path[MAX_PATH];
        const int64_t fileTime = SpotFilePath(path, sizeof(path)) ? FileWriteTime(path) : 0;
        int64_t seen = 0;
        const sco_result rs = st.GetInt(kSpotTimeKey, seen);
        if (haveFile && (rs != SCO_OK || seen != fileTime)) {
            if (StoreSpot(file, fileTime))
                Log("[storage] imported spawn.txt into teleport%s", rs == SCO_OK ? " (spawn.txt changed since the last import)" : "");
        } else {
            std::string text;
            const sco_result r = st.S().Get(kSpotKey, text);
            Spot stored;
            if (r == SCO_OK && SpotFromText(text.c_str(), stored, true)) { g_spot = stored; from = "storage"; }
            else if (r != SCO_OK && r != SCO_NOT_FOUND) st.Failed("reading the spot", r, "using spawn.txt");
        }
    }
    const bool startingOverDaymar = g_startingOverDaymar;
    if (g_spot.n) {
        g_autoTeleportPending = true;
        char chain[512];
        DescribeChain(g_spot, chain, sizeof(chain));
        Log("[tp] saved spot loaded from %s ('%s'; zones: %s); will teleport there after spawning. F7 = save new spot, F8 = go there now",
            from, g_spot.z[0].name, chain);
    } else if (oldFormat) {
        Log("[tp] saved spot is from an older version and can't be used after a relaunch: stand there and press F7 once more");
    } else {
        Log("[tp] no saved spot yet: stand where you want to spawn and press F7 (F8 = go there now)");
    }
    if (startingOverDaymar && g_autoTeleportPending) {
        g_autoTeleportPending = false;
        Log("[tp] starting over Daymar instead of the saved spot (F8 still goes there)");
    }
}

void TeleportTick(DWORD now) {
    if (g_spotLoadPending) {
        g_spotLoadPending = false;
        LoadSpotNow();
    }
    // F7 and F8 are the teleport built-in's hotkeys (sco.ui), dispatched by hotkeys.cpp.
    if (g_autoTeleportPending) {
        uintptr_t actor, entity;
        bool ready = false;
        __try { ready = GetLocalPlayer(actor, entity); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (!ready) { g_playerReadySince = 0; return; }
        if (!g_playerReadySince) g_playerReadySince = now;
        if (now - g_playerReadySince < 8000) return;
        static DWORD lastTry = 0;
        if (now - lastTry < 2000) return;
        lastTry = now;
        const char* err = GoToSpot(g_spot, now, "spawned");
        if (!err) g_autoTeleportPending = false;
        else if (err == kOtherSystem || now - g_playerReadySince > 120000) { g_autoTeleportPending = false; Log("[tp] auto-teleport gave up: %s", err); }
    }

    if (g_refineLevel > 0) {
        static DWORD lastCheck = 0;
        if (now - lastCheck < 1000) return;
        lastCheck = now;
        const int level = SavedZoneLevel(g_refineSpot);
        if (level >= 0 && level < g_refineLevel) {
            int got = -1;
            if (TeleportToSpot(g_refineSpot, got)) { g_refineLevel = 0; return; }
            g_refineLevel = got;
            if (got == 0) Log("[tp] '%s' streamed in -> exact spot", g_refineSpot.z[0].name);
        } else if (now - g_refineSince > 60000) {
            Log("[tp] '%s' did not stream in within 60 s; stayed in '%s' (F8 = retry)", g_refineSpot.z[0].name,
                g_refineSpot.z[g_refineLevel].name);
            g_refineLevel = 0;
        }
    }
}
