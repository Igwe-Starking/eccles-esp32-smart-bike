package starking.eccles.smartbike;

import android.Manifest;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.text.method.ScrollingMovementMethod;
import android.view.KeyEvent;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;
import android.widget.ToggleButton;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.text.SimpleDateFormat;
import java.util.Locale;

/**
 * Single-screen UI mirroring the firmware's own web UI (littlefs_image/index.html + app.js):
 * a device grid, a sensor strip, bluetooth transport controls, conversation controls, and a
 * command terminal - all wired to send text down to CommandBridge and react to whatever
 * MessageThread hands back from the native engine.
 */
public class MainActivity extends AppCompatActivity
        implements MessageThread.Listener, UdpDiscovery.Listener, VoiceRecognitionManager.Listener {

    private static final int REQUEST_RECORD_AUDIO = 1001;

    private TextView statusText;
    private Button hotspotButton;
    private TextView fuelValue;
    private TextView tempValue;
    private TextView conversationStatus;
    private TextView logView;
    private EditText commandInput;

    private ToggleButton btnHeadlamp, btnHorn, btnLeftTurn, btnRightTurn, btnIgnition, btnStarter, btnEngine;
    private Button btnBtPrev, btnBtPlayPause, btnBtNext, btnBtVolDown, btnBtVolUp;
    private Button btnStartAi, btnStartReal, btnCancelConversation;

    private UdpDiscovery udpDiscovery;
    private MessageThread messageThread;
    private VoiceRecognitionManager voiceManager;
    private boolean bluetoothIsPlaying = false;
    private boolean engineStarted = false;

    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm:ss", Locale.US);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);
        bindViews();
        wireDeviceButtons();
        wireBluetoothButtons();
        wireConversationButtons();
        wireTerminal();

        hotspotButton.setOnClickListener(v -> HotspotHelper.promptEnableHotspot(this));

        voiceManager = new VoiceRecognitionManager(this, this);

        beginConnectionFlow();
    }

    private void bindViews() {
        statusText = findViewById(R.id.statusText);
        hotspotButton = findViewById(R.id.hotspotButton);
        fuelValue = findViewById(R.id.fuelValue);
        tempValue = findViewById(R.id.tempValue);
        conversationStatus = findViewById(R.id.conversationStatus);
        logView = findViewById(R.id.logView);
        logView.setMovementMethod(new ScrollingMovementMethod());
        commandInput = findViewById(R.id.commandInput);

        btnHeadlamp = findViewById(R.id.btnHeadlamp);
        btnHorn = findViewById(R.id.btnHorn);
        btnLeftTurn = findViewById(R.id.btnLeftTurn);
        btnRightTurn = findViewById(R.id.btnRightTurn);
        btnIgnition = findViewById(R.id.btnIgnition);
        btnStarter = findViewById(R.id.btnStarter);
        btnEngine = findViewById(R.id.btnEngine);

        btnBtPrev = findViewById(R.id.btnBtPrev);
        btnBtPlayPause = findViewById(R.id.btnBtPlayPause);
        btnBtNext = findViewById(R.id.btnBtNext);
        btnBtVolDown = findViewById(R.id.btnBtVolDown);
        btnBtVolUp = findViewById(R.id.btnBtVolUp);

        btnStartAi = findViewById(R.id.btnStartAi);
        btnStartReal = findViewById(R.id.btnStartReal);
        btnCancelConversation = findViewById(R.id.btnCancelConversation);

        findViewById(R.id.refreshSensorsButton).setOnClickListener(v -> {
            sendPhrase("eccles check " + DeviceId.FUEL_GAUGE.phraseWord());
            sendPhrase("eccles check " + DeviceId.TEMP_GAUGE.phraseWord());
        });

        findViewById(R.id.micButton).setOnClickListener(v -> onMicTapped());
        findViewById(R.id.sendButton).setOnClickListener(v -> onSendTapped());
    }

    // ---- device grid: every toggle just sends "eccles turn on/off <device>" as text --------

    private void wireDeviceButtons() {
        wireToggle(btnHeadlamp, DeviceId.HEADLAMP);
        wireToggle(btnHorn, DeviceId.HORN);
        wireToggle(btnLeftTurn, DeviceId.LEFT_TURN);
        wireToggle(btnRightTurn, DeviceId.RIGHT_TURN);
        wireToggle(btnIgnition, DeviceId.IGNITION);
        wireToggle(btnStarter, DeviceId.STARTER);
        wireToggle(btnEngine, DeviceId.ENGINE);
    }

    private void wireToggle(ToggleButton button, DeviceId device) {
        button.setOnClickListener(v -> {
            boolean turningOn = button.isChecked();
            sendPhrase("eccles turn " + (turningOn ? "on " : "off ") + device.phraseWord());
        });
    }

    // ---- bluetooth transport ---------------------------------------------------------------

    private void wireBluetoothButtons() {
        btnBtPrev.setOnClickListener(v -> sendPhrase("eccles previous bluetooth"));
        btnBtNext.setOnClickListener(v -> sendPhrase("eccles skip bluetooth"));
        btnBtVolDown.setOnClickListener(v -> sendPhrase("eccles volume down bluetooth"));
        btnBtVolUp.setOnClickListener(v -> sendPhrase("eccles volume up bluetooth"));
        btnBtPlayPause.setOnClickListener(v -> {
            bluetoothIsPlaying = !bluetoothIsPlaying;
            btnBtPlayPause.setText(bluetoothIsPlaying ? R.string.pause : R.string.play);
            sendPhrase(bluetoothIsPlaying ? "eccles play bluetooth" : "eccles pause bluetooth");
        });
    }

    // ---- conversation -------------------------------------------------------------------

    private void wireConversationButtons() {
        btnStartAi.setOnClickListener(v -> sendPhrase("eccles start ai conversation"));
        btnStartReal.setOnClickListener(v -> {
            if (ensureRecordAudioPermission()) sendPhrase("eccles start real conversation");
        });
        btnCancelConversation.setOnClickListener(v -> {
            sendPhrase("eccles cancel conversation");
            conversationStatus.setText(R.string.conversation_idle);
        });
    }

    // ---- command terminal -------------------------------------------------------------------

    private void wireTerminal() {
        commandInput.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_SEND ||
                    (event != null && event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                onSendTapped();
                return true;
            }
            return false;
        });
    }

    private void onSendTapped() {
        String text = commandInput.getText().toString().trim();
        if (text.isEmpty()) return;
        commandInput.setText("");
        // convenience: the web UI's terminal lets you type without the wake word, same here
        if (!text.toLowerCase(Locale.US).startsWith("eccles")) {
            text = "eccles " + text;
        }
        sendPhrase(text);
    }

    private void onMicTapped() {
        if (!ensureRecordAudioPermission()) return;
        appendLog("listening…");
        voiceManager.startListening();
    }

    private void sendPhrase(String phrase) {
        appendLog("> " + phrase);
        CommandBridge.send(CommandType.TEXT_BINARY, phrase);
    }

    private boolean ensureRecordAudioPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO)
                == PackageManager.PERMISSION_GRANTED) {
            return true;
        }
        ActivityCompat.requestPermissions(this, new String[]{Manifest.permission.RECORD_AUDIO}, REQUEST_RECORD_AUDIO);
        return false;
    }

    private void appendLog(String line) {
        logView.append("[" + timeFormat.format(new java.util.Date()) + "] " + line + "\n");
    }

    // ---- connection lifecycle: hotspot -> UDP discovery -> native engine ------------------

    private void beginConnectionFlow() {
        if (!HotspotHelper.isHotspotLikelyEnabled(this)) {
            statusText.setText(R.string.status_waiting_hotspot);
            HotspotHelper.promptEnableHotspot(this);
        }
        statusText.setText(R.string.status_waiting_bike);
        appendLog("listening for the bike's broadcast on UDP 4210…");
        udpDiscovery = new UdpDiscovery(this);
        udpDiscovery.start();
    }

    @Override
    public void onIpDiscovered(String ip) {
        statusText.setText(getString(R.string.status_connecting, ip));
        appendLog("bike found at " + ip + ", starting native engine…");
        CommandBridge.initEngine(ip);

        messageThread = new MessageThread(this);
        messageThread.start();
    }

    @Override
    public void onDiscoveryFailed(String reason) {
        appendLog("discovery failed: " + reason);
        Toast.makeText(this, reason, Toast.LENGTH_LONG).show();
    }

    // ---- MessageThread.Listener: everything the native engine reports back ------------------

    @Override
    public void onNativeMessage(NativeMessage message) {
        switch (message.type) {
            case MessageType.CONNECTED:
                statusText.setText(getString(R.string.status_connected, "the bike"));
                appendLog("connected");
                if (!engineStarted) {
                    engineStarted = true;
                    voiceManager.setContinuousMode(false);
                }
                break;

            case MessageType.DISCONNECTED:
                statusText.setText(R.string.status_disconnected);
                appendLog("disconnected");
                break;

            case MessageType.DEVICE_STATE:
                applyDeviceState(DeviceId.fromNative(message.device), message.value != 0);
                break;

            case MessageType.DEVICE_VALUE:
                applyDeviceValue(DeviceId.fromNative(message.device), message.value);
                break;

            case MessageType.CONVERSATION_AUDIO_CONFIG:
                conversationStatus.setText(R.string.conversation_active);
                appendLog("conversation audio: " + message.extra1 + "Hz, " + message.extra2
                        + "-bit, " + message.extra3 + "ch");
                break;

            case MessageType.ERROR:
                appendLog("error: " + message.text);
                break;

            case MessageType.LOG:
            default:
                appendLog(message.text);
                break;
        }
    }

    private void applyDeviceState(DeviceId device, boolean on) {
        ToggleButton button = toggleFor(device);
        if (button != null) button.setChecked(on);
    }

    private void applyDeviceValue(DeviceId device, int value) {
        if (device == DeviceId.FUEL_GAUGE) {
            fuelValue.setText(getString(R.string.fuel_placeholder).replace("--", value + "%"));
        } else if (device == DeviceId.TEMP_GAUGE) {
            tempValue.setText(getString(R.string.temp_placeholder).replace("--", value + "°"));
        }
    }

    private ToggleButton toggleFor(DeviceId device) {
        switch (device) {
            case HEADLAMP: return btnHeadlamp;
            case HORN: return btnHorn;
            case LEFT_TURN: return btnLeftTurn;
            case RIGHT_TURN: return btnRightTurn;
            case IGNITION: return btnIgnition;
            case STARTER: return btnStarter;
            case ENGINE: return btnEngine;
            default: return null;
        }
    }

    // ---- VoiceRecognitionManager.Listener ---------------------------------------------------

    @Override
    public void onCommandHeard(String fullPhrase) {
        sendPhrase(fullPhrase);
    }

    @Override
    public void onRejectedAsNoise(String heardText) {
        appendLog("(ignored, no wake word): " + heardText);
    }

    @Override
    public void onListeningStateChanged(boolean listening) {
        // could drive a mic icon animation; kept minimal here
    }

    @Override
    public void onError(String message) {
        appendLog("voice error: " + message);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (udpDiscovery != null) udpDiscovery.cancel();
        if (messageThread != null) messageThread.stopPolling();
        if (voiceManager != null) voiceManager.destroy();
        CommandBridge.stopEngine();
    }
}
