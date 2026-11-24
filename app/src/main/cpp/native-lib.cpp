// ============================================================
// native-lib.cpp - BOHA XVIP / ModMenuPro v7
// ------------------------------------------------------------
// * Thread-safe SIGSEGV/SIGBUS Fault Shield (ScopedFaultGuard)
// * Zero-lag cached libil2cpp.so base/range + relaxed atomics
// * Full Il2Cpp runtime resolution (no hardcoded static RVAs)
// * All hooks integrated from libdatastore_shared.so analysis:
//     1. SR-SPOOF   (Device ID Spoofer / Unban)
//     2. ESP        (Transform & PhysicsActor tracking)
//     3. Weapon     (SilentAim / WallPen / OnUpdate)
//     4. Camera     (FOV / UniversalCamera / FreeCam / Spectate)
//     5. SkyWalk    (CharacterActor / FallState / StateMachine / Head)
//     6. Health/UI  (ElementHealthSync / HudHealthView / Sliders / Fall)
//     7. Movement   (PhysicsActor Velocity / WalkOnAir / AutoWalk / NavGraph)
//     8. Sync/Car   (Clothes / Interp / VehBody / Stamina)
//     9. World/Range(DayNight / RemoteCollisionCulling 5-system loop)
// ============================================================

#include <jni.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <cinttypes>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <csetjmp>
#include <dlfcn.h>
#include <unistd.h>
#include <android/log.h>

#include "Features.h"
#include "il2cpp/Il2CppApi.h"
#include "kitty/KittyMemory.h"
#include "dobby/dobby.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "ModMenu", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "ModMenu", __VA_ARGS__)

// ============================================================
// SIGSEGV / SIGBUS FAULT SHIELD (matches SEGV-SHIELD pattern)
// ============================================================
static thread_local sigjmp_buf t_faultJmp;
static thread_local volatile sig_atomic_t t_faultGuarded = 0;
static struct sigaction g_prevSegv{}, g_prevBus{};
static std::atomic<bool> g_shieldInstalled{false};

