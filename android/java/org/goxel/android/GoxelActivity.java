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
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Parcelable;
import android.os.Process;
import android.provider.OpenableColumns;
import android.util.Log;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/*
 * Thin wrapper around NativeActivity: everything runs in libgoxel.so, this
 * class only provides the services the NDK doesn't have (Storage Access
 * Framework file picker, soft keyboard, unicode of key events).
 *
 * Documents: goxel works with plain file paths, but on android the user
 * picks documents (content URIs).  Each picked document gets a persistent
 * local copy (files/docs/<hash>/<name>) that goxel reads and writes, and we
 * remember its URI (with a persisted permission).  Whenever goxel writes
 * the local copy (save, export...), syncDocuments copies it back to the
 * document.  Since the local paths are stable, goxel recent files work.
 */
public class GoxelActivity extends NativeActivity {
    private static final String TAG = "goxel";
    private static final int REQ_OPEN = 1;
    private static final int REQ_SAVE = 2;

    static {
        // Load the library ourselves so that the JNI natives get resolved.
        System.loadLibrary("goxel");
    }

    // Formats goxel can open (see src/formats), must match the manifest.
    private static final List<String> OPEN_EXTS = Arrays.asList(
            "gox", "vox", "qb", "qbcl", "vxl", "kv6", "kvx", "mts", "obj",
            "txt");

    private static native void nativeDialogResult(String path);
    private static native void nativeOpenFile(String path, boolean imp);

    // Local copy path -> document URI, persisted in the preferences.
    private SharedPreferences docs;
    // Local copy path -> "mtime:size" of the local copy when last synced.
    private final Map<String, String> synced = new HashMap<>();

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemUi();
        docs = getSharedPreferences("documents", MODE_PRIVATE);
        // Assume the local copies are in sync with the documents.
        for (String path : docs.getAll().keySet())
            synced.put(path, fileState(new File(path)));
        // Not when recreated: the file was already opened.
        if (savedInstanceState == null) handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleIntent(intent);
    }

    /*
     * "Open with" / "Share" from another app.  If we can write the document
     * it becomes a normal document (save writes it back), otherwise we open
     * a copy without a save path.
     */
    private void handleIntent(Intent intent) {
        Uri uri = null;
        if (intent == null) return;
        if (Intent.ACTION_VIEW.equals(intent.getAction())) {
            uri = intent.getData();
        } else if (Intent.ACTION_SEND.equals(intent.getAction())) {
            Parcelable p = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (p instanceof Uri) uri = (Uri)p;
        }
        if (uri == null) return;
        final Uri fUri = uri;
        final boolean writable = Intent.ACTION_VIEW.equals(intent.getAction())
            && (intent.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION) != 0
            && checkUriPermission(uri, Process.myPid(), Process.myUid(),
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION) ==
                    PackageManager.PERMISSION_GRANTED;
        new Thread(new Runnable() {
            public void run() { openExternal(fUri, writable); }
        }).start();
    }

    private void openExternal(Uri uri, boolean writable) {
        String name = getDisplayName(uri);
        int dot = name.lastIndexOf('.');
        String ext = dot < 0 ? "" :
            name.substring(dot + 1).toLowerCase(Locale.ROOT);
        if (!OPEN_EXTS.contains(ext)) {
            toast("Goxel cannot open " + name);
            return;
        }
        // goxel matches the extensions in lower case.
        name = name.substring(0, dot + 1) + ext;
        if (writable) takePermission(uri);
        File dir;
        if (writable) {
            dir = localFile(uri).getParentFile();
        } else {
            dir = new File(getCacheDir(), "opened");
            dir.mkdirs();
        }
        File file = new File(dir, name);
        try {
            copy(getContentResolver().openInputStream(uri),
                 new FileOutputStream(file));
        } catch (Exception e) {
            Log.e(TAG, "Cannot read " + uri, e);
            toast("Cannot read " + name);
            return;
        }
        if (writable) setDocument(file, uri);
        nativeOpenFile(file.getAbsolutePath(), !writable);
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
                intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION |
                                Intent.FLAG_GRANT_WRITE_URI_PERMISSION |
                                Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
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
        takePermission(uri);
        File file = localFile(uri);
        try {
            if (request == REQ_OPEN) {
                copy(getContentResolver().openInputStream(uri),
                     new FileOutputStream(file));
            } else {
                // goxel will write it, then syncDocuments copies it.
                file.delete();
            }
            setDocument(file, uri);
            nativeDialogResult(file.getAbsolutePath());
        } catch (Exception e) {
            Log.e(TAG, "File dialog error", e);
            nativeDialogResult(null);
        }
    }

    private void takePermission(Uri uri) {
        int flags = Intent.FLAG_GRANT_READ_URI_PERMISSION |
                    Intent.FLAG_GRANT_WRITE_URI_PERMISSION;
        try {
            getContentResolver().takePersistableUriPermission(uri, flags);
        } catch (Exception e) {
            // Some providers only give read access.
            try {
                getContentResolver().takePersistableUriPermission(
                        uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (Exception e2) {
                Log.w(TAG, "No persistable permission for " + uri);
            }
        }
    }

    // Stable local copy path for a document.
    private File localFile(Uri uri) {
        File dir = new File(new File(getFilesDir(), "docs"),
                            Integer.toHexString(uri.toString().hashCode()));
        dir.mkdirs();
        return new File(dir, getDisplayName(uri));
    }

    private void setDocument(File file, Uri uri) {
        String path = file.getAbsolutePath();
        docs.edit().putString(path, uri.toString()).apply();
        synchronized (synced) {
            synced.put(path, fileState(file));
        }
    }

    private static String fileState(File file) {
        if (!file.exists()) return "";
        return file.lastModified() + ":" + file.length();
    }

    /*
     * Called from native thread, every frame: copy the local copies that
     * changed since the last sync back to their documents.
     */
    public void syncDocuments() {
        synchronized (synced) {
            for (Map.Entry<String, String> e : synced.entrySet()) {
                File file = new File(e.getKey());
                String state = fileState(file);
                if (state.isEmpty() || state.equals(e.getValue())) continue;
                e.setValue(state);
                String uri = docs.getString(e.getKey(), null);
                if (uri == null) continue;
                try {
                    copy(new FileInputStream(file),
                         getContentResolver().openOutputStream(
                                 Uri.parse(uri), "wt"));
                    Log.i(TAG, "Synced " + file + " to " + uri);
                } catch (Exception ex) {
                    Log.e(TAG, "Cannot write " + uri, ex);
                    toast("Cannot write " + file.getName() + ": " +
                          ex.getMessage());
                }
            }
        }
    }

    /*
     * Called from native thread before opening a file (e.g. from the
     * recent files): refresh the local copy from its document, in case it
     * was modified by another app.
     */
    public void refreshDocument(String path) {
        String uri = docs.getString(path, null);
        if (uri == null) return;
        File file = new File(path);
        synchronized (synced) {
            // Never overwrite changes we could not write to the document.
            String state = synced.get(path);
            if (state != null && !state.equals(fileState(file))) return;
        }
        try {
            copy(getContentResolver().openInputStream(Uri.parse(uri)),
                 new FileOutputStream(file));
            synchronized (synced) {
                synced.put(path, fileState(file));
            }
        } catch (Exception e) {
            // Use the local copy.
            Log.w(TAG, "Cannot read " + uri + ", using local copy", e);
        }
    }

    private void toast(final String msg) {
        runOnUiThread(new Runnable() {
            public void run() {
                Toast.makeText(GoxelActivity.this, msg,
                               Toast.LENGTH_LONG).show();
            }
        });
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
