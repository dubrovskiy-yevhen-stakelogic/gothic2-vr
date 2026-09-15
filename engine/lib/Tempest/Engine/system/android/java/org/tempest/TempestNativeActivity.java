package org.tempest;

import android.app.NativeActivity;
import android.content.pm.ActivityInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.hardware.input.InputManager;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.util.Log;
import android.media.AudioAttributes;
import android.view.Display;
import android.view.DisplayCutout;
import android.view.View;
import android.view.WindowInsets;
import android.os.Bundle;
import android.text.Editable;
import android.text.InputType;
import android.text.TextWatcher;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.FrameLayout;

public class TempestNativeActivity extends NativeActivity implements InputManager.InputDeviceListener {
    private volatile float hdrPeakLuminance;
    private int hdrDisplayId = -1;
    private DisplayManager displayManager;
    private final DisplayManager.DisplayListener displayListener = new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int id) { refreshHdrDisplay(); }
        @Override public void onDisplayRemoved(int id) { refreshHdrDisplay(); }
        @Override public void onDisplayChanged(int id) { refreshHdrDisplay(); }
    };
    private native void nativeDisplayChanged();
    private native void nativeCutoutChanged(int left, int top, int right, int bottom, int width, int height);

    private void refreshCutout(View content) {
        int left = 0, top = 0, right = 0, bottom = 0;
        if (Build.VERSION.SDK_INT >= 28) {
            WindowInsets insets = content.getRootWindowInsets();
            DisplayCutout cutout = insets == null ? null : insets.getDisplayCutout();
            if (cutout != null) {
                int[] location = new int[2];
                content.getLocationInWindow(location);
                View decor = getWindow().getDecorView();
                // Convert window insets into content insets, including letterboxed windows.
                left = Math.max(0, cutout.getSafeInsetLeft() - location[0]);
                top = Math.max(0, cutout.getSafeInsetTop() - location[1]);
                right = Math.max(0, cutout.getSafeInsetRight() - (decor.getWidth() - location[0] - content.getWidth()));
                bottom = Math.max(0, cutout.getSafeInsetBottom() - (decor.getHeight() - location[1] - content.getHeight()));
            }
        }
        nativeCutoutChanged(left, top, right, bottom, content.getWidth(), content.getHeight());
    }

    // Called from the render thread; Android display queries stay on the activity thread.
    public float getHdrPeakLuminance() { return hdrPeakLuminance; }

    @SuppressWarnings("deprecation")
    private void refreshHdrDisplay() {
        Display display = getWindowManager().getDefaultDisplay();
        float peak = 0;
        int displayId = display == null ? -1 : display.getDisplayId();
        if (display != null && Build.VERSION.SDK_INT >= 24) {
            Display.HdrCapabilities caps = display.getHdrCapabilities();
            int[] types = Build.VERSION.SDK_INT >= 34
                    ? display.getMode().getSupportedHdrTypes() : caps.getSupportedHdrTypes();
            for (int type : types) {
                if (type == Display.HdrCapabilities.HDR_TYPE_HDR10 || type == Display.HdrCapabilities.HDR_TYPE_HDR10_PLUS) {
                    peak = caps.getDesiredMaxLuminance();
                    // Some displays advertise HDR without a usable peak-luminance estimate.
                    if (!Float.isFinite(peak) || peak <= 0)
                        peak = 1000;
                    peak = Math.max(100, Math.min(peak, 1000));
                    break;
                }
            }
        }
        if (peak != hdrPeakLuminance || displayId != hdrDisplayId) {
            hdrPeakLuminance = peak;
            hdrDisplayId = displayId;
            nativeDisplayChanged();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        resumed = true;
        refreshHdrDisplay();
    }
    private EditText textInput;
    private boolean synchronizingText;
    private InputManager inputManager;
    private int controllerId = -1;
    private int controllerButtons;
    private int controllerHat;
    private final float[] controllerAxes = new float[6];
    private boolean resumed;
    private Vibrator activeVibrator;
    private boolean vibrationFailureReported;

    @SuppressWarnings("deprecation")
    private void reportControllerVibration() {
        InputDevice device = InputDevice.getDevice(controllerId);
        if (!isController(device))
            return;
        vibrationFailureReported = false;
        if (Build.VERSION.SDK_INT >= 31) {
            int[] ids = device.getVibratorManager().getVibratorIds();
            Log.i("TempestHaptics", "Controller " + device.getName() + " (id=" + controllerId
                    + ", vendor=" + device.getVendorId() + ", product=" + device.getProductId()
                    + "): vibrator IDs=" + java.util.Arrays.toString(ids));
        } else {
            Log.i("TempestHaptics", "Controller " + device.getName()
                    + ": hasVibrator=" + device.getVibrator().hasVibrator());
        }
    }

    private void stopVibration() {
        if (activeVibrator != null) {
            try {
                activeVibrator.cancel();
            } catch (SecurityException ignored) {
                // Applications without VIBRATE permission can still use the activity.
            }
            activeVibrator = null;
        }
    }

    // Called through JNI. Device lookup and lifecycle checks belong on the activity thread.
    @SuppressWarnings("deprecation")
    public void vibrate(int milliseconds, float strength, boolean gamepad) {
        final long requestedAt = SystemClock.uptimeMillis();
        runOnUiThread(() -> {
            if (milliseconds <= 0) {
                stopVibration();
                return;
            }
            if (!resumed || !hasWindowFocus() || SystemClock.uptimeMillis() - requestedAt > 150)
                return;
            if (!Float.isFinite(strength) || strength <= 0)
                return;
            if (!gamepad && Settings.System.getInt(getContentResolver(), Settings.System.HAPTIC_FEEDBACK_ENABLED, 1) == 0)
                return;
            try {
                Vibrator vibrator;
                if (gamepad) {
                    InputDevice device = InputDevice.getDevice(controllerId);
                    if (!isController(device))
                        return;
                    vibrator = Build.VERSION.SDK_INT >= 31
                            ? device.getVibratorManager().getDefaultVibrator() : device.getVibrator();
                } else {
                    vibrator = getSystemService(Vibrator.class);
                }
                if (vibrator == null || !vibrator.hasVibrator())
                    return;
                stopVibration();
                activeVibrator = vibrator;
                int duration = Math.min(milliseconds, 1000);
                AudioAttributes attributes = new AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION).build();
                if (Build.VERSION.SDK_INT >= 26) {
                    int amplitude = Math.max(1, Math.min(255, Math.round(strength * 255)));
                    vibrator.vibrate(VibrationEffect.createOneShot(duration, amplitude), attributes);
                } else {
                    vibrator.vibrate(duration, attributes);
                }
            } catch (SecurityException | IllegalArgumentException error) {
                // Missing permission or a disconnected controller must never interrupt the game.
                if (!vibrationFailureReported) {
                    Log.w("TempestHaptics", "Vibration request failed", error);
                    vibrationFailureReported = true;
                }
            }
        });
    }

    private native void nativeSetText(String text);
    private native void nativeEditorAction();
    private native void nativeGamepad(boolean connected, int buttons, float lx, float ly,
                                      float rx, float ry, float lt, float rt);

    @Override
    protected void onCreate(Bundle state) {
        loadNativeLibrary();
        super.onCreate(state);
        View content = findViewById(android.R.id.content);
        content.setOnApplyWindowInsetsListener((view, insets) -> {
            refreshCutout(view);
            return view.onApplyWindowInsets(insets);
        });
        content.addOnLayoutChangeListener((view, left, top, right, bottom, oldLeft, oldTop, oldRight, oldBottom) -> refreshCutout(view));
        content.requestApplyInsets();
        displayManager = getSystemService(DisplayManager.class);
        displayManager.registerDisplayListener(displayListener, null);
        refreshHdrDisplay();
        inputManager = getSystemService(InputManager.class);
        inputManager.registerInputDeviceListener(this, null);
        refreshController();

        textInput = new EditText(this);
        textInput.setSingleLine(true);
        textInput.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_FLAG_CAP_SENTENCES);
        textInput.setImeOptions(EditorInfo.IME_ACTION_DONE | EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        textInput.setBackgroundColor(Color.TRANSPARENT);
        textInput.setTextColor(Color.TRANSPARENT);
        textInput.setCursorVisible(false);
        textInput.setAlpha(0.01f);
        textInput.setPadding(0, 0, 0, 0);
        textInput.setFocusable(false);

        FrameLayout.LayoutParams layout = new FrameLayout.LayoutParams(1, 1, Gravity.BOTTOM | Gravity.LEFT);
        addContentView(textInput, layout);

        textInput.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence text, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence text, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable text) {
                if (!synchronizingText)
                    nativeSetText(text.toString());
            }
        });
        textInput.setOnEditorActionListener((view, actionId, event) -> {
            if (actionId == EditorInfo.IME_ACTION_DONE) {
                nativeEditorAction();
                return true;
            }
            return false;
        });
    }

    private static boolean isController(InputDevice device) {
        return device != null && PanelInputPolicy.isGamepad(device.getVendorId(),
                device.supportsSource(InputDevice.SOURCE_GAMEPAD)
                || device.supportsSource(InputDevice.SOURCE_JOYSTICK));
    }

    private void reportController() {
        nativeGamepad(controllerId != -1, controllerButtons | controllerHat,
                controllerAxes[0], controllerAxes[1], controllerAxes[2], controllerAxes[3],
                controllerAxes[4], controllerAxes[5]);
    }

    private void resetController() {
        controllerButtons = 0;
        controllerHat = 0;
        java.util.Arrays.fill(controllerAxes, 0);
        reportController();
    }

    private void refreshController() {
        if (isController(InputDevice.getDevice(controllerId)))
            return;
        stopVibration();
        controllerId = -1;
        for (int id : InputDevice.getDeviceIds()) {
            if (isController(InputDevice.getDevice(id))) {
                controllerId = id;
                break;
            }
        }
        resetController();
        reportControllerVibration();
        Log.i("TempestInput", "Panel gamepad=" + controllerId
                + "; Quest Touch uses pointer input");
    }

    @Override public void onInputDeviceAdded(int id) { refreshController(); }
    @Override public void onInputDeviceRemoved(int id) { refreshController(); }
    @Override public void onInputDeviceChanged(int id) {
        refreshController();
        if (id == controllerId) reportControllerVibration();
    }

    @Override
    protected void onPause() {
        resumed = false;
        stopVibration();
        resetController();
        super.onPause();
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused) {
            stopVibration();
            resetController();
        }
    }

    @Override
    protected void onDestroy() {
        resumed = false;
        stopVibration();
        displayManager.unregisterDisplayListener(displayListener);
        inputManager.unregisterInputDeviceListener(this);
        controllerId = -1;
        resetController();
        super.onDestroy();
    }

    private static int controllerButton(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_BUTTON_A: return 1 << 0;
            case KeyEvent.KEYCODE_BUTTON_B: return 1 << 1;
            case KeyEvent.KEYCODE_BUTTON_X: return 1 << 2;
            case KeyEvent.KEYCODE_BUTTON_Y: return 1 << 3;
            case KeyEvent.KEYCODE_BUTTON_L1: return 1 << 4;
            case KeyEvent.KEYCODE_BUTTON_R1: return 1 << 5;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return 1 << 6;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return 1 << 7;
            case KeyEvent.KEYCODE_BUTTON_START: return 1 << 8;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return 1 << 9;
            case KeyEvent.KEYCODE_DPAD_UP: return 1 << 10;
            case KeyEvent.KEYCODE_DPAD_DOWN: return 1 << 11;
            case KeyEvent.KEYCODE_DPAD_LEFT: return 1 << 12;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return 1 << 13;
            case KeyEvent.KEYCODE_BUTTON_L2: return 1 << 14;
            case KeyEvent.KEYCODE_BUTTON_R2: return 1 << 15;
            default: return 0;
        }
    }

    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (isController(event.getDevice())) {
            int bit = controllerButton(event.getKeyCode());
            if (bit != 0) {
                if (event.getDeviceId() != controllerId)
                    return true;
                if (event.getAction() == KeyEvent.ACTION_DOWN)
                    controllerButtons |= bit;
                else if (event.getAction() == KeyEvent.ACTION_UP)
                    controllerButtons &= ~bit;
                reportController();
                return true;
            }
        }
        if (textInput != null && textInput.hasFocus()) {
            int key = event.getKeyCode();
            // Keep the Java editor authoritative while the IME is open.
            // NativeActivity must not consume deletion before EditText updates its buffer.
            if (key == KeyEvent.KEYCODE_ENTER) {
                if (event.getAction() == KeyEvent.ACTION_UP)
                    nativeEditorAction();
                return true;
            }
            if (key != KeyEvent.KEYCODE_BACK && key != KeyEvent.KEYCODE_VOLUME_UP
                    && key != KeyEvent.KEYCODE_VOLUME_DOWN && key != KeyEvent.KEYCODE_VOLUME_MUTE)
                return textInput.dispatchKeyEvent(event);
        }
        return super.dispatchKeyEvent(event);
    }

    private static float axis(MotionEvent event, int axis, int fallback) {
        InputDevice device = event.getDevice();
        InputDevice.MotionRange range = device.getMotionRange(axis, event.getSource());
        if (range == null) {
            axis = fallback;
            range = device.getMotionRange(axis, event.getSource());
        }
        if (range == null)
            return 0;
        float value = event.getAxisValue(axis);
        return Math.abs(value) <= range.getFlat() ? 0 : value;
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (event.isFromSource(InputDevice.SOURCE_JOYSTICK) && isController(event.getDevice())) {
            if (event.getDeviceId() != controllerId)
                return true;
            controllerAxes[0] = axis(event, MotionEvent.AXIS_X, MotionEvent.AXIS_X);
            controllerAxes[1] = axis(event, MotionEvent.AXIS_Y, MotionEvent.AXIS_Y);
            controllerAxes[2] = axis(event, MotionEvent.AXIS_Z, MotionEvent.AXIS_RX);
            controllerAxes[3] = axis(event, MotionEvent.AXIS_RZ, MotionEvent.AXIS_RY);
            controllerAxes[4] = axis(event, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE);
            controllerAxes[5] = axis(event, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS);
            float hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X);
            float hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y);
            controllerHat = (hatY < -0.5f ? 1 << 10 : 0) | (hatY > 0.5f ? 1 << 11 : 0)
                    | (hatX < -0.5f ? 1 << 12 : 0) | (hatX > 0.5f ? 1 << 13 : 0);
            reportController();
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    private void loadNativeLibrary() {
        try {
            ActivityInfo info = getPackageManager().getActivityInfo(
                    getComponentName(), PackageManager.GET_META_DATA);
            System.loadLibrary(info.metaData.getString("android.app.lib_name"));
        } catch (PackageManager.NameNotFoundException error) {
            throw new IllegalStateException("Cannot read the NativeActivity library name", error);
        }
    }

    public void showSoftInput(String text) {
        runOnUiThread(() -> {
            synchronizingText = true;
            textInput.setText(text);
            synchronizingText = false;
            textInput.setFocusableInTouchMode(true);
            textInput.setFocusable(true);
            textInput.requestFocus();
            textInput.selectAll();

            InputMethodManager keyboard = getSystemService(InputMethodManager.class);
            keyboard.restartInput(textInput);
            textInput.post(() -> keyboard.showSoftInput(textInput, InputMethodManager.SHOW_IMPLICIT));
        });
    }

    public void hideSoftInput() {
        runOnUiThread(() -> {
            InputMethodManager keyboard = getSystemService(InputMethodManager.class);
            keyboard.hideSoftInputFromWindow(textInput.getWindowToken(), 0);
            textInput.clearFocus();
            textInput.setFocusable(false);
        });
    }
}