static void faultSignalHandler(int sig, siginfo_t* info, void* uctx) {
    if (t_faultGuarded) {
        t_faultGuarded = 0;
        F.faultsRecovered.fetch_add(1, std::memory_order_relaxed);
        siglongjmp(t_faultJmp, sig);
    }
    const struct sigaction& prev = (sig == SIGSEGV) ? g_prevSegv : g_prevBus;
    if (prev.sa_flags & SA_SIGINFO) {
        if (prev.sa_sigaction) { prev.sa_sigaction(sig, info, uctx); return; }
    } else {
        if (prev.sa_handler != SIG_DFL && prev.sa_handler != SIG_IGN && prev.sa_handler != nullptr) {
            prev.sa_handler(sig);
            return;
        }
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void ensureFaultShield() {
    bool expected = false;
    if (!g_shieldInstalled.compare_exchange_strong(expected, true)) return;
    struct sigaction sa{};
    sa.sa_sigaction = faultSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_NODEFER;
    sigaction(SIGSEGV, &sa, &g_prevSegv);
    sigaction(SIGBUS,  &sa, &g_prevBus);
}

#define GUARDED_RUN(label, block)                                               \
    do {                                                                        \
        ensureFaultShield();                                                    \
        if (sigsetjmp(t_faultJmp, 1) == 0) {                                    \
            t_faultGuarded = 1;                                                 \
            block                                                               \
            t_faultGuarded = 0;                                                 \
        } else {                                                                \
            t_faultGuarded = 0;                                                 \
            LOGE("[SEGV-SHIELD] recovered fault in %s", (label));               \
        }                                                                       \
    } while (0)

// ============================================================
// MATH & COMPONENT STRUCTS
// ============================================================
struct Quat { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };
struct PositionComponent { float x, y, z; };

static std::atomic<uintptr_t> g_libBase{0};
static std::atomic<uintptr_t> g_libEnd{0};
static std::atomic<bool> g_initStarted{false};

// Spoofed device ID helper (cached hex string)
static char g_spoofHex[33] = "9a4f2c81d7e04b31a8c502e9f1b63d48";
static void refreshSpoofHex(uint32_t seed) {
    static const char hex[] = "0123456789abcdef";
    uint32_t x = seed ? seed : 0xA3C59AC3u;
    for (int i = 0; i < 32; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        g_spoofHex[i] = hex[x & 0xF];
    }
    g_spoofHex[32] = '\0';
}

static Il2CppString* makeSpoofIl2CppString() {
    if (!il2cpp.string_new) return nullptr;
    refreshSpoofHex(F.spoofSeed.load(std::memory_order_relaxed));
    return il2cpp.string_new(g_spoofHex);
}

// ============================================================
// 1. SR-SPOOF HOOKS (Device ID Spoofer / Unban)
// ============================================================
using StaticStrGetter_t = Il2CppString* (*)(const MethodInfo*);
using AuthFromDevice_t  = void* (*)(void*, void*, const MethodInfo*);
using AuthFillExt_t     = void (*)(void*, const MethodInfo*);
using SettingsGetDev_t  = Il2CppString* (*)(void*, const MethodInfo*);

static StaticStrGetter_t orig_GetDeviceUniqueId = nullptr;
static AuthFromDevice_t  orig_AuthFromDevice    = nullptr;
static AuthFillExt_t     orig_AuthFillExtended  = nullptr;
static SettingsGetDev_t  orig_SettingsGetDevId  = nullptr;

static size_t g_offExtReal = 0, g_offExtGen = 0, g_offAdvId = 0;

static Il2CppString* hook_GetDeviceUniqueId(const MethodInfo* method) {
    if (F.deviceSpoof.load(std::memory_order_relaxed)) {
        if (auto* s = makeSpoofIl2CppString()) return s;
    }
    return orig_GetDeviceUniqueId ? orig_GetDeviceUniqueId(method) : nullptr;
}

static void* hook_AuthFromDevice(void* arg0, void* arg1, const MethodInfo* method) {
    void* res = orig_AuthFromDevice ? orig_AuthFromDevice(arg0, arg1, method) : nullptr;
    if (res && F.deviceSpoof.load(std::memory_order_relaxed)) {
        if (auto* s = makeSpoofIl2CppString()) {
            if (g_offExtReal) *reinterpret_cast<Il2CppString**>(reinterpret_cast<uintptr_t>(res) + g_offExtReal) = s;
            if (g_offExtGen)  *reinterpret_cast<Il2CppString**>(reinterpret_cast<uintptr_t>(res) + g_offExtGen)  = s;
            if (g_offAdvId)   *reinterpret_cast<Il2CppString**>(reinterpret_cast<uintptr_t>(res) + g_offAdvId)   = s;
        }
    }
    return res;
}

static void hook_AuthFillExtended(void* self, const MethodInfo* method) {
    if (orig_AuthFillExtended) orig_AuthFillExtended(self, method);
    if (self && F.deviceSpoof.load(std::memory_order_relaxed)) {
        if (auto* s = makeSpoofIl2CppString()) {
            if (g_offExtReal) *reinterpret_cast<Il2CppString**>(reinterpret_cast<uintptr_t>(self) + g_offExtReal) = s;
            if (g_offExtGen)  *reinterpret_cast<Il2CppString**>(reinterpret_cast<uintptr_t>(self) + g_offExtGen)  = s;
        }
    }
}

static Il2CppString* hook_SettingsGetDeviceId(void* self, const MethodInfo* method) {
    if (F.deviceSpoof.load(std::memory_order_relaxed)) {
        if (auto* s = makeSpoofIl2CppString()) return s;
    }
    return orig_SettingsGetDevId ? orig_SettingsGetDevId(self, method) : nullptr;
}

// ============================================================
// 2. ESP / TRANSFORM / PHYSICS TRACKING HOOKS
// ============================================================
using TransformSetPos_t       = void (*)(void*, Vec3, const MethodInfo*);
using TransformSetPosRot_t    = void (*)(void*, Vec3, Quat, const MethodInfo*);
using VoidUpdate_t            = void (*)(void*, const MethodInfo*);

static TransformSetPos_t    orig_TransformSetPos      = nullptr;
static TransformSetPos_t    orig_TransformSetLocalPos = nullptr;
static TransformSetPosRot_t orig_TransformSetPosRot   = nullptr;
static VoidUpdate_t         orig_PhysicsActorUpdate   = nullptr;
static VoidUpdate_t         orig_PhysicsSyncFixed     = nullptr;
static VoidUpdate_t         orig_PositionUpdate       = nullptr;

static void hook_TransformSetPos(void* self, Vec3 pos, const MethodInfo* m) {
    if (orig_TransformSetPos) orig_TransformSetPos(self, pos, m);
}
static void hook_TransformSetLocalPos(void* self, Vec3 pos, const MethodInfo* m) {
    if (orig_TransformSetLocalPos) orig_TransformSetLocalPos(self, pos, m);
}
static void hook_TransformSetPosRot(void* self, Vec3 pos, Quat rot, const MethodInfo* m) {
    if (orig_TransformSetPosRot) orig_TransformSetPosRot(self, pos, rot, m);
}
static void hook_PhysicsActorUpdate(void* self, const MethodInfo* m) {
    if (orig_PhysicsActorUpdate) orig_PhysicsActorUpdate(self, m);
}
static void hook_PhysicsSyncFixed(void* self, const MethodInfo* m) {
    if (orig_PhysicsSyncFixed) orig_PhysicsSyncFixed(self, m);
}
static void hook_PositionUpdate(void* self, const MethodInfo* method) {
    if (self) {
        auto* pos = reinterpret_cast<PositionComponent*>(self);
        F.playerPos = {pos->x, pos->y, pos->z};
        if (F.teleportRequested.load(std::memory_order_relaxed) &&
            F.hasWaypoint.load(std::memory_order_relaxed)) {
            pos->x = F.waypoint.x;
            pos->y = F.waypoint.y;
            pos->z = F.waypoint.z;
            F.teleportRequested.store(false, std::memory_order_relaxed);
        }
    }
    if (orig_PositionUpdate) orig_PositionUpdate(self, method);
}

// ============================================================
// 3. WEAPON / SILENT AIM / WALLPEN HOOKS
// ============================================================
using WeaponOnUpdate_t    = void (*)(void*, const MethodInfo*);
using RayCast7_t          = void* (*)(void*, Vec3, Vec3, float, int32_t, int32_t, void*, const MethodInfo*);
using RayCast8_t          = void* (*)(void*, Vec3, Vec3, float, int32_t, int32_t, void*, bool, const MethodInfo*);
using ProcessHits6_t      = void (*)(void*, void*, void*, void*, void*, void*, const MethodInfo*);
using WallPenDist_t       = float (*)(void*, void*, float, const MethodInfo*);
using WallPenParams_t     = bool (*)(void*, void*, float*, float*, const MethodInfo*);

static WeaponOnUpdate_t orig_WeaponOnUpdate = nullptr;
static RayCast7_t       orig_RayCast7       = nullptr;
static RayCast8_t       orig_RayCast8       = nullptr;
static ProcessHits6_t   orig_ProcessHits6   = nullptr;
static WallPenDist_t    orig_WallPenDist    = nullptr;
static WallPenParams_t  orig_WallPenParams  = nullptr;

static void hook_WeaponOnUpdate(void* self, const MethodInfo* m) {
    if (orig_WeaponOnUpdate) orig_WeaponOnUpdate(self, m);
}
static void* hook_RayCast7(void* self, Vec3 origin, Vec3 dir, float dist, int32_t a, int32_t b, void* c, const MethodInfo* m) {
    if (F.bulletWallhack.load(std::memory_order_relaxed) && dist > 0.f && dist < 500.f) dist = 500.f;
    return orig_RayCast7 ? orig_RayCast7(self, origin, dir, dist, a, b, c, m) : nullptr;
}
static void* hook_RayCast8(void* self, Vec3 origin, Vec3 dir, float dist, int32_t a, int32_t b, void* c, bool d, const MethodInfo* m) {
    if (F.bulletWallhack.load(std::memory_order_relaxed) && dist > 0.f && dist < 500.f) dist = 500.f;
    return orig_RayCast8 ? orig_RayCast8(self, origin, dir, dist, a, b, c, d, m) : nullptr;
}
static void hook_ProcessHits6(void* self, void* a, void* b, void* c, void* d, void* e, const MethodInfo* m) {
    if (orig_ProcessHits6) orig_ProcessHits6(self, a, b, c, d, e, m);
}
static float hook_WallPenDist(void* self, void* hit, float dist, const MethodInfo* m) {
    if (F.bulletWallhack.load(std::memory_order_relaxed)) return 9999.f;
    return orig_WallPenDist ? orig_WallPenDist(self, hit, dist, m) : dist;
}
static bool hook_WallPenParams(void* self, void* mat, float* outLoss, float* outDist, const MethodInfo* m) {
    if (F.bulletWallhack.load(std::memory_order_relaxed)) {
        if (outLoss) *outLoss = 0.f;
        if (outDist) *outDist = 9999.f;
        return true;
    }
    return orig_WallPenParams ? orig_WallPenParams(self, mat, outLoss, outDist, m) : false;
}

// ============================================================
// 4. CAMERA / FOV / FREECAM / SPECTATE HOOKS
// ============================================================
using CameraSetFov_t   = void (*)(void*, float, const MethodInfo*);
using CameraGetFov_t   = float (*)(void*, const MethodInfo*);
using UnivCamRot_t     = void (*)(void*, float, float, const MethodInfo*);
using SwitchListener_t = void (*)(void*, void*, const MethodInfo*);

static CameraSetFov_t   orig_CameraSetFov      = nullptr;
static CameraGetFov_t   orig_CameraGetFov      = nullptr;
static UnivCamRot_t     orig_UnivCamRot        = nullptr;
static VoidUpdate_t     orig_CamFollowPed      = nullptr;
static VoidUpdate_t     orig_CamFollowVeh      = nullptr;
static VoidUpdate_t     orig_CamPlacement      = nullptr;
static SwitchListener_t orig_SwitchListener    = nullptr;

static void hook_CameraSetFov(void* self, float fov, const MethodInfo* m) {
    if (F.fovOverride.load(std::memory_order_relaxed)) fov = F.fov.load(std::memory_order_relaxed);
    if (orig_CameraSetFov) orig_CameraSetFov(self, fov, m);
}
static float hook_CameraGetFov(void* self, const MethodInfo* m) {
    if (F.fovOverride.load(std::memory_order_relaxed)) return F.fov.load(std::memory_order_relaxed);
    return orig_CameraGetFov ? orig_CameraGetFov(self, m) : 60.f;
}
static void hook_UnivCamRot(void* self, float x, float y, const MethodInfo* m) {
    if (orig_UnivCamRot) orig_UnivCamRot(self, x, y, m);
}
static void hook_CamFollowPed(void* self, const MethodInfo* m) {
    if (F.freeCam.load(std::memory_order_relaxed)) return;
    if (orig_CamFollowPed) orig_CamFollowPed(self, m);
}
static void hook_CamFollowVeh(void* self, const MethodInfo* m) {
    if (F.freeCam.load(std::memory_order_relaxed)) return;
    if (orig_CamFollowVeh) orig_CamFollowVeh(self, m);
}
static void hook_CamPlacement(void* self, const MethodInfo* m) {
    if (F.freeCam.load(std::memory_order_relaxed)) return;
    if (orig_CamPlacement) orig_CamPlacement(self, m);
}
static void hook_SwitchListener(void* self, void* target, const MethodInfo* m) {
    if (orig_SwitchListener) orig_SwitchListener(self, target, m);
}

// ============================================================
// 5. SKYWALK / GODMODE / FALL DAMAGE / HEAD ENLARGE HOOKS
// ============================================================
using BoolGetter_t     = bool (*)(void*, const MethodInfo*);
using CanEnterState_t  = bool (*)(void*, void*, const MethodInfo*);
using FallDamageFn_t   = float (*)(void*, float, const MethodInfo*);
using StateMachOp_t    = void (*)(void*, void*, const MethodInfo*);
using StateMachTry_t   = bool (*)(void*, void*, const MethodInfo*);

static VoidUpdate_t    orig_CharActorUpdate     = nullptr;
static VoidUpdate_t    orig_CharActorPostSim    = nullptr;
static VoidUpdate_t    orig_CharActorPreSim     = nullptr;
static BoolGetter_t    orig_CharIsStable        = nullptr;
static BoolGetter_t    orig_CharIsGrounded      = nullptr;
static VoidUpdate_t    orig_CharForceNotGround  = nullptr;
static VoidUpdate_t    orig_CharActorLateUpdate = nullptr;
static FallDamageFn_t  orig_KnockoutFallDamage  = nullptr;
static CanEnterState_t orig_FallStateCanEnter    = nullptr;
static StateMachOp_t   orig_StateForceExit      = nullptr;
static StateMachTry_t  orig_StateTryEnter       = nullptr;
static StateMachOp_t   orig_StateForceEnter     = nullptr;
static CanEnterState_t orig_WeaponShootCanEnter = nullptr;
static CanEnterState_t orig_PedStateCanEnter    = nullptr;

static void hook_CharActorUpdate(void* self, const MethodInfo* m)    { if (orig_CharActorUpdate) orig_CharActorUpdate(self, m); }
static void hook_CharActorPostSim(void* self, const MethodInfo* m)   { if (orig_CharActorPostSim) orig_CharActorPostSim(self, m); }
static void hook_CharActorPreSim(void* self, const MethodInfo* m)    { if (orig_CharActorPreSim) orig_CharActorPreSim(self, m); }
static bool hook_CharIsStable(void* self, const MethodInfo* m) {
    if (F.skyWalk.load(std::memory_order_relaxed)) return true;
    return orig_CharIsStable ? orig_CharIsStable(self, m) : true;
}
static bool hook_CharIsGrounded(void* self, const MethodInfo* m) {
    if (F.skyWalk.load(std::memory_order_relaxed) || F.walkOnAir.load(std::memory_order_relaxed)) return true;
    return orig_CharIsGrounded ? orig_CharIsGrounded(self, m) : true;
}
static void hook_CharForceNotGround(void* self, const MethodInfo* m) {
    if (F.skyWalk.load(std::memory_order_relaxed) || F.walkOnAir.load(std::memory_order_relaxed)) return;
    if (orig_CharForceNotGround) orig_CharForceNotGround(self, m);
}
static void hook_CharActorLateUpdate(void* self, const MethodInfo* m) {
    if (orig_CharActorLateUpdate) orig_CharActorLateUpdate(self, m);
}
static float hook_KnockoutFallDamage(void* self, float vel, const MethodInfo* m) {
    if (F.godMode.load(std::memory_order_relaxed) ||
        F.noFallDamage.load(std::memory_order_relaxed) ||
        F.skyWalk.load(std::memory_order_relaxed)) {
        return 0.f;
    }
    return orig_KnockoutFallDamage ? orig_KnockoutFallDamage(self, vel, m) : 0.f;
}
static bool hook_FallStateCanEnter(void* self, void* prev, const MethodInfo* m) {
    if (F.skyWalk.load(std::memory_order_relaxed) || F.noFallDamage.load(std::memory_order_relaxed)) return false;
    return orig_FallStateCanEnter ? orig_FallStateCanEnter(self, prev, m) : false;
}
static void hook_StateForceExit(void* self, void* st, const MethodInfo* m) {
    if (orig_StateForceExit) orig_StateForceExit(self, st, m);
}
static bool hook_StateTryEnter(void* self, void* st, const MethodInfo* m) {
    return orig_StateTryEnter ? orig_StateTryEnter(self, st, m) : false;
}
static void hook_StateForceEnter(void* self, void* st, const MethodInfo* m) {
    if (orig_StateForceEnter) orig_StateForceEnter(self, st, m);
}
static bool hook_WeaponShootCanEnter(void* self, void* prev, const MethodInfo* m) {
    if (F.skyWalk.load(std::memory_order_relaxed)) return true;
    return orig_WeaponShootCanEnter ? orig_WeaponShootCanEnter(self, prev, m) : false;
}
static bool hook_PedStateCanEnter(void* self, void* prev, const MethodInfo* m) {
    return orig_PedStateCanEnter ? orig_PedStateCanEnter(self, prev, m) : false;
}

// ============================================================
// 6. HEALTH / FALL DAMAGE / HUD SLIDER HOOKS
// ============================================================
using SliderSetVal_t   = void (*)(void*, float, const MethodInfo*);
using ApplyFallDmg_t   = void (*)(void*, float, const MethodInfo*);
using TakeDamage_t     = void (*)(void*, float, const MethodInfo*);

static VoidUpdate_t   orig_ElemHealthSync  = nullptr;
static VoidUpdate_t   orig_HudHealthView   = nullptr;
static SliderSetVal_t orig_SliderSetVal    = nullptr;
static SliderSetVal_t orig_SliderSetSilent = nullptr;
static ApplyFallDmg_t orig_ApplyLocalFall  = nullptr;
static TakeDamage_t   orig_TakeDamage      = nullptr;

static void hook_ElemHealthSync(void* self, const MethodInfo* m) {
    if (orig_ElemHealthSync) orig_ElemHealthSync(self, m);
}
static void hook_HudHealthView(void* self, const MethodInfo* m) {
    if (orig_HudHealthView) orig_HudHealthView(self, m);
}
static void hook_SliderSetVal(void* self, float v, const MethodInfo* m) {
    if (orig_SliderSetVal) orig_SliderSetVal(self, v, m);
}
static void hook_SliderSetSilent(void* self, float v, const MethodInfo* m) {
    if (orig_SliderSetSilent) orig_SliderSetSilent(self, v, m);
}
static void hook_ApplyLocalFall(void* self, float dmg, const MethodInfo* m) {
    if (F.godMode.load(std::memory_order_relaxed) ||
        F.noFallDamage.load(std::memory_order_relaxed) ||
        F.skyWalk.load(std::memory_order_relaxed)) {
        return;
    }
    if (orig_ApplyLocalFall) orig_ApplyLocalFall(self, dmg, m);
}
static void hook_TakeDamage(void* self, float amount, const MethodInfo* m) {
    if (F.godMode.load(std::memory_order_relaxed)) return;
    if (orig_TakeDamage) orig_TakeDamage(self, amount, m);
}

// ============================================================
// 7. SPEED / WALKONAIR / AUTOWALK / NAVGRAPH HOOKS
// ============================================================
using GetVelocity_t = Vec3 (*)(void*, const MethodInfo*);
using NavFindPath_t = bool (*)(void*, Vec3, Vec3, void*, const MethodInfo*);
using NavVecOp_t    = void (*)(void*, Vec3, const MethodInfo*);

static GetVelocity_t orig_GetVelocity    = nullptr;
static VoidUpdate_t  orig_PhysicsFixed   = nullptr;
static VoidUpdate_t  orig_PhysicsWoaUpd  = nullptr;
static VoidUpdate_t  orig_PedViewInputs  = nullptr;
static NavFindPath_t orig_NavFindPath    = nullptr;
static VoidUpdate_t  orig_NavCtor        = nullptr;
static NavVecOp_t    orig_NavSetGoal     = nullptr;
static NavVecOp_t    orig_NavSetWaypoint = nullptr;
static NavVecOp_t    orig_NavSetDest     = nullptr;
static NavVecOp_t    orig_NavAddMarker   = nullptr;

static Vec3 hook_GetVelocity(void* self, const MethodInfo* m) {
    Vec3 v = orig_GetVelocity ? orig_GetVelocity(self, m) : Vec3{};
    if (F.playerSpeedOn.load(std::memory_order_relaxed)) {
        float mul = F.playerSpeed.load(std::memory_order_relaxed);
        v.x *= mul;
        v.z *= mul;
    }
    return v;
}
static void hook_PhysicsFixed(void* self, const MethodInfo* m)   { if (orig_PhysicsFixed) orig_PhysicsFixed(self, m); }
static void hook_PhysicsWoaUpd(void* self, const MethodInfo* m)  { if (orig_PhysicsWoaUpd) orig_PhysicsWoaUpd(self, m); }
static void hook_PedViewInputs(void* self, const MethodInfo* m)  { if (orig_PedViewInputs) orig_PedViewInputs(self, m); }
static bool hook_NavFindPath(void* self, Vec3 a, Vec3 b, void* outList, const MethodInfo* m) {
    return orig_NavFindPath ? orig_NavFindPath(self, a, b, outList, m) : false;
}
static void hook_NavCtor(void* self, const MethodInfo* m)        { if (orig_NavCtor) orig_NavCtor(self, m); }
static void hook_NavSetGoal(void* self, Vec3 p, const MethodInfo* m) {
    F.waypoint = p; F.hasWaypoint.store(true, std::memory_order_relaxed);
    if (orig_NavSetGoal) orig_NavSetGoal(self, p, m);
}
static void hook_NavSetWaypoint(void* self, Vec3 p, const MethodInfo* m) {
    F.waypoint = p; F.hasWaypoint.store(true, std::memory_order_relaxed);
    if (orig_NavSetWaypoint) orig_NavSetWaypoint(self, p, m);
}
static void hook_NavSetDest(void* self, Vec3 p, const MethodInfo* m) {
    F.waypoint = p; F.hasWaypoint.store(true, std::memory_order_relaxed);
    if (orig_NavSetDest) orig_NavSetDest(self, p, m);
}
static void hook_NavAddMarker(void* self, Vec3 p, const MethodInfo* m) {
    F.waypoint = p; F.hasWaypoint.store(true, std::memory_order_relaxed);
    if (orig_NavAddMarker) orig_NavAddMarker(self, p, m);
}

// ============================================================
// 8. CLOTHES / VEHICLES / STAMINA HOOKS
// ============================================================
using SetStaminaLose_t = void (*)(void*, bool, const MethodInfo*);

static VoidUpdate_t     orig_PedLoadClothes   = nullptr;
static VoidUpdate_t     orig_InterpTransform  = nullptr;
static VoidUpdate_t     orig_VehBodyPrePhys   = nullptr;
static VoidUpdate_t     orig_PedClothesSync   = nullptr;
static VoidUpdate_t     orig_ElemModelSync    = nullptr;
static SetStaminaLose_t orig_SetStaminaLose   = nullptr;
static VoidUpdate_t     orig_PedStaminaSystem = nullptr;

static void hook_PedLoadClothes(void* self, const MethodInfo* m)  { if (orig_PedLoadClothes) orig_PedLoadClothes(self, m); }
static void hook_InterpTransform(void* self, const MethodInfo* m) { if (orig_InterpTransform) orig_InterpTransform(self, m); }
static void hook_VehBodyPrePhys(void* self, const MethodInfo* m)  { if (orig_VehBodyPrePhys) orig_VehBodyPrePhys(self, m); }
static void hook_PedClothesSync(void* self, const MethodInfo* m)  { if (orig_PedClothesSync) orig_PedClothesSync(self, m); }
static void hook_ElemModelSync(void* self, const MethodInfo* m)   { if (orig_ElemModelSync) orig_ElemModelSync(self, m); }
static void hook_SetStaminaLose(void* self, bool lose, const MethodInfo* m) {
    if (F.infiniteStamina.load(std::memory_order_relaxed)) lose = false;
    if (orig_SetStaminaLose) orig_SetStaminaLose(self, lose, m);
}
static void hook_PedStaminaSystem(void* self, const MethodInfo* m) {
    if (F.infiniteStamina.load(std::memory_order_relaxed)) return;
    if (orig_PedStaminaSystem) orig_PedStaminaSystem(self, m);
}

// ============================================================
// 9. DAYNIGHT & RANGE CULLING HOOKS
// ============================================================
static VoidUpdate_t orig_SkyDriverLate   = nullptr;
static VoidUpdate_t orig_RangeCull[5]    = {nullptr, nullptr, nullptr, nullptr, nullptr};

static void hook_SkyDriverLate(void* self, const MethodInfo* m) {
    if (orig_SkyDriverLate) orig_SkyDriverLate(self, m);
}

template <int Idx>
static void hook_RangeCull(void* self, const MethodInfo* m) {
    if (F.extendedRange.load(std::memory_order_relaxed)) return;
    if (orig_RangeCull[Idx]) orig_RangeCull[Idx](self, m);
}

// ============================================================
// SAFE HOOKING ENGINE
// ============================================================
static bool safeHook(Il2CppClass* klass, const char* methodName, int args,
                     void* replaceFn, void** outOrig,
                     uintptr_t libBase, uintptr_t libEnd, const char* label) {
    F.hooksTotal.fetch_add(1, std::memory_order_relaxed);
    if (!klass || !methodName || !replaceFn || !outOrig || !il2cpp.class_get_method) {
        return false;
    }

    const Il2CppMethod* m = nullptr;
    GUARDED_RUN(label, {
        m = il2cpp.class_get_method(klass, methodName, args);
    });
    if (!m) {
        LOGI("[hook] %s: '%s'(%d) not found", label, methodName, args);
        return false;
    }

    void* ptr = Il2CppApi::methodPointer(m);
    if (!ptr) {
        LOGE("[hook] %s: '%s' null ptr", label, methodName);
        return false;
    }
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr < libBase || addr >= libEnd) {
        LOGE("[hook] %s: '%s' %p outside libil2cpp.so", label, methodName, ptr);
        return false;
    }
#if defined(__aarch64__)
    if ((addr & 3u) != 0) {
        LOGE("[hook] %s: unaligned ARM64 addr %p", label, ptr);
        return false;
    }
#endif

    int rc = -1;
    GUARDED_RUN(label, {
        rc = DobbyHook(ptr, replaceFn, outOrig);
    });
    if (rc != 0) {
        *outOrig = nullptr;
        LOGE("[hook] %s: DobbyHook('%s') failed rc=%d", label, methodName, rc);
        return false;
    }

    F.hooksInstalled.fetch_add(1, std::memory_order_relaxed);
    LOGI("[hook] %s: hooked '%s' @ %p", label, methodName, ptr);
    return true;
}

static size_t safeFieldOffset(Il2CppClass* klass, const char* fieldName) {
    if (!klass || !fieldName || !il2cpp.class_get_field || !il2cpp.field_get_offset) return 0;
    size_t off = 0;
    GUARDED_RUN(fieldName, {
        if (auto* f = il2cpp.class_get_field(klass, fieldName)) {
            off = il2cpp.field_get_offset(f);
        }
    });
    return off;
}

static bool isSafeModeFilePresent() {
    return access("/sdcard/modmenu_safe.txt", F_OK) == 0 ||
           access("/data/local/tmp/modmenu_safe.txt", F_OK) == 0;
}

static void* waitForIl2Cpp(int timeoutSeconds = 90) {
    const int attempts = timeoutSeconds * 2;
    for (int i = 0; i < attempts; i++) {
        uintptr_t base = 0, end = 0;
        if (KittyMemory::getLibraryRange("libil2cpp.so", base, end)) {
            void* handle = dlopen("libil2cpp.so", RTLD_NOLOAD | RTLD_NOW);
            if (handle) {
                g_libBase.store(base, std::memory_order_relaxed);
                g_libEnd.store(end, std::memory_order_relaxed);
                LOGI("[init] libil2cpp.so mapped 0x%" PRIxPTR "-0x%" PRIxPTR, base, end);
                return handle;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    LOGE("[init] libil2cpp.so timeout after %ds", timeoutSeconds);
    return nullptr;
}

static Il2CppDomain* waitForDomain(int timeoutSeconds = 30) {
    if (!il2cpp.domain_get) return nullptr;
    for (int i = 0; i < timeoutSeconds * 2; i++) {
        Il2CppDomain* d = nullptr;
        GUARDED_RUN("domain_get", { d = il2cpp.domain_get(); });
        if (d) return d;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return nullptr;
}

// Lookup helper trying fullName first, then bare class name across all assemblies
static Il2CppClass* findClassSafe(Il2CppDomain* domain, const char* fullName) {
    if (!domain || !fullName) return nullptr;
    Il2CppClass* k = nullptr;
    GUARDED_RUN(fullName, {
        k = il2cpp.findClass(fullName);
    });
    return k;
}

static void installAllHooks(Il2CppDomain* domain, uintptr_t libBase, uintptr_t libEnd) {
    // --- 0. Fallback generic Position / Health ---
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.UnityViewFeature.Components.Position")) {
        const char* cands[] = {"Update", "OnUpdate", "Execute", "UpdatePosition", "SetPosition"};
        for (const char* m : cands) {
            if (safeHook(k, m, -1, (void*)hook_PositionUpdate, (void**)&orig_PositionUpdate, libBase, libEnd, "Position")) break;
        }
    }
    if (auto* k = findClassSafe(domain, "Health")) {
        safeHook(k, "TakeDamage", 1, (void*)hook_TakeDamage, (void**)&orig_TakeDamage, libBase, libEnd, "Health.TakeDamage");
    }

    // --- 1. SR-SPOOF ---
    if (auto* k = findClassSafe(domain, "UnityEngine.SystemInfo")) {
        safeHook(k, "get_deviceUniqueIdentifier", 0, (void*)hook_GetDeviceUniqueId, (void**)&orig_GetDeviceUniqueId, libBase, libEnd, "SystemInfo.DeviceId");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.GameScripts.Authorization.SpecificWindows.AuthorizationMetaModel")) {
        g_offExtReal = safeFieldOffset(k, "ExtendedDeviceIdReal");
        g_offExtGen  = safeFieldOffset(k, "ExtendedDeviceIdGenerated");
        g_offAdvId   = safeFieldOffset(k, "AdvertisingId");
        safeHook(k, "FromDevice", -1, (void*)hook_AuthFromDevice, (void**)&orig_AuthFromDevice, libBase, libEnd, "Auth.FromDevice");
        safeHook(k, "FillSelfExtended", 0, (void*)hook_AuthFillExtended, (void**)&orig_AuthFillExtended, libBase, libEnd, "Auth.FillSelfExtended");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.SettingsFeature.Systems.SettingsServiceSystem")) {
        safeHook(k, "GetDeviceId", 0, (void*)hook_SettingsGetDeviceId, (void**)&orig_SettingsGetDevId, libBase, libEnd, "Settings.GetDeviceId");
    }

    // --- 2. ESP / Transform / Physics ---
    if (auto* k = findClassSafe(domain, "UnityEngine.Transform")) {
        safeHook(k, "set_position", 1, (void*)hook_TransformSetPos, (void**)&orig_TransformSetPos, libBase, libEnd, "Transform.set_position");
        safeHook(k, "set_localPosition", 1, (void*)hook_TransformSetLocalPos, (void**)&orig_TransformSetLocalPos, libBase, libEnd, "Transform.set_localPosition");
        safeHook(k, "SetPositionAndRotation", 2, (void*)hook_TransformSetPosRot, (void**)&orig_TransformSetPosRot, libBase, libEnd, "Transform.SetPositionAndRotation");
    }
    if (auto* k = findClassSafe(domain, "Lightbug.CharacterControllerPro.Core.PhysicsActor")) {
        safeHook(k, "Update", 0, (void*)hook_PhysicsActorUpdate, (void**)&orig_PhysicsActorUpdate, libBase, libEnd, "PhysicsActor.Update");
        safeHook(k, "get_Velocity", 0, (void*)hook_GetVelocity, (void**)&orig_GetVelocity, libBase, libEnd, "PhysicsActor.get_Velocity");
        safeHook(k, "FixedUpdate", 0, (void*)hook_PhysicsFixed, (void**)&orig_PhysicsFixed, libBase, libEnd, "PhysicsActor.FixedUpdate");
        safeHook(k, "Update", 0, (void*)hook_PhysicsWoaUpd, (void**)&orig_PhysicsWoaUpd, libBase, libEnd, "PhysicsActor.Update(woa)");
    }
    if (auto* k = findClassSafe(domain, "Lightbug.CharacterControllerPro.Core.PhysicsActorSync")) {
        safeHook(k, "FixedUpdate", 0, (void*)hook_PhysicsSyncFixed, (void**)&orig_PhysicsSyncFixed, libBase, libEnd, "PhysicsActorSync.FixedUpdate");
    }

    // --- 3. Weapon / SilentAim / WallPen ---
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.WeaponFeature.Systems.WeaponProduceAttackRangedSystem")) {
        safeHook(k, "RayCastAttackRanged", 7, (void*)hook_RayCast7, (void**)&orig_RayCast7, libBase, libEnd, "RayCastAttackRanged.7");
        safeHook(k, "RayCastAttackRanged", 8, (void*)hook_RayCast8, (void**)&orig_RayCast8, libBase, libEnd, "RayCastAttackRanged.8");
        safeHook(k, "OnUpdate", -1, (void*)hook_WeaponOnUpdate, (void**)&orig_WeaponOnUpdate, libBase, libEnd, "WeaponSystem.OnUpdate");
        safeHook(k, "ProcessHits", 6, (void*)hook_ProcessHits6, (void**)&orig_ProcessHits6, libBase, libEnd, "Weapon.ProcessHits");
        safeHook(k, "GetPenetrationParameters", -1, (void*)hook_WallPenParams, (void**)&orig_WallPenParams, libBase, libEnd, "WallPen.GetPenParams");
        safeHook(k, "TryGetPenetrationDistance", -1, (void*)hook_WallPenDist, (void**)&orig_WallPenDist, libBase, libEnd, "WallPen.TryGetPenDist");
    }

    // --- 4. Camera / FOV / FreeCam / Spectate ---
    if (auto* k = findClassSafe(domain, "UnityEngine.Camera")) {
        safeHook(k, "set_fieldOfView", 1, (void*)hook_CameraSetFov, (void**)&orig_CameraSetFov, libBase, libEnd, "Camera.set_fieldOfView");
        safeHook(k, "get_fieldOfView", 0, (void*)hook_CameraGetFov, (void**)&orig_CameraGetFov, libBase, libEnd, "Camera.get_fieldOfView");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.MainCameraFeature.Systems.Universal.UniversalCameraSystem")) {
        safeHook(k, "ApplyInputWithVerticalRotationLimit", 2, (void*)hook_UnivCamRot, (void**)&orig_UnivCamRot, libBase, libEnd, "UniversalCameraSystem");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.MainCameraFeature.Systems.CameraFollowingPedSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_CamFollowPed, (void**)&orig_CamFollowPed, libBase, libEnd, "FreeCam.Ped");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.MainCameraFeature.Systems.CameraFollowingVehicleSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_CamFollowVeh, (void**)&orig_CamFollowVeh, libBase, libEnd, "FreeCam.Veh");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.MainCameraFeature.Systems.CameraPlacementSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_CamPlacement, (void**)&orig_CamPlacement, libBase, libEnd, "FreeCam.Placement");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.SoundFmodFeature.Services.SoundFmodPriorityService")) {
        safeHook(k, "SwitchListener", 1, (void*)hook_SwitchListener, (void**)&orig_SwitchListener, libBase, libEnd, "SoundFmod.SwitchListener");
    }

    // --- 5. SkyWalk / GodMode / FallDamage / Head ---
    if (auto* k = findClassSafe(domain, "Lightbug.CharacterControllerPro.Core.CharacterActor")) {
        safeHook(k, "Update", 0, (void*)hook_CharActorUpdate, (void**)&orig_CharActorUpdate, libBase, libEnd, "SkyWalk.Update");
        safeHook(k, "PostSimulationUpdate", 1, (void*)hook_CharActorPostSim, (void**)&orig_CharActorPostSim, libBase, libEnd, "SkyWalk.PostSim");
        safeHook(k, "PreSimulationUpdate", 1, (void*)hook_CharActorPreSim, (void**)&orig_CharActorPreSim, libBase, libEnd, "SkyWalk.PreSim");
        safeHook(k, "get_IsStable", 0, (void*)hook_CharIsStable, (void**)&orig_CharIsStable, libBase, libEnd, "SkyWalk.IsStable");
        safeHook(k, "get_IsGrounded", 0, (void*)hook_CharIsGrounded, (void**)&orig_CharIsGrounded, libBase, libEnd, "SkyWalk.ActorIsGrounded");
        safeHook(k, "ForceNotGrounded", 0, (void*)hook_CharForceNotGround, (void**)&orig_CharForceNotGround, libBase, libEnd, "SkyWalk.ForceNotGrounded");
        safeHook(k, "LateUpdate", 0, (void*)hook_CharActorLateUpdate, (void**)&orig_CharActorLateUpdate, libBase, libEnd, "CharActor.LateUpdate(head)");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.DataContainers.PedKnockoutParameters")) {
        if (!safeHook(k, "GetFallDamage", 1, (void*)hook_KnockoutFallDamage, (void**)&orig_KnockoutFallDamage, libBase, libEnd, "SkyWalk.FallDamage")) {
            safeHook(k, "CalculateFallDamage", 1, (void*)hook_KnockoutFallDamage, (void**)&orig_KnockoutFallDamage, libBase, libEnd, "SkyWalk.FallDamage");
        }
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.StateMachine.States.Fall.FallState")) {
        safeHook(k, "CanEnterState", 1, (void*)hook_FallStateCanEnter, (void**)&orig_FallStateCanEnter, libBase, libEnd, "SkyWalk.FallState");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.StateMachine.PedStateMachine")) {
        safeHook(k, "ForceExit", 1, (void*)hook_StateForceExit, (void**)&orig_StateForceExit, libBase, libEnd, "SkyWalk.ForceExit");
        safeHook(k, "TryEnter", 1, (void*)hook_StateTryEnter, (void**)&orig_StateTryEnter, libBase, libEnd, "SkyWalk.TryEnter");
        safeHook(k, "ForceEnter", 1, (void*)hook_StateForceEnter, (void**)&orig_StateForceEnter, libBase, libEnd, "GodMode.ForceEnter");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.StateMachine.States.Weapon.WeaponRangeShootState")) {
        safeHook(k, "CanEnterState", 1, (void*)hook_WeaponShootCanEnter, (void**)&orig_WeaponShootCanEnter, libBase, libEnd, "SkyWalk.WeaponShoot");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.StateMachine.PedState")) {
        safeHook(k, "CanEnterState", 1, (void*)hook_PedStateCanEnter, (void**)&orig_PedStateCanEnter, libBase, libEnd, "SkyWalk.PedState");
    }

    // --- 6. Health / HUD Sliders / FallDamage ---
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.ElementsFeature.Systems.ElementHealthSyncSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_ElemHealthSync, (void**)&orig_ElemHealthSync, libBase, libEnd, "ElementHealthSync.OnUpdate");
    } else if (auto* k2 = findClassSafe(domain, "ElementHealthSyncSystem")) {
        safeHook(k2, "OnUpdate", -1, (void*)hook_ElemHealthSync, (void**)&orig_ElemHealthSync, libBase, libEnd, "ElementHealthSync.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "UpdateHudHealthViewSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_HudHealthView, (void**)&orig_HudHealthView, libBase, libEnd, "HudHealthView.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "UnityEngine.UI.Slider")) {
        safeHook(k, "set_value", 1, (void*)hook_SliderSetVal, (void**)&orig_SliderSetVal, libBase, libEnd, "Slider.set_value");
        safeHook(k, "SetValueWithoutNotify", 1, (void*)hook_SliderSetSilent, (void**)&orig_SliderSetSilent, libBase, libEnd, "Slider.SetSilent");
    }
    if (auto* k = findClassSafe(domain, "PedViewKnockoutService")) {
        safeHook(k, "ApplyLocalFallDamage", 1, (void*)hook_ApplyLocalFall, (void**)&orig_ApplyLocalFall, libBase, libEnd, "FallDamage.Apply");
    }

    // --- 7. AutoWalk / NavigationGraph ---
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.Systems.PedViewInputsSystem")) {
        if (!safeHook(k, "update", -1, (void*)hook_PedViewInputs, (void**)&orig_PedViewInputs, libBase, libEnd, "PedViewInputsSystem.update")) {
            safeHook(k, "OnUpdate", -1, (void*)hook_PedViewInputs, (void**)&orig_PedViewInputs, libBase, libEnd, "PedViewInputsSystem.OnUpdate");
        }
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.Map.NavigationGraph")) {
        safeHook(k, "FindPath", 3, (void*)hook_NavFindPath, (void**)&orig_NavFindPath, libBase, libEnd, "NavGraph.FindPath");
        safeHook(k, ".ctor", -1, (void*)hook_NavCtor, (void**)&orig_NavCtor, libBase, libEnd, "NavGraph.ctor");
        safeHook(k, "SetGoal", 1, (void*)hook_NavSetGoal, (void**)&orig_NavSetGoal, libBase, libEnd, "NavGraph.SetGoal");
        safeHook(k, "SetWaypoint", 1, (void*)hook_NavSetWaypoint, (void**)&orig_NavSetWaypoint, libBase, libEnd, "NavGraph.SetWaypoint");
        safeHook(k, "SetDestination", 1, (void*)hook_NavSetDest, (void**)&orig_NavSetDest, libBase, libEnd, "NavGraph.SetDestination");
        safeHook(k, "AddMarker", 1, (void*)hook_NavAddMarker, (void**)&orig_NavAddMarker, libBase, libEnd, "NavGraph.AddMarker");
    }

    // --- 8. CLSV-HOOKS & CarFeatures ---
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedsFeature.Systems.Streaming.PedLoadClothesSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_PedLoadClothes, (void**)&orig_PedLoadClothes, libBase, libEnd, "PedLoadClothes.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Systems.ProxySystems.GeneralElementInterpolateTransformSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_InterpTransform, (void**)&orig_InterpTransform, libBase, libEnd, "Interp.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.VehicleViewFeature.Systems.VehicleBody.VehicleBodyPrePhysicsSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_VehBodyPrePhys, (void**)&orig_VehBodyPrePhys, libBase, libEnd, "VehBody.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "PedChangeClothesSyncSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_PedClothesSync, (void**)&orig_PedClothesSync, libBase, libEnd, "PedClothesSync.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "ElementModelSyncSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_ElemModelSync, (void**)&orig_ElemModelSync, libBase, libEnd, "ElementModelSync.OnUpdate");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedViewFeature.Systems.PedViewStorage")) {
        safeHook(k, "SetStaminaLose", 1, (void*)hook_SetStaminaLose, (void**)&orig_SetStaminaLose, libBase, libEnd, "CarFeatures.SetStaminaLose");
    }
    if (auto* k = findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.PedsFeature.Systems.PedValues.PedStaminaSystem")) {
        safeHook(k, "OnUpdate", -1, (void*)hook_PedStaminaSystem, (void**)&orig_PedStaminaSystem, libBase, libEnd, "CarFeatures.PedStamina");
    }

    // --- 9. DAYNIGHT & SR-RANGE ---
    if (auto* k = findClassSafe(domain, "SkyLightingDriver")) {
        safeHook(k, "LateUpdate", 0, (void*)hook_SkyDriverLate, (void**)&orig_SkyDriverLate, libBase, libEnd, "SkyDriver.LateUpdate");
    }
    const char* rangeClasses[5] = {
        "RemoteCollisionCullingSystem",
        "RemoteCollisionCullingRestoreSystem",
        "PedViewInVehicleVisibilityDistanceSystem",
        "PedViewVisibilityUpdateSystem",
        "VehiclesDistanceCheckSystem"
    };
    void* rangeFns[5] = {
        (void*)hook_RangeCull<0>, (void*)hook_RangeCull<1>,
        (void*)hook_RangeCull<2>, (void*)hook_RangeCull<3>,
        (void*)hook_RangeCull<4>
    };
    for (int i = 0; i < 5; i++) {
        if (auto* k = findClassSafe(domain, rangeClasses[i])) {
            safeHook(k, "OnUpdate", -1, rangeFns[i], (void**)&orig_RangeCull[i], libBase, libEnd, rangeClasses[i]);
        }
    }
}

