package com.max.waterlens;
import android.app.NativeActivity;
import android.os.Bundle;
import android.content.pm.PackageManager;
import android.widget.Toast;
import java.util.ArrayList;

public class WaterLensActivity extends NativeActivity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        String[] requested = {"horizonos.permission.HEADSET_CAMERA",
            "android.permission.CAMERA", "com.oculus.permission.USE_SCENE"};
        ArrayList<String> missing = new ArrayList<>();
        for (String permission : requested)
            if (checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED)
                missing.add(permission);
        if (!missing.isEmpty()) requestPermissions(missing.toArray(new String[0]), 1);
    }
    public void notifyStatus(final String message) {
        runOnUiThread(new Runnable() {
            @Override public void run() {
                Toast.makeText(WaterLensActivity.this, message, Toast.LENGTH_LONG).show();
            }
        });
    }
}
