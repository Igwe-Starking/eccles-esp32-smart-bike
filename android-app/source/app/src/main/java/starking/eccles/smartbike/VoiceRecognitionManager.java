package starking.eccles.smartbike;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.speech.RecognitionListener;
import android.speech.RecognizerIntent;
import android.speech.SpeechRecognizer;

import java.util.ArrayList;
import java.util.Locale;

/**
 * Thin wrapper around SpeechRecognizer. Per the integration spec: speech input must start
 * with the "Eccles" wake word or it's rejected as noise before it ever reaches the native
 * CommandFactory (typed/button text skips this check - it's only ambient/misheard speech
 * this guards against).
 */
public class VoiceRecognitionManager {

    public interface Listener {
        void onCommandHeard(String fullPhrase);
        void onRejectedAsNoise(String heardText);
        void onListeningStateChanged(boolean listening);
        void onError(String message);
    }

    private final SpeechRecognizer recognizer;
    private final Listener listener;
    private boolean continuousMode = false;

    public VoiceRecognitionManager(Context context, Listener listener) {
        this.listener = listener;
        this.recognizer = SpeechRecognizer.createSpeechRecognizer(context);
        this.recognizer.setRecognitionListener(new RecognitionListener() {
            @Override public void onReadyForSpeech(Bundle params) { listener.onListeningStateChanged(true); }
            @Override public void onBeginningOfSpeech() {}
            @Override public void onRmsChanged(float rmsdB) {}
            @Override public void onBufferReceived(byte[] buffer) {}
            @Override public void onEndOfSpeech() { listener.onListeningStateChanged(false); }

            @Override
            public void onError(int error) {
                listener.onListeningStateChanged(false);
                listener.onError("speech recognizer error code " + error);
                if (continuousMode) startListening();
            }

            @Override
            public void onResults(Bundle results) {
                ArrayList<String> matches = results.getStringArrayList(SpeechRecognizer.RESULTS_RECOGNITION);
                handleHeard(matches);
                if (continuousMode) startListening();
            }

            @Override public void onPartialResults(Bundle partialResults) {}
            @Override public void onEvent(int eventType, Bundle params) {}
        });
    }

    private void handleHeard(ArrayList<String> matches) {
        if (matches == null || matches.isEmpty()) return;
        String heard = matches.get(0).trim();
        String normalized = heard.toLowerCase(Locale.US);
        // must start with the wake word or it's rejected as noise, per the integration spec -
        // CommandFactory::stripWakeWord() enforces this again natively as a second line of
        // defense, but rejecting here avoids spending a native round trip on background chatter
        if (normalized.startsWith("eccles")) {
            listener.onCommandHeard(heard);
        } else {
            listener.onRejectedAsNoise(heard);
        }
    }

    public void setContinuousMode(boolean continuous) {
        this.continuousMode = continuous;
    }

    public void startListening() {
        Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, Locale.US);
        intent.putExtra(RecognizerIntent.EXTRA_PARTIAL_RESULTS, false);
        recognizer.startListening(intent);
    }

    public void stopListening() {
        continuousMode = false;
        recognizer.stopListening();
    }

    public void destroy() {
        continuousMode = false;
        recognizer.destroy();
    }
}
