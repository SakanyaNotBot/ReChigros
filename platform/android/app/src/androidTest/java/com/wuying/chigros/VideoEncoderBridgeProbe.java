package com.wuying.chigros;

import android.app.Activity;
import android.app.Instrumentation;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

public final class VideoEncoderBridgeProbe extends Instrumentation {
    private static final String TAG = "VideoEncoderBridgeProbe";

    @Override
    public void onCreate(Bundle arguments) {
        super.onCreate(arguments);
        start();
    }

    @Override
    public void onStart() {
        super.onStart();
        final Bundle results = new Bundle();
        try {
            runProbe(results);
            results.putString("status", "PASS");
            finish(Activity.RESULT_OK, results);
        } catch (Throwable error) {
            Log.e(TAG, "probe failed", error);
            MainActivity.abortVideoEncoder();
            results.putString("status", "FAIL");
            results.putString("error", Log.getStackTraceString(error));
            finish(Activity.RESULT_CANCELED, results);
        }
    }

    private void runProbe(Bundle results) throws Exception {
        final File external = getTargetContext().getExternalFilesDir(null);
        check(external != null, "external storage is unavailable");
        final File pcm = new File(getTargetContext().getCacheDir(), "aac-bridge-probe.pcm");
        final File output = new File(external,
                "aac-bridge-probe-" + System.currentTimeMillis() + ".mp4");
        final int sampleRate = 48_000;
        final int pcmFrames = sampleRate * 2;
        final byte[] samples = new byte[pcmFrames * 2 * Float.BYTES];
        final FloatBuffer floats = ByteBuffer.wrap(samples)
                .order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer();
        for (int frame = 0; frame < pcmFrames; ++frame) {
            final double time = frame / (double) sampleRate;
            final float left = (float) (0.42 * Math.sin(2.0 * Math.PI * 440.0 * time)
                    + 0.08 * Math.sin(2.0 * Math.PI * 880.0 * time));
            final float right = (float) (0.34 * Math.sin(2.0 * Math.PI * 660.0 * time));
            floats.put(left);
            floats.put(right);
        }
        try (FileOutputStream stream = new FileOutputStream(pcm)) {
            stream.write(samples);
        }

        // The device encoder rejects a 64x64 surface with BAD_VALUE, so the probe uses a size it
        // advertises support for; the audio path under test is independent of the frame size.
        final int width = 1280;
        final int height = 720;
        final int videoFrames = 30;
        check(MainActivity.startVideoEncoder(output.getAbsolutePath(), width, height, 30, 23),
                "startVideoEncoder returned false");
        check(MainActivity.setVideoAudioTrack(pcm.getAbsolutePath(), sampleRate, 2, 0),
                "setVideoAudioTrack returned false");
        for (int frame = 0; frame < videoFrames; ++frame) {
            final ByteBuffer planes = ByteBuffer.allocateDirect(width * height * 3 / 2);
            for (int i = 0; i < width * height; ++i) {
                planes.put((byte) (24 + frame * 5 + (i & 7)));
            }
            for (int i = 0; i < width * height / 2; ++i) {
                planes.put((byte) (i % 2 == 0 ? 96 + frame : 160 - frame));
            }
            planes.flip();
            check(MainActivity.queueVideoFrame(planes, width, height),
                    "queueVideoFrame returned false at frame " + frame);
        }
        check(MainActivity.finishVideoEncoder(), "finishVideoEncoder returned false");
        check(output.isFile() && output.length() > 0, "the container is missing or empty");

        final MediaExtractor extractor = new MediaExtractor();
        extractor.setDataSource(output.getAbsolutePath());
        int audioTrack = -1;
        int videoTrack = -1;
        long audioDurationUs = -1;
        for (int track = 0; track < extractor.getTrackCount(); ++track) {
            final MediaFormat format = extractor.getTrackFormat(track);
            final String mime = format.getString(MediaFormat.KEY_MIME);
            if ("audio/mp4a-latm".equals(mime)) {
                audioTrack = track;
                audioDurationUs = format.getLong(MediaFormat.KEY_DURATION);
            } else if ("video/avc".equals(mime)) {
                videoTrack = track;
            }
        }
        check(videoTrack >= 0, "H.264 track is absent");
        check(audioTrack >= 0, "AAC track is absent");
        check(audioDurationUs >= 1_900_000L, "AAC duration is " + audioDurationUs);
        extractor.selectTrack(audioTrack);
        final ByteBuffer packetBuffer = ByteBuffer.allocateDirect(65_536);
        int audioPackets = 0;
        long audioBytes = 0;
        long lastAudioPtsUs = -1;
        while (true) {
            final int size = extractor.readSampleData(packetBuffer, 0);
            if (size < 0) break;
            check(size > 0, "zero-length AAC packet at index " + audioPackets);
            audioPackets++;
            audioBytes += size;
            lastAudioPtsUs = extractor.getSampleTime();
            if (!extractor.advance()) break;
        }
        extractor.release();
        check(audioPackets >= 90, "AAC packet count is " + audioPackets);
        check(audioBytes >= 30_000L, "AAC payload is only " + audioBytes + " bytes");
        check(lastAudioPtsUs >= 1_800_000L, "last AAC PTS is " + lastAudioPtsUs);

        results.putString("output", output.getAbsolutePath());
        results.putInt("pcmFrames", pcmFrames);
        results.putInt("videoFrames", videoFrames);
        results.putInt("audioPackets", audioPackets);
        results.putLong("audioBytes", audioBytes);
        results.putLong("audioDurationUs", audioDurationUs);
        results.putLong("lastAudioPtsUs", lastAudioPtsUs);
        Log.i(TAG, "PASS output=" + output + " pcmFrames=" + pcmFrames
                + " videoFrames=" + videoFrames + " audioPackets=" + audioPackets
                + " audioBytes=" + audioBytes + " audioDurationUs=" + audioDurationUs
                + " lastAudioPtsUs=" + lastAudioPtsUs);
    }

    private static void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
}
