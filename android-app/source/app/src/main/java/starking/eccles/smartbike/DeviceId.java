package starking.eccles.smartbike;

/**
 * Mirrors Eccles::DeviceID (CommandTypes.h / the firmware's HardwareDevice.h) ordinal-for-
 * ordinal. Only used on the Java side to know which UI control a native DEVICE_STATE /
 * DEVICE_VALUE message refers to, and to build the phrase a button tap sends down as text.
 */
public enum DeviceId {
    UNKNOWN_DEVICE,
    IGNITION, HORN, HEADLAMP, LEFT_TURN, RIGHT_TURN, STARTER, ENGINE,
    IGNITION_FB, FUEL_GAUGE, TEMP_GAUGE, MICROPHONE,
    SHOCK_SENSOR,
    ALL,
    CONFIG,
    BLUETOOTH,
    CONVERSATION;

    public static DeviceId fromNative(int ordinal) {
        DeviceId[] values = values();
        if (ordinal < 0 || ordinal >= values.length) return UNKNOWN_DEVICE;
        return values[ordinal];
    }

    /** The word CommandFactory.cpp's target phrase table expects for this device. */
    public String phraseWord() {
        switch (this) {
            case HEADLAMP: return "headlamp";
            case HORN: return "horn";
            case IGNITION: return "ignition";
            case LEFT_TURN: return "left turn signal";
            case RIGHT_TURN: return "right turn signal";
            case STARTER: return "starter";
            case ENGINE: return "engine";
            case FUEL_GAUGE: return "fuel gauge";
            case TEMP_GAUGE: return "temperature";
            case MICROPHONE: return "microphone";
            case SHOCK_SENSOR: return "shock sensor";
            case BLUETOOTH: return "bluetooth";
            case CONVERSATION: return "conversation";
            case ALL: return "everything";
            default: return "";
        }
    }
}
