package com.bonika.modmenu;

public final class NativeBridge {
    private static volatile boolean loaded = false;
    private static volatile boolean loadAttempted = false;

    private NativeBridge() {}

    public static synchronized void load() {
        if (loadAttempted) return;
        loadAttempted = true;
        try {
            System.loadLibrary("modmenu");
            loaded = true;
        } catch (Throwable t) {
            loaded = false;
        }
    }

    public static boolean isLibraryLoaded() { return loaded; }

    public static native void setEspEnabled(boolean v);
    public static native void setEspDistance(float v);
    public static native void setSkeletonEsp(boolean v);
    public static native void setShowNpcs(boolean v);
    public static native void setShowVehicles(boolean v);
    public static native void setEspBoxColor(int v);

    public static native void setGodMode(boolean v);
    public static native void setInfiniteAmmo(boolean v);
    public static native void setRapidFire(boolean v);
    public static native void setNoRecoil(boolean v);
    public static native void setSilentAim(boolean v);
    public static native void setBulletWallhack(boolean v);
    public static native void setNoFallDamage(boolean v);

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

    public static native void setFov(float v);
    public static native void setFovOverride(boolean v);
    public static native void setNightVision(boolean v);
    public static native void setFreeFly(boolean v);
    public static native void setFreeCam(boolean v);
    public static native void setExtendedRange(boolean v);
    public static native void setHeadEnlarge(boolean v);
    public static native void setHeadScale(float v);

    public static native void setDeviceSpoof(boolean v);
    public static native void regenerateSpoofId();

    public static native boolean isHooked();
    public static native int getHooksInstalled();
    public static native int getHooksTotal();
    public static native int getFaultsRecovered();
    public static native float[] getPlayerPosition();

    /** Decrypt q.dat (or any file) via SilkGuard.open(byte[]). Returns null on failure. */
    public static native byte[] decryptQDat(String path);
}
