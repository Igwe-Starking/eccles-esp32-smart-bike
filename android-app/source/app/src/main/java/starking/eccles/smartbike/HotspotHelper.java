package starking.eccles.smartbike;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.wifi.WifiManager;

/**
 * The bike's firmware joins an existing WiFi network as a station (see WebTransport::prepare
 * on the firmware) rather than creating its own access point, so the phone's own hotspot has
 * to be the network it joins. Android has never offered a public API to flip the hotspot on
 * programmatically, so the best a well-behaved app can do is detect it's off and send the
 * user straight to the system hotspot screen.
 */
public final class HotspotHelper {

    private HotspotHelper() {}

    /**
     * Best-effort check. There's no official public API for "is my hotspot on" either;
     * WifiManager's tethering state getter is a hidden API on most OEM builds, so this uses
     * a reflective best-effort call and simply assumes "on" if it can't tell either way -
     * meaning worst case the user sees the settings prompt once even though it was already on.
     */
    public static boolean isHotspotLikelyEnabled(Context context) {
        try {
            WifiManager wifiManager = (WifiManager) context.getApplicationContext()
                    .getSystemService(Context.WIFI_SERVICE);
            java.lang.reflect.Method method = wifiManager.getClass().getMethod("isWifiApEnabled");
            method.setAccessible(true);
            return (Boolean) method.invoke(wifiManager);
        } catch (Exception reflectionUnavailable) {
            return true; // can't tell - don't nag the user with a false positive
        }
    }

    /** Sends the user to the OS hotspot/tethering screen so they can flip it on themselves. */
    public static void promptEnableHotspot(Context context) {
        Intent intent = new Intent("android.settings.TETHER_SETTINGS");
        if (intent.resolveActivity(context.getPackageManager()) != null) {
            context.startActivity(intent);
            return;
        }
        // fallback for OEMs that don't expose the direct tether screen under that action
        context.startActivity(new Intent(android.provider.Settings.ACTION_WIRELESS_SETTINGS));
    }
}