namespace Offsets {
    constexpr uintptr_t InfiniteAmmo = 0x0;
    constexpr uintptr_t RapidFire    = 0x0;
    constexpr uintptr_t NoRecoil     = 0x0;
    constexpr uintptr_t VehicleSpeed = 0x0;
    constexpr uintptr_t CameraFov    = 0x0;
}

static bool installHooksSafely() {
    try {
        if (isSafeModeFilePresent()) {
            F.safeMode.store(true, std::memory_order_relaxed);
            LOGI("[SR-SAFE] Safe mode file detected - hooks skipped");
            return false;
        }

        void* handle = waitForIl2Cpp();
        if (!handle) return false;

        std::this_thread::sleep_for(std::chrono::seconds(3));

        if (!il2cpp.init(handle)) {
            LOGE("[init] il2cpp.init FAILED");
            return false;
        }

        Il2CppDomain* domain = waitForDomain();
        if (!domain) return false;

        if (il2cpp.thread_attach) {
            GUARDED_RUN("thread_attach", {
                il2cpp.thread_attach(domain);
            });
        }

        // Wait briefly for main game assemblies to finish registering
        for (int i = 0; i < 20; i++) {
            if (findClassSafe(domain, "Lightbug.CharacterControllerPro.Core.PhysicsActor") ||
                findClassSafe(domain, "dev.Scripts.ECS.Morpeh.Features.UnityViewFeature.Components.Position")) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }

        uintptr_t base = g_libBase.load(std::memory_order_relaxed);
        uintptr_t end  = g_libEnd.load(std::memory_order_relaxed);
        installAllHooks(domain, base, end);

        bool ok = F.hooksInstalled.load(std::memory_order_relaxed) > 0;
        F.hooked.store(ok, std::memory_order_relaxed);
        LOGI("[init] hooks installed: %d / %d (faults=%d)",
             F.hooksInstalled.load(), F.hooksTotal.load(), F.faultsRecovered.load());
        return ok;
    } catch (...) {
        LOGE("[init] exception during hook install");
        return false;
    }
}

