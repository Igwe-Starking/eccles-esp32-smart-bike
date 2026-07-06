package starking.eccles.smartbike;

import android.os.Handler;
import android.os.Looper;

import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;

/**
 * Listens on UDP port 4210 (see UDP_PORT in the firmware's components/eccles/include/
 * Transport.h) for the "ECCLES_IP:x.x.x.x" broadcast the bike sends once it's joined the
 * phone's hotspot and picked up a DHCP lease (see Transport.cpp's broadcast-on-connect logic).
 * Runs its own thread; delivers exactly one result then stops.
 */
public class UdpDiscovery extends Thread {

    private static final int DISCOVERY_PORT = 4210;
    private static final String BROADCAST_PREFIX = "ECCLES_IP:";

    /** Delivered on the main thread. onIpDiscovered fires at most once. */
    public interface Listener {
        void onIpDiscovered(String ip);
        void onDiscoveryFailed(String reason);
    }

    private final Listener listener;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());
    private volatile boolean running = true;

    public UdpDiscovery(Listener listener) {
        super("EcclesUdpDiscovery");
        this.listener = listener;
    }

    public void cancel() {
        running = false;
        interrupt();
    }

    @Override
    public void run() {
        try (DatagramSocket socket = new DatagramSocket(null)) {
            socket.setReuseAddress(true);
            socket.bind(new java.net.InetSocketAddress(DISCOVERY_PORT));
            socket.setBroadcast(true);
            socket.setSoTimeout(2000); // wake periodically to honor cancel()

            byte[] buffer = new byte[256];
            while (running) {
                try {
                    DatagramPacket packet = new DatagramPacket(buffer, buffer.length);
                    socket.receive(packet);
                    String message = new String(packet.getData(), 0, packet.getLength(), StandardCharsets.UTF_8).trim();
                    if (message.startsWith(BROADCAST_PREFIX)) {
                        final String ip = message.substring(BROADCAST_PREFIX.length()).trim();
                        mainHandler.post(() -> listener.onIpDiscovered(ip));
                        return;
                    }
                } catch (SocketTimeoutException timeout) {
                    // expected every 2s so `running` gets rechecked; not an error
                }
            }
        } catch (SocketException e) {
            mainHandler.post(() -> listener.onDiscoveryFailed(
                    "couldn't listen for the bike's broadcast: " + e.getMessage()));
        } catch (Exception e) {
            mainHandler.post(() -> listener.onDiscoveryFailed(e.getMessage()));
        }
    }
}
