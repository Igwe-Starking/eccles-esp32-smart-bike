package starking.eccles.smartbike;

/**
 * Plain data holder returned by CommandBridge.requestMessage(). Field layout matches the
 * JNI constructor call built in native-lib.cpp - do not reorder without updating both sides.
 */
public class NativeMessage {
    public final int type;      // one of MessageType.*
    public final int device;    // DeviceId ordinal, when applicable
    public final int value;     // device state (0/1) or sensor value, meaning depends on type
    public final String text;   // LOG / ERROR text
    public final int extra1;    // CONVERSATION_AUDIO_CONFIG: sample rate
    public final int extra2;    // CONVERSATION_AUDIO_CONFIG: bit depth
    public final int extra3;    // CONVERSATION_AUDIO_CONFIG: channel count

    public NativeMessage(int type, int device, int value, String text, int extra1, int extra2, int extra3) {
        this.type = type;
        this.device = device;
        this.value = value;
        this.text = text;
        this.extra1 = extra1;
        this.extra2 = extra2;
        this.extra3 = extra3;
    }
}