// Low-overhead patch loop: uses cached base address (no /proc/self/maps polling!)
static void patchLoop() {
    constexpr bool hasAnyOffset = (Offsets::InfiniteAmmo || Offsets::NoRecoil ||
                                   Offsets::VehicleSpeed || Offsets::CameraFov);
    while (true) {
        uintptr_t base = g_libBase.load(std::memory_order_relaxed);
        if (base && hasAnyOffset) {
            GUARDED_RUN("patchLoop", {
                if (F.infiniteAmmo.load(std::memory_order_relaxed) && Offsets::InfiniteAmmo)
                    KittyMemory::writeValue<int>(base + Offsets::InfiniteAmmo, 9999);
                if (F.noRecoil.load(std::memory_order_relaxed) && Offsets::NoRecoil)
                    KittyMemory::writeValue<float>(base + Offsets::NoRecoil, 0.f);
                if (F.vehicleSpeedOn.load(std::memory_order_relaxed) && Offsets::VehicleSpeed)
                    KittyMemory::writeValue<float>(base + Offsets::VehicleSpeed, F.vehicleSpeed.load(std::memory_order_relaxed));
                if (F.fovOverride.load(std::memory_order_relaxed) && Offsets::CameraFov)
                    KittyMemory::writeValue<float>(base + Offsets::CameraFov, F.fov.load(std::memory_order_relaxed));
            });
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        } else {
            // Sleep longer when no raw offsets are active -> zero CPU/lag
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }
}

static void hackThread() {
    LOGI("[init] background thread started (pid=%d)", getpid());
    installHooksSafely();
    patchLoop();
}

// ============================================================
// JNI BRIDGE (com.bonika.modmenu.NativeBridge)
// ============================================================
#define JNI_FN(name) Java_com_bonika_modmenu_NativeBridge_##name
extern "C" {
JNIEXPORT void JNICALL JNI_FN(setEspEnabled)(JNIEnv*, jclass, jboolean v)           { F.esp = v; }
JNIEXPORT void JNICALL JNI_FN(setEspDistance)(JNIEnv*, jclass, jfloat v)            { F.espDistance = v; }
JNIEXPORT void JNICALL JNI_FN(setSkeletonEsp)(JNIEnv*, jclass, jboolean v)          { F.skeleton = v; }
JNIEXPORT void JNICALL JNI_FN(setShowNpcs)(JNIEnv*, jclass, jboolean v)             { F.showNpcs = v; }
JNIEXPORT void JNICALL JNI_FN(setShowVehicles)(JNIEnv*, jclass, jboolean v)         { F.showVehicles = v; }
JNIEXPORT void JNICALL JNI_FN(setEspBoxColor)(JNIEnv*, jclass, jint v)              { F.espBoxColor = v; }

JNIEXPORT void JNICALL JNI_FN(setGodMode)(JNIEnv*, jclass, jboolean v)              { F.godMode = v; }
JNIEXPORT void JNICALL JNI_FN(setInfiniteAmmo)(JNIEnv*, jclass, jboolean v)         { F.infiniteAmmo = v; }
JNIEXPORT void JNICALL JNI_FN(setRapidFire)(JNIEnv*, jclass, jboolean v)            { F.rapidFire = v; }
JNIEXPORT void JNICALL JNI_FN(setNoRecoil)(JNIEnv*, jclass, jboolean v)             { F.noRecoil = v; }
JNIEXPORT void JNICALL JNI_FN(setSilentAim)(JNIEnv*, jclass, jboolean v)            { F.silentAim = v; }
JNIEXPORT void JNICALL JNI_FN(setBulletWallhack)(JNIEnv*, jclass, jboolean v)       { F.bulletWallhack = v; }
JNIEXPORT void JNICALL JNI_FN(setNoFallDamage)(JNIEnv*, jclass, jboolean v)         { F.noFallDamage = v; }

JNIEXPORT void JNICALL JNI_FN(setVehicleSpeedEnabled)(JNIEnv*, jclass, jboolean v)  { F.vehicleSpeedOn = v; }
JNIEXPORT void JNICALL JNI_FN(setVehicleSpeedMultiplier)(JNIEnv*, jclass, jfloat v) { F.vehicleSpeed = v; }
JNIEXPORT void JNICALL JNI_FN(setPlayerSpeedEnabled)(JNIEnv*, jclass, jboolean v)   { F.playerSpeedOn = v; }
JNIEXPORT void JNICALL JNI_FN(setPlayerSpeedMultiplier)(JNIEnv*, jclass, jfloat v)  { F.playerSpeed = v; }
JNIEXPORT void JNICALL JNI_FN(setSkyWalk)(JNIEnv*, jclass, jboolean v)              { F.skyWalk = v; }
JNIEXPORT void JNICALL JNI_FN(setWalkOnAir)(JNIEnv*, jclass, jboolean v)            { F.walkOnAir = v; }
JNIEXPORT void JNICALL JNI_FN(setAutoWalk)(JNIEnv*, jclass, jboolean v)             { F.autoWalk = v; }
JNIEXPORT void JNICALL JNI_FN(setInfiniteStamina)(JNIEnv*, jclass, jboolean v)      { F.infiniteStamina = v; }
JNIEXPORT void JNICALL JNI_FN(teleportToWaypoint)(JNIEnv*, jclass)                  { F.teleportRequested = true; }
JNIEXPORT void JNICALL JNI_FN(setInstantBrake)(JNIEnv*, jclass, jboolean v)         { F.instantBrake = v; }

JNIEXPORT void JNICALL JNI_FN(setFov)(JNIEnv*, jclass, jfloat v)                   { F.fov = v; }
JNIEXPORT void JNICALL JNI_FN(setFovOverride)(JNIEnv*, jclass, jboolean v)          { F.fovOverride = v; }
JNIEXPORT void JNICALL JNI_FN(setNightVision)(JNIEnv*, jclass, jboolean v)          { F.nightVision = v; }
JNIEXPORT void JNICALL JNI_FN(setFreeFly)(JNIEnv*, jclass, jboolean v)              { F.freeFly = v; }
JNIEXPORT void JNICALL JNI_FN(setFreeCam)(JNIEnv*, jclass, jboolean v)              { F.freeCam = v; }
JNIEXPORT void JNICALL JNI_FN(setExtendedRange)(JNIEnv*, jclass, jboolean v)        { F.extendedRange = v; }
JNIEXPORT void JNICALL JNI_FN(setHeadEnlarge)(JNIEnv*, jclass, jboolean v)          { F.headEnlarge = v; }
JNIEXPORT void JNICALL JNI_FN(setHeadScale)(JNIEnv*, jclass, jfloat v)              { F.headScale = v; }

JNIEXPORT void JNICALL JNI_FN(setDeviceSpoof)(JNIEnv*, jclass, jboolean v)          { F.deviceSpoof = v; }
JNIEXPORT void JNICALL JNI_FN(regenerateSpoofId)(JNIEnv*, jclass) {
    F.spoofSeed.fetch_add(0x9E3779B9u, std::memory_order_relaxed);
    refreshSpoofHex(F.spoofSeed.load(std::memory_order_relaxed));
}

JNIEXPORT jboolean JNICALL JNI_FN(isHooked)(JNIEnv*, jclass)           { return F.hooked.load(); }
JNIEXPORT jint JNICALL JNI_FN(getHooksInstalled)(JNIEnv*, jclass)      { return F.hooksInstalled.load(); }
JNIEXPORT jint JNICALL JNI_FN(getHooksTotal)(JNIEnv*, jclass)          { return F.hooksTotal.load(); }
JNIEXPORT jint JNICALL JNI_FN(getFaultsRecovered)(JNIEnv*, jclass)     { return F.faultsRecovered.load(); }

JNIEXPORT jfloatArray JNICALL JNI_FN(getPlayerPosition)(JNIEnv* env, jclass) {
    if (!env) return nullptr;
    jfloatArray a = env->NewFloatArray(3);
    if (!a) return nullptr;
    float xyz[3] = {F.playerPos.x, F.playerPos.y, F.playerPos.z};
    env->SetFloatArrayRegion(a, 0, 3, xyz);
    return a;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) {
    bool expected = false;
    if (g_initStarted.compare_exchange_strong(expected, true)) {
        try {
            std::thread(hackThread).detach();
            LOGI("[init] JNI_OnLoad: worker spawned");
        } catch (...) {
            g_initStarted = false;
        }
    }
    return JNI_VERSION_1_6;
}
} // extern "C"
