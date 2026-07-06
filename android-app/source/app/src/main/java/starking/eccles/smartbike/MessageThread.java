package starking.eccles.smartbike;

import android.os.Handler;
import android.os.Looper;

/**
 * Owns the one thread whose entire job is to call CommandBridge.requestMessage() in a loop
 * and hop each result back onto the main thread for MainActivity to apply to the UI - exactly
 * the pattern the spec calls for, since requestMessage() blocks on the native condition
 * variable and must never be called from the UI thread.
 */
public class MessageThread extends Thread {

    /** Delivered on the main thread. */
    public interface Listener {
        void onNativeMessage(NativeMessage message);
    }

    private final Listener listener;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private volatile boolean running = true;

    public MessageThread(Listener listener) {
        super("EcclesMessageThread");
        this.listener = listener;
    }

    public void stopPolling() {
        running = false;
    }

    @Override
    public void run() {
        while (running) {
            final NativeMessage message = CommandBridge.requestMessage();
            if (message == null) {
                // engine has stopped (stopEngine() was called, or the connection died and
                // Engine::stop() was invoked) - nothing more will ever arrive
                break;
            }
            mainHandler.post(() -> listener.onNativeMessage(message));
        }
    }
}
