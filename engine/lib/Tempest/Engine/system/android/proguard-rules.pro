# Native code resolves these Java callbacks by name through JNI.
-keepclassmembers class org.tempest.TempestNativeActivity {
    public void showSoftInput(java.lang.String);
    public void hideSoftInput();
    public void vibrate(int, float, boolean);
    public float getHdrPeakLuminance();
}

# Keep the JNI symbol names, including the class name used by native exports.
-keepclasseswithmembernames,includedescriptorclasses class org.tempest.TempestNativeActivity {
    native <methods>;
}
