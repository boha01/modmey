#pragma once
#include <atomic>
#include <cstdint>

struct Vec3 { float x = 0.f, y = 0.f, z = 0.f; };

struct Features {
    // PLAYER / ESP
    std::atomic<bool> esp{false}, skeleton{false}, showNpcs{true}, showVehicles{false};
    std::atomic<float> espDistance{500.f};
    std::atomic<int> espBoxColor{static_cast<int>(0xFFA855F7)};

    // COMBAT & PROTECTION
    std::atomic<bool> godMode{false}, infiniteAmmo{false}, rapidFire{false}, noRecoil{false};
    std::atomic<bool> silentAim{false}, bulletWallhack{false}, noFallDamage{false};

    // MOVEMENT & VEHICLE
    std::atomic<bool> vehicleSpeedOn{false}, instantBrake{false};
    std::atomic<float> vehicleSpeed{1.f};
    std::atomic<bool> playerSpeedOn{false};
    std::atomic<float> playerSpeed{1.f};
    std::atomic<bool> skyWalk{false}, walkOnAir{false}, autoWalk{false}, infiniteStamina{false};
    std::atomic<bool> teleportRequested{false};

    // VISUALS & CAMERA
    std::atomic<float> fov{60.f};
    std::atomic<bool> fovOverride{false};
    std::atomic<bool> nightVision{false}, freeFly{false}, freeCam{false};
    std::atomic<bool> extendedRange{false}, headEnlarge{false};
    std::atomic<float> headScale{2.f};

    // PROTECTION / SPOOFER
    std::atomic<bool> deviceSpoof{false};
    std::atomic<bool> safeMode{false};
    std::atomic<uint32_t> spoofSeed{0x5A17B04Au};

    // RUNTIME HOOK TELEMETRY
    std::atomic<bool> hooked{false};
    std::atomic<int> hooksInstalled{0};
    std::atomic<int> hooksTotal{0};
    std::atomic<int> faultsRecovered{0};

    // STATE from hooks
    Vec3 playerPos;
    Vec3 waypoint;
    std::atomic<bool> hasWaypoint{false};
};

inline Features F;
