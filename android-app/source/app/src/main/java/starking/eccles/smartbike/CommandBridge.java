package starking.eccles.smartbike;

import java.nio.charset.StandardCharsets;

/**
 * Static native bridge. Every method here is a thin
 * JNI call straight into native-lib.cpp / Eccles::Engine - no logic lives on this class.
 */
public final class CommandBridge {

    static {
        System.loadLibrary("eccles");
    }

    private CommandBridge() {}

    /**
     * Starts the native engine on its own thread (eccles_main, see cpp/eccles/main.cpp) once
     * connected to the firmware's IP, as discovered by UdpDiscovery.
     */
    public static native void initEngine(String firmwareIp);

    /**
     * Sends a fully-formed "Eccles ..." phrase down to CommandFactory, regardless of whether
     * it came from speech, the keyboard, or a UI button tap - they all funnel through here.
     */
    public static void send(int commandType, String text) {
        send(commandType, text.getBytes(StandardCharsets.UTF_8));
    }

    private static native void send(int commandType, byte[] utf8Text);

    /**
     * Blocks the calling thread until the engine has something to report. MessageThread owns
     * a dedicated background thread that does nothing but call this in a loop.
     */
    public static native NativeMessage requestMessage();

    /** Tears down the websocket connection, conversation audio, and the engine thread. */
    public static native void stopEngine();
}
