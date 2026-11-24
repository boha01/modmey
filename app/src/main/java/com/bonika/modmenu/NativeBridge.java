package com.bonika.modmenu;

public class NativeBridge {
    private static volatile boolean loaded = false;

    static {
        try {
            System.loadLibrary("modmenu");
            loaded = true;
        } catch (Throwable ignored) {
            loaded = false;
        }
    }

    public static boolean isLibraryLoaded() { return loaded; }

    // PLAYER / ESP
    public static native void setEspEnabled(boolean v);
    public static native void setEspDistance(float meters);
    public static native void setSkeletonEsp(boolean v);
    public static native void setShowNpcs(boolean v);
    public static native void setShowVehicles(boolean v);
    public static native void setEspBoxColor(int argb);

    // COMBAT & PROTECTION
    public static native void setGodMode(boolean v);
    public static native void setInfiniteAmmo(boolean v);
    public static native void setRapidFire(boolean v);
    public static native void setNoRecoil(boolean v);
    public static native void setSilentAim(boolean v);
    public static native void setBulletWallhack(boolean v);
    public static native void setNoFallDamage(boolean v);

    // MOVEMENT & VEHICLE
    public static native void setVehicleSpeedEnabled(boolean v);
    public static native void setVehicleSpeedMultiplier(float v);
    public static native void setPlayerSpeedEnabled(boolean v);
    public static native void setPlayerSpeedMultiplier(float v);
    public static native void setSkyWalk(boolean v);
    public static native void setWalkOnAir(boolean v);
    public static native void setAutoWalk(boolean v);
    public static native void setInfiniteStamina(boolean v);
    public static native void teleportToWaypoint();
    public static native void setInstantBrake(boolean v);

    // VISUALS & CAMERA
    public static native void setFov(float v);
    public static native void setFovOverride(boolean v);
    public static native void setNightVision(boolean v);
    public static native void setFreeFly(boolean v);
    public static native void setFreeCam(boolean v);
    public static native void setExtendedRange(boolean v);
    public static native void setHeadEnlarge(boolean v);
    public static native void setHeadScale(float v);

    // SPOOFER & PROTECTION
    public static native void setDeviceSpoof(boolean v);
    public static native void regenerateSpoofId();

    // TELEMETRY / STATE
    public static native float[] getPlayerPosition();
    public static native boolean isHooked();
    public static native int getHooksInstalled();
    public static native int getHooksTotal();
    public static native int getFaultsRecovered();
}
