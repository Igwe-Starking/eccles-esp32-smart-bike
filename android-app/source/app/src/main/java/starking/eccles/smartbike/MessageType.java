package starking.eccles.smartbike;

/** Mirrors Eccles::MessageType (Engine.h) - what NativeMessage.type will be set to. */
public final class MessageType {
    private MessageType() {}
    public static final int CONNECTED = 0;
    public static final int DISCONNECTED = 1;
    public static final int DEVICE_STATE = 2;
    public static final int DEVICE_VALUE = 3;
    public static final int LOG = 4;
    public static final int ERROR = 5;
    public static final int CONVERSATION_AUDIO_CONFIG = 6;
}
