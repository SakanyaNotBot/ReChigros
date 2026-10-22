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
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Intent intent = getIntent();
        if (intent != null) {
            try {
                String path = prepareIncomingIntent(intent);
                if (path != null) intent.setData(Uri.fromFile(new File(path)));
            } catch (IOException error) {
                Log.e(TAG, "Unable to prepare incoming chart", error);
                intent.setData(null);
            }
        }
        super.onCreate(savedInstanceState);
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
            } catch (IOException error) {
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
        if ("file".equalsIgnoreCase(uri.getScheme())) {
            File file = new File(uri.getPath());
            return isZip(file) ? extractChartArchive(file).getAbsolutePath() : file.getAbsolutePath();
        }
        if (!"content".equalsIgnoreCase(uri.getScheme())) {
            throw new IOException("Unsupported URI scheme: " + uri.getScheme());
        }
        try {
            getContentResolver().takePersistableUriPermission(uri,
                    intent.getFlags() & Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (RuntimeException ignored) {
        }
        File imported = copyContentUri(uri);
        return isZip(imported) ? extractChartArchive(imported).getAbsolutePath()
                : imported.getAbsolutePath();
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
    private static final int REQUEST_PICKER = 0x51D1;

    private static MainActivity instance;

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
        if (resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri uri = data.getData();
        // Reading the document touches the disk, so it runs off the UI thread.
        new Thread(() -> {
            try {
                File imported = copyContentUri(uri);
                final String path = isZip(imported) ? extractChartArchive(imported).getAbsolutePath()
                        : imported.getAbsolutePath();
                SDLActivity.onNativeDropFile(path);
            } catch (IOException error) {
                Log.e(TAG, "Unable to read the chosen document", error);
            }
        }).start();
    }
    private static void deleteRecursively(File file) {
        File[] children = file.listFiles();
        if (children != null) for (File child : children) deleteRecursively(child);
        file.delete();
    }
}
