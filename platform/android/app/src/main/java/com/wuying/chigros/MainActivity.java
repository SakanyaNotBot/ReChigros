package com.wuying.chigros;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.webkit.MimeTypeMap;
import android.widget.Toast;

import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class MainActivity extends SDLActivity {
    private static final String TAG = "ReChigrosImport";
    private static final long MAX_INPUT_BYTES = 512L * 1024L * 1024L;
    private static final long MAX_UNPACKED_BYTES = 1024L * 1024L * 1024L;
    private static final int MAX_ARCHIVE_ENTRIES = 4096;

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL3", "main" };
    }

    // The interface is authored on a wide canvas, so the window stays in landscape on every
    // device and no other orientation is ever accepted. While creating its surface the
    // toolkit derives a requested orientation from the window size and the registered
    // orientation hint, which would otherwise hand the choice back to the system and let a
    // portrait device open in portrait; answering the request here keeps the single allowed
    // orientation regardless of the device or the system-wide rotation lock.
    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        setRequestedOrientation(android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    // Automation and device bring-up runs need the same flags the desktop build takes
    // (a frame limit, a frame capture path, a GL diagnostic switch), so the launcher may
    // hand them over as a single space-separated intent extra. A normal launch carries no
    // extra and the native side sees no arguments.
    @Override
    protected String[] getArguments() {
        final Intent intent = getIntent();
        final String arguments = intent != null ? intent.getStringExtra("args") : null;
        if (arguments == null || arguments.isBlank()) return new String[0];
        return arguments.trim().split("\\s+");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The activity has to be attached before a document can be resolved, so the incoming
        // chart is prepared after the base class has run, not before.
        super.onCreate(savedInstanceState);
        final Intent intent = getIntent();
        if (intent == null || intent.getData() == null) return;
        if (!Intent.ACTION_VIEW.equals(intent.getAction())) return;
        new Thread(() -> {
            try {
                final String path = prepareIncomingIntent(intent);
                if (path != null) runOnUiThread(() -> SDLActivity.onNativeDropFile(path));
            } catch (IOException | RuntimeException error) {
                // A content URI whose read grant is missing (or already revoked) throws
                // SecurityException, which is a RuntimeException. Catching only IOException
                // let it escape this worker thread and kill the whole process on import.
                Log.e(TAG, "Unable to prepare incoming chart", error);
                runOnUiThread(() -> Toast.makeText(this,
                        "Cannot import chart: " + error.getMessage(), Toast.LENGTH_LONG).show());
            }
        }).start();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        if (intent == null || intent.getData() == null) return;
        new Thread(() -> {
            try {
                String path = prepareIncomingIntent(intent);
                if (path != null) runOnUiThread(() -> SDLActivity.onNativeDropFile(path));
            } catch (IOException | RuntimeException error) {
                Log.e(TAG, "Unable to import incoming chart", error);
                runOnUiThread(() -> Toast.makeText(this,
                        "Cannot import chart: " + error.getMessage(), Toast.LENGTH_LONG).show());
            }
        }, "ReChigrosImport").start();
    }

    private String prepareIncomingIntent(Intent intent) throws IOException {
        if (!Intent.ACTION_VIEW.equals(intent.getAction())) return null;
        Uri uri = intent.getData();
        if (uri == null) return null;
        File incoming;
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            incoming = new File(uri.getPath());
        } else if ("content".equalsIgnoreCase(uri.getScheme())) {
            try {
                getContentResolver().takePersistableUriPermission(uri,
                        intent.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION);
            } catch (RuntimeException ignored) {
            }
            incoming = copyContentUri(uri);
        } else {
            throw new IOException("Unsupported URI scheme: " + uri.getScheme());
        }
        return tagIncomingFile(incoming);
    }

    // A picked document is handed to the native side with a short kind tag so it is imported
    // into the right store: chart packs go into the chart library, replay archives into the
    // replay store and resource packages into the skin store. A loose chart document is
    // passed untagged, which starts it as a temporary chart instead of installing a pack.
    private static String tagIncomingFile(File incoming) throws IOException {
        final String lower = incoming.getName().toLowerCase(Locale.ROOT);
        if (lower.endsWith(".cgrp")) return "replay|" + incoming.getAbsolutePath();
        if (!isZip(incoming)) return incoming.getAbsolutePath();
        if (zipContainsAny(incoming, "replay.json")) return "replay|" + incoming.getAbsolutePath();
        if (zipContainsAny(incoming, "respack.json", "skin.json")) {
            return "skin|" + incoming.getAbsolutePath();
        }
        return "chartzip|" + incoming.getAbsolutePath();
    }

    private static boolean zipContainsAny(File archive, String... names) {
        try (ZipInputStream zip = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(archive)))) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                final String entryName = entry.getName().toLowerCase(Locale.ROOT);
                for (String name : names) {
                    if (entryName.equals(name) || entryName.endsWith("/" + name)) return true;
                }
            }
        } catch (IOException ignored) {
        }
        return false;
    }

    private File copyContentUri(Uri uri) throws IOException {
        File root = new File(getCacheDir(), "incoming");
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create import cache");
        String displayName = queryDisplayName(uri);
        if (displayName == null || displayName.isEmpty()) {
            String extension = MimeTypeMap.getSingleton().getExtensionFromMimeType(
                    getContentResolver().getType(uri));
            displayName = "chart" + (extension == null ? ".bin" : "." + extension);
        }
        displayName = sanitizeFileName(displayName);
        File output = new File(root, System.currentTimeMillis() + "_" + displayName);
        long total = 0;
        InputStream source = getContentResolver().openInputStream(uri);
        if (source == null) throw new IOException("Content provider returned no stream");
        try (InputStream input = new BufferedInputStream(source);
             FileOutputStream fileOutput = new FileOutputStream(output);
             BufferedOutputStream bufferedOutput = new BufferedOutputStream(fileOutput)) {
            byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) != -1) {
                total += count;
                if (total > MAX_INPUT_BYTES) throw new IOException("Incoming file exceeds 512 MB");
                bufferedOutput.write(buffer, 0, count);
            }
        } catch (IOException error) {
            output.delete();
            throw error;
        }
        return output;
    }

    private String queryDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) return cursor.getString(index);
            }
        } catch (RuntimeException ignored) {
        }
        return null;
    }

    private static String sanitizeFileName(String name) {
        String safe = name.replaceAll("[\\\\/:*?\"<>|]", "_");
        if (safe.length() > 160) safe = safe.substring(safe.length() - 160);
        return safe.isEmpty() ? "chart.bin" : safe;
    }

    private static boolean isZip(File file) throws IOException {
        String lower = file.getName().toLowerCase(Locale.ROOT);
        if (lower.endsWith(".zip")) return true;
        try (FileInputStream input = new FileInputStream(file)) {
            return input.read() == 0x50 && input.read() == 0x4b;
        }
    }

    private File extractChartArchive(File archive) throws IOException {
        File root = new File(new File(getCacheDir(), "incoming"),
                "unpacked_" + System.currentTimeMillis());
        if (!root.mkdirs()) throw new IOException("Cannot create archive cache");
        String rootPath = root.getCanonicalPath() + File.separator;
        long total = 0;
        int entries = 0;
        try (ZipInputStream zip = new ZipInputStream(
                new BufferedInputStream(new FileInputStream(archive)))) {
            ZipEntry entry;
            byte[] buffer = new byte[64 * 1024];
            while ((entry = zip.getNextEntry()) != null) {
                if (++entries > MAX_ARCHIVE_ENTRIES) throw new IOException("Archive has too many files");
                File output = new File(root, entry.getName());
                String outputPath = output.getCanonicalPath();
                if (!outputPath.startsWith(rootPath)) throw new IOException("Unsafe archive path");
                if (entry.isDirectory()) {
                    if (!output.isDirectory() && !output.mkdirs()) throw new IOException("Cannot create archive directory");
                    continue;
                }
                File parent = output.getParentFile();
                if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
                    throw new IOException("Cannot create archive directory");
                }
                try (BufferedOutputStream target = new BufferedOutputStream(new FileOutputStream(output))) {
                    int count;
                    while ((count = zip.read(buffer)) != -1) {
                        total += count;
                        if (total > MAX_UNPACKED_BYTES) throw new IOException("Archive expands beyond 1 GB");
                        target.write(buffer, 0, count);
                    }
                }
            }
            File chart = findChart(root);
            if (chart == null) throw new IOException("Archive contains no supported Official or PEC chart");
            return chart;
        } catch (IOException error) {
            deleteRecursively(root);
            throw error;
        }
    }

    private static File findChart(File root) {
        List<File> candidates = new ArrayList<>();
        collectChartCandidates(root, candidates);
        candidates.sort(Comparator.comparingInt((File file) -> file.getAbsolutePath().length())
                .thenComparing(File::getAbsolutePath));
        return candidates.isEmpty() ? null : candidates.get(0);
    }

    private static void collectChartCandidates(File current, List<File> candidates) {
        File[] files = current.listFiles();
        if (files == null) return;
        for (File file : files) {
            if (file.isDirectory()) {
                collectChartCandidates(file, candidates);
            } else if (isChartCandidate(file)) {
                candidates.add(file);
            }
        }
    }

    private static boolean isChartCandidate(File file) {
        String lower = file.getName().toLowerCase(Locale.ROOT);
        if (lower.endsWith(".pec")) return true;
        if (!lower.endsWith(".json") || lower.equals("info.json")
                || lower.equals("extra.json") || lower.equals("respack.json")
                || lower.equals("replay.json")) return false;
        String head = readHead(file, 64 * 1024);
        if (head == null) return false;
        return head.contains("\"formatVersion\"") || head.contains("\"judgeLineList\"")
                || head.contains("\"RPEVersion\"") || head.contains("\"BPMList\"");
    }

    private static String readHead(File file, int maximum) {
        byte[] buffer = new byte[maximum];
        try (FileInputStream input = new FileInputStream(file)) {
            int count = input.read(buffer);
            return count <= 0 ? null : new String(buffer, 0, count, StandardCharsets.UTF_8);
        } catch (IOException ignored) {
            return null;
        }
    }

    // --- platform file and folder picker -------------------------------------
    // The picker is requested from the native side. Its answer is delivered through the same
    // drop-file path an incoming document uses, so the rest of the application needs no
    // second way to receive a chosen path.

    private static final int PICKER_FILE = 0;
    private static final int PICKER_FOLDER = 1;
    private static final int PICKER_REPLAY = 2;
    private static final int PICKER_SKIN = 3;
    private static final int REQUEST_PICKER = 0x51D1;
    private static final int REQUEST_SHARED_STORAGE = 0x51D2;

    private static MainActivity instance;
    // Which picker the pending request asked for, so its answer can be read correctly.
    private static int pendingPickerMode = PICKER_FILE;

    // --- recorder bridge -------------------------------------------------------------
    // The native recorder addresses these by name on the activity class, so one lookup finds them
    // regardless of which object owns the encoder state. They forward straight to the helper.

    public static boolean startVideoEncoder(String path, int width, int height, int fps,
            int quality) {
        return VideoEncoderBridge.start(path, width, height, fps, quality);
    }

    public static boolean setVideoAudioTrack(String path, int sampleRate, int channels,
            int delayMs) {
        return VideoEncoderBridge.setAudioTrack(path, sampleRate, delayMs);
    }

    /**
     * Absolute path of one of the system's shared media folders, or null when it is unavailable.
     *
     * <p>A packaged application cannot assume where shared storage lives, and the directory it
     * names differs per release, so the platform is asked for it here. The folder's own
     * subdirectory for this application is created by the caller.
     */
    public static String sharedStorageDirectory(String category) {
        try {
            String kind = category != null ? category : "Movies";
            java.io.File dir = android.os.Environment.getExternalStoragePublicDirectory(kind);
            if (dir == null) return null;
            if (!dir.exists() && !dir.mkdirs() && !dir.exists()) {
                Log.e(TAG, "cannot create public directory " + dir);
                return null;
            }
            // The folder being reported is not proof it can be written: on the releases that need
            // the legacy grant it exists but refuses every write. Probing it here is what tells
            // the caller to use its private folder instead of failing a long recording at the end.
            final File probe = new File(dir, ".rechigros-write-probe");
            try {
                if (!probe.createNewFile() && !probe.exists()) {
                    Log.w(TAG, "public directory is not writable: " + dir);
                    return null;
                }
            } catch (IOException | SecurityException error) {
                Log.w(TAG, "public directory is not writable: " + dir, error);
                return null;
            } finally {
                //noinspection ResultOfMethodCallIgnored
                probe.delete();
            }
            return dir.getAbsolutePath();
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot resolve public directory " + category, error);
            return null;
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode != REQUEST_SHARED_STORAGE) return;
        final boolean granted = results.length > 0
                && results[0] == android.content.pm.PackageManager.PERMISSION_GRANTED;
        Log.i(TAG, "shared storage granted=" + granted);
    }

    /**
     * Copies a finished file into the shared media library and returns where it landed.
     *
     * <p>A recent release does not let an application create a file in the shared library by path:
     * the folder exists and is readable, but every write is refused. The platform media service is
     * the supported way in, so the finished file is handed to it here and the private copy removed.
     * The returned path is the one the player sees, or null when the file could not be published and
     * the caller should keep its own copy.
     */
    public static String publishMediaFile(String sourcePath, String category) {
        if (sourcePath == null) return null;
        // The resolver belongs to the activity, and the caller arrives from the native side with no
        // reference to it; the instance kept by onResume is the one that owns the application
        // context this needs.
        final MainActivity activity = instance;
        if (activity == null) return null;
        final File source = new File(sourcePath);
        if (!source.isFile() || source.length() == 0) return null;
        final boolean movies = !"Download".equals(category);
        final String collection = movies
                ? android.provider.MediaStore.Video.Media.EXTERNAL_CONTENT_URI.toString()
                : android.provider.MediaStore.Downloads.EXTERNAL_CONTENT_URI.toString();
        final String relativePath = (movies ? "Movies" : "Download") + "/Chigros";
        final String mime = movies ? "video/mp4" : "application/octet-stream";
        android.net.Uri uri = null;
        try {
            final android.content.ContentValues values = new android.content.ContentValues();
            values.put(android.provider.MediaStore.MediaColumns.DISPLAY_NAME, source.getName());
            values.put(android.provider.MediaStore.MediaColumns.MIME_TYPE, mime);
            values.put(android.provider.MediaStore.MediaColumns.RELATIVE_PATH, relativePath);
            // The entry stays hidden from other applications until the bytes are all in place, so
            // a viewer never sees a half written recording.
            values.put(android.provider.MediaStore.MediaColumns.IS_PENDING, 1);
            uri = activity.getContentResolver().insert(android.net.Uri.parse(collection), values);
            if (uri == null) {
                Log.e(TAG, "media service refused the entry for " + source);
                return null;
            }
            try (java.io.InputStream in = new java.io.FileInputStream(source);
                    java.io.OutputStream out = activity.getContentResolver().openOutputStream(uri)) {
                if (out == null) throw new IOException("no output stream for " + uri);
                final byte[] buffer = new byte[1 << 16];
                int read;
                while ((read = in.read(buffer)) > 0) out.write(buffer, 0, read);
            }
            values.clear();
            values.put(android.provider.MediaStore.MediaColumns.IS_PENDING, 0);
            activity.getContentResolver().update(uri, values, null, null);
            //noinspection ResultOfMethodCallIgnored
            source.delete();
            Log.i(TAG, "published " + source.getName() + " to " + relativePath);
            return relativePath + "/" + source.getName();
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "cannot publish " + source, error);
            if (uri != null) {
                try {
                    activity.getContentResolver().delete(uri, null, null);
                } catch (RuntimeException ignored) {
                }
            }
            return null;
        }
    }

    /**
     * Asks for the legacy shared-storage grant when the release still requires it. Reporting
     * whether it is already held lets the caller decide before it writes anything.
     */
    public static boolean ensureSharedStoragePermission() {
        if (android.os.Build.VERSION.SDK_INT > 28) return true;
        // Permission checks live on the activity, and the caller arrives from the native side with
        // no reference to it; the instance kept by onResume is the one the system will answer for.
        final MainActivity activity = instance;
        if (activity == null) return false;
        try {
            if (activity.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    == android.content.pm.PackageManager.PERMISSION_GRANTED) {
                return true;
            }
            activity.requestPermissions(
                    new String[] { android.Manifest.permission.WRITE_EXTERNAL_STORAGE },
                    REQUEST_SHARED_STORAGE);
            return false;
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot request shared storage", error);
            return false;
        }
    }

    /** Progress of the music encode: 0 none, 1 preparing, 2 ready, 3 failed. */
    public static int videoAudioTrackState() {
        return VideoEncoderBridge.audioTrackState();
    }

    public static int videoAudioTrackProgressPermille() {
        return VideoEncoderBridge.audioTrackProgressPermille();
    }

    public static boolean queueVideoFrame(java.nio.ByteBuffer planes, int width, int height) {
        return VideoEncoderBridge.queueFrame(planes, width, height);
    }

    public static boolean finishVideoEncoder() {
        return VideoEncoderBridge.finish();
    }

    public static void abortVideoEncoder() {
        VideoEncoderBridge.abort();
    }

    @Override
    protected void onResume() {
        super.onResume();
        instance = this;
    }

    @Override
    protected void onDestroy() {
        if (instance == this) instance = null;
        super.onDestroy();
    }

    // Called from the native side when the import button is pressed.
    public static void requestPicker(final int mode) {
        final MainActivity activity = instance;
        if (activity == null) return;
        activity.runOnUiThread(() -> {
            try {
                final android.content.Intent intent;
                if (mode == PICKER_FOLDER) {
                    intent = new android.content.Intent(
                            android.content.Intent.ACTION_OPEN_DOCUMENT_TREE);
                } else {
                    intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
                    intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
                    intent.setType("*/*");
                }
                intent.addFlags(android.content.Intent.FLAG_GRANT_READ_URI_PERMISSION);
                pendingPickerMode = mode;
                activity.startActivityForResult(intent, REQUEST_PICKER);
            } catch (RuntimeException error) {
                Log.e(TAG, "Unable to open the document picker", error);
            }
        });
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQUEST_PICKER) return;
        final int pickerMode = pendingPickerMode;
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        // Reading the document touches the disk, so it runs off the UI thread.
        new Thread(() -> {
            try {
                final String path;
                if (pickerMode == PICKER_FOLDER) {
                    path = "chartdir|" + copyDocumentTree(uri);
                } else if (pickerMode == PICKER_REPLAY) {
                    path = "replay|" + copyContentUri(uri).getAbsolutePath();
                } else if (pickerMode == PICKER_SKIN) {
                    path = "skin|" + copyContentUri(uri).getAbsolutePath();
                } else {
                    path = tagIncomingFile(copyContentUri(uri));
                }
                if (path != null) SDLActivity.onNativeDropFile(path);
            } catch (IOException error) {
                Log.e(TAG, "Unable to read the chosen document", error);
            }
        }).start();
    }

    // Copies a whole document tree into the import cache and returns the root it wrote.
    // The system grants read access to the chosen tree only, so every file has to be pulled
    // through the resolver rather than opened from a path.
    private String copyDocumentTree(Uri treeUri) throws IOException {
        File root = new File(getCacheDir(), "incoming");
        if (!root.isDirectory() && !root.mkdirs()) throw new IOException("Cannot create import cache");
        File output = new File(root, System.currentTimeMillis() + "_folder");
        if (!output.mkdirs()) throw new IOException("Cannot create the import folder");
        long[] budget = new long[]{MAX_UNPACKED_BYTES};
        final int copied = copyTreeRecursive(treeUri, output, budget, 0);
        if (copied <= 0) throw new IOException("The chosen folder holds no readable files");
        return output.getAbsolutePath();
    }

    private int copyTreeRecursive(Uri parentUri, File target, long[] budget, int depth)
            throws IOException {
        if (depth > 8) return 0;
        final int copied;
        try {
            copied = copyChildren(parentUri, target, budget, depth);
        } catch (RuntimeException error) {
            throw new IOException("Unable to read the chosen folder", error);
        }
        return copied;
    }

    private int copyChildren(Uri parentUri, File target, long[] budget, int depth)
            throws IOException {
        int copied = 0;
        try (android.database.Cursor cursor = getContentResolver().query(parentUri,
                new String[]{android.provider.DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                        android.provider.DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                        android.provider.DocumentsContract.Document.COLUMN_MIME_TYPE},
                null, null, null)) {
            if (cursor == null) return 0;
            while (cursor.moveToNext() && copied < MAX_ARCHIVE_ENTRIES) {
                final String documentId = cursor.getString(0);
                final String name = cursor.getString(1);
                final String mime = cursor.getString(2);
                if (documentId == null) continue;
                final Uri childUri = android.provider.DocumentsContract.buildDocumentUriUsingTree(
                        parentUri, documentId);
                if (android.provider.DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    File child = new File(target, sanitizeFileName(name == null ? "dir" : name));
                    if (!child.mkdirs() && !child.isDirectory()) continue;
                    copied += copyChildren(childUri, child, budget, depth + 1);
                    continue;
                }
                try (InputStream input = getContentResolver().openInputStream(childUri)) {
                    if (input == null) continue;
                    File child = new File(target, sanitizeFileName(name == null ? "file" : name));
                    try (FileOutputStream output = new FileOutputStream(child)) {
                        byte[] buffer = new byte[64 * 1024];
                        int count;
                        while ((count = input.read(buffer)) != -1) {
                            budget[0] -= count;
                            if (budget[0] < 0) {
                                throw new IOException("The chosen folder is too large");
                            }
                            output.write(buffer, 0, count);
                        }
                    }
                    ++copied;
                }
            }
        }
        return copied;
    }
    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteRecursively(child);
        file.delete();
    }
}
