/* Goxel 3D voxels editor
 *
 * Goxel is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later
 * version.
 */

package org.goxel.android;

import android.app.NativeActivity;
import android.content.Context;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.InputMethodManager;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/*
 * Thin wrapper around NativeActivity: everything runs in libgoxel.so, this
 * class only provides the services the NDK doesn't have (Storage Access
 * Framework file picker, soft keyboard, unicode of key events).
 */
public class GoxelActivity extends NativeActivity {
    private static final String TAG = "goxel";
    private static final int REQ_OPEN = 1;
    private static final int REQ_SAVE = 2;

    static {
        // Load the library ourselves so that the JNI natives get resolved.
        System.loadLibrary("goxel");
    }

    private static native void nativeDialogResult(String path);

    // Document picked in the last save dialog, and the temporary file goxel
    // writes into before we copy it there.
    private Uri pendingSaveUri;
    private File pendingSaveFile;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemUi();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION |
                View.SYSTEM_UI_FLAG_FULLSCREEN |
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    /* Called from native thread. */
    public void openDialog(final boolean save, final String defaultName,
                           final String exts) {
        runOnUiThread(new Runnable() {
            public void run() {
                Intent intent = new Intent(save ? Intent.ACTION_CREATE_DOCUMENT
                                                : Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                // There is no registered mime type for vox/gox files, so we
                // can't filter here.
                intent.setType(save ? "application/octet-stream" : "*/*");
                if (save) intent.putExtra(Intent.EXTRA_TITLE, defaultName);
                try {
                    startActivityForResult(intent, save ? REQ_SAVE : REQ_OPEN);
                } catch (Exception e) {
                    Log.e(TAG, "Cannot start file picker", e);
                    nativeDialogResult(null);
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request != REQ_OPEN && request != REQ_SAVE) {
            super.onActivityResult(request, result, data);
            return;
        }
        if (result != RESULT_OK || data == null || data.getData() == null) {
            nativeDialogResult(null);
            return;
        }
        Uri uri = data.getData();
        String name = getDisplayName(uri);
        try {
            if (request == REQ_OPEN) {
                File dir = new File(getCacheDir(), "open");
                dir.mkdirs();
                File file = new File(dir, name);
                copy(getContentResolver().openInputStream(uri),
                     new FileOutputStream(file));
                nativeDialogResult(file.getAbsolutePath());
            } else {
                File dir = new File(getCacheDir(), "save");
                dir.mkdirs();
                pendingSaveUri = uri;
                pendingSaveFile = new File(dir, name);
                pendingSaveFile.delete();
                nativeDialogResult(pendingSaveFile.getAbsolutePath());
            }
        } catch (Exception e) {
            Log.e(TAG, "File dialog error", e);
            nativeDialogResult(null);
        }
    }

    /* Called from native thread, once goxel wrote the file to save. */
    public void flushSave() {
        if (pendingSaveUri == null) return;
        try {
            if (pendingSaveFile.exists()) {
                copy(new FileInputStream(pendingSaveFile),
                     getContentResolver().openOutputStream(pendingSaveUri, "wt"));
                pendingSaveFile.delete();
            }
        } catch (Exception e) {
            Log.e(TAG, "Cannot save file", e);
        }
        pendingSaveUri = null;
        pendingSaveFile = null;
    }

    /* Called from native thread. */
    public void showKeyboard(final boolean show) {
        runOnUiThread(new Runnable() {
            public void run() {
                InputMethodManager imm = (InputMethodManager)
                    getSystemService(Context.INPUT_METHOD_SERVICE);
                View view = getWindow().getDecorView();
                if (show)
                    imm.showSoftInput(view, InputMethodManager.SHOW_FORCED);
                else
                    imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
            }
        });
    }

    /* Called from native thread. */
    public int getUnicodeChar(int keyCode, int metaState) {
        return new KeyEvent(KeyEvent.ACTION_DOWN, keyCode)
                .getUnicodeChar(metaState);
    }

    private String getDisplayName(Uri uri) {
        String name = null;
        Cursor cursor = null;
        try {
            cursor = getContentResolver().query(uri,
                    new String[] {OpenableColumns.DISPLAY_NAME},
                    null, null, null);
            if (cursor != null && cursor.moveToFirst())
                name = cursor.getString(0);
        } catch (Exception e) {
            Log.w(TAG, "Cannot get file name", e);
        } finally {
            if (cursor != null) cursor.close();
        }
        if (name == null) name = uri.getLastPathSegment();
        if (name == null) name = "untitled";
        return name.replace('/', '_');
    }

    private static void copy(InputStream in, OutputStream out)
            throws Exception {
        try {
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
        } finally {
            in.close();
            out.close();
        }
    }
}
