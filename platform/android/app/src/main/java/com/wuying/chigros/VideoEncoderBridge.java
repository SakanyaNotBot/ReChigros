package com.wuying.chigros;

import android.media.MediaCodec;
import android.media.MediaCodecInfo;
import android.media.MediaFormat;
import android.media.MediaMuxer;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.ShortBuffer;

/**
 * Hardware encoder for the recorder.
 *
 * <p>The packaged build has no external encoder to shell out to, so the recorder hands its frames
 * here instead. Frames arrive as top-down RGBA bytes and are written into the encoder's input
 * image planes; the music the recorder already decoded is encoded to AAC and muxed alongside.
 *
 * <p>All state is static because a recording is a single process-wide session: one runs at a time,
 * and the native side addresses these entry points by name.
 */
final class VideoEncoderBridge {
    private static final String TAG = "ReChigrosEncoder";
    private static final String MIME_VIDEO = MediaFormat.MIMETYPE_VIDEO_AVC;
    private static final String MIME_AUDIO = MediaFormat.MIMETYPE_AUDIO_AAC;

    private static MediaCodec videoEncoder;
    private static MediaCodec audioEncoder;
    private static MediaMuxer muxer;
    private static MediaCodec.BufferInfo videoInfo;
    private static MediaCodec.BufferInfo audioInfo;
    private static int videoTrack = -1;
    private static int audioTrack = -1;
    private static boolean muxerStarted;
    private static File outputFile;
    private static int width;
    private static int height;
    private static int frameRate;
    private static long frameIndex;

    private static byte[] rgbaScratch;
    private static int[] lumaScratch;

    /** Music supplied by the recorder, decoded already to interleaved stereo float samples. */
    private static byte[] pcmBytes;
    private static int pcmSampleRate;
    private static int pcmDelayMs;

    private VideoEncoderBridge() {
    }

    static synchronized boolean start(String path, int w, int h, int fps, int quality) {
        abort();
        outputFile = new File(path);
        width = w;
        height = h;
        frameRate = Math.max(15, Math.min(240, fps));
        frameIndex = 0;
        try {
            final File parent = outputFile.getParentFile();
            if (parent != null && !parent.exists() && !parent.mkdirs()) {
                Log.e(TAG, "cannot create output directory " + parent);
                return false;
            }
            // The desktop path treats the setting as a quantiser, so a lower value means a higher
            // bitrate here.
            final double bitsPerPixel = quality <= 18 ? 0.16 : (quality <= 26 ? 0.10 : 0.06);
            final int bitRate = Math.max(1_000_000, (int) (w * (double) h * frameRate * bitsPerPixel));

            final MediaFormat format =
                    MediaFormat.createVideoFormat(MIME_VIDEO, w, h);
            format.setInteger(MediaFormat.KEY_COLOR_FORMAT,
                    MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible);
            format.setInteger(MediaFormat.KEY_BIT_RATE, bitRate);
            format.setInteger(MediaFormat.KEY_FRAME_RATE, frameRate);
            format.setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1);

            videoEncoder = MediaCodec.createEncoderByType(MIME_VIDEO);
            videoEncoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            videoEncoder.start();

            muxer = new MediaMuxer(path, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4);
            videoInfo = new MediaCodec.BufferInfo();
            audioInfo = new MediaCodec.BufferInfo();
            muxerStarted = false;
            videoTrack = -1;
            audioTrack = -1;
            rgbaScratch = new byte[w * h * 4];
            lumaScratch = new int[w * h];
            Log.i(TAG, "video encoder started " + w + "x" + h + " @" + frameRate + " bitRate=" + bitRate);
            return true;
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "cannot start encoder", error);
            abort();
            return false;
        }
    }

    /**
     * Takes the recorder's raw audio track. The samples stay in memory until the video is finished,
     * because the muxer only accepts both tracks after the container is started.
     */
    static synchronized boolean setAudioTrack(String path, int sampleRate, int delayMs) {
        pcmSampleRate = Math.max(8000, sampleRate);
        pcmDelayMs = Math.max(0, delayMs);
        pcmBytes = null;
        try {
            final File file = new File(path);
            if (file.isFile() && file.length() > 0 && file.length() < Integer.MAX_VALUE) {
                pcmBytes = new byte[(int) file.length()];
                try (java.io.RandomAccessFile input = new java.io.RandomAccessFile(file, "r")) {
                    input.readFully(pcmBytes);
                }
                Log.i(TAG, "audio track loaded bytes=" + pcmBytes.length + " rate=" + pcmSampleRate);
            }
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "cannot read audio track", error);
            pcmBytes = null;
        }
        return pcmBytes != null;
    }

    /** Queues one top-down RGBA frame. */
    static synchronized boolean queueFrame(byte[] rgba, int w, int h) {
        if (videoEncoder == null || rgba == null) return false;
        if (w != width || h != height || rgba.length < w * h * 4) return false;
        try {
            final long presentationUs = frameIndex * 1_000_000L / frameRate;
            if (!submitFrame(rgba, presentationUs)) return false;
            frameIndex++;
            return true;
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot queue frame", error);
            return false;
        }
    }

    private static boolean submitFrame(byte[] rgba, long presentationUs) {
        int index = videoEncoder.dequeueInputBuffer(10_000L);
        for (int attempt = 0; index < 0 && attempt < 20; ++attempt) {
            drainVideo(false);
            index = videoEncoder.dequeueInputBuffer(10_000L);
        }
        if (index < 0) return false;

        // The Image API is the documented way to fill a flexible-format input buffer: the plane
        // layout and strides are the codec's, not an assumption about a specific pixel format.
        android.media.Image image = videoEncoder.getInputImage(index);
        if (image == null) return false;
        android.media.Image.Plane[] planes = image.getPlanes();
        if (planes.length < 3) {
            // Returning the buffer with no data keeps the encoder's input queue consistent; leaving
            // it un-returned would leak one slot per frame and stall the recording.
            videoEncoder.queueInputBuffer(index, 0, 0, presentationUs, 0);
            return false;
        }
        fillPlane(planes[0], rgba, 0);
        fillPlane(planes[1], rgba, 1);
        fillPlane(planes[2], rgba, 2);
        // The buffer's size is the whole image, not zero: a zero-length frame tells the encoder an
        // empty buffer arrived, and it answers with a codec error instead of an encoded frame.
        final int imageSize = planes[0].getBuffer().capacity()
                + planes[1].getBuffer().capacity() + planes[2].getBuffer().capacity();
        videoEncoder.queueInputBuffer(index, 0, imageSize, presentationUs, 0);
        return drainVideo(false);
    }

    /**
     * Writes one colour plane from the RGBA frame. Luma is computed per pixel; the chroma planes are
     * sampled at their own resolution, so one sample serves a 2x2 block of pixels.
     */
    private static void fillPlane(android.media.Image.Plane plane, byte[] rgba, int planeIndex) {
        final ByteBuffer buffer = plane.getBuffer();
        final int rowStride = plane.getRowStride();
        final int pixelStride = plane.getPixelStride();
        buffer.position(0);
        final boolean luma = planeIndex == 0;
        final int stepX = luma ? 1 : 2;
        final int stepY = luma ? 1 : 2;
        int offset = 0;
        for (int y = 0; y < height; y += stepY) {
            final int rowStart = y * width * 4;
            int columnOffset = offset;
            for (int x = 0; x < width; x += stepX) {
                final int p = rowStart + x * 4;
                final int r = rgba[p] & 0xFF;
                final int g = rgba[p + 1] & 0xFF;
                final int b = rgba[p + 2] & 0xFF;
                final int value;
                if (luma) {
                    final int yv = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
                    value = yv < 16 ? 16 : (yv > 235 ? 235 : yv);
                } else if (planeIndex == 1) {
                    final int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
                    value = u < 0 ? 0 : (u > 255 ? 255 : u);
                } else {
                    final int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
                    value = v < 0 ? 0 : (v > 255 ? 255 : v);
                }
                final int position = columnOffset;
                if (position >= 0 && position < buffer.limit()) buffer.put(position, (byte) value);
                columnOffset += pixelStride;
            }
            offset += rowStride;
        }
    }

    private static boolean drainVideo(boolean waitForOutput) {
        while (true) {
            final int index = videoEncoder.dequeueOutputBuffer(videoInfo, waitForOutput ? 10_000L : 0L);
            if (index == MediaCodec.INFO_TRY_AGAIN_LATER) return true;
            if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                ensureMuxerStarted();
                continue;
            }
            if (index < 0) continue;
            final ByteBuffer output = videoEncoder.getOutputBuffer(index);
            if (output != null && videoInfo.size > 0) {
                output.position(videoInfo.offset);
                output.limit(videoInfo.offset + videoInfo.size);
                if (muxerStarted && videoTrack >= 0) muxer.writeSampleData(videoTrack, output, videoInfo);
            }
            videoEncoder.releaseOutputBuffer(index, false);
        }
    }

    /** Starts the muxer once both track formats are known. */
    private static void ensureMuxerStarted() {
        if (muxerStarted || muxer == null) return;
        if (videoTrack < 0) {
            videoTrack = muxer.addTrack(videoEncoder.getOutputFormat());
        }
        if (audioTrack < 0 && audioEncoder != null) {
            return; // wait for the audio track's format before starting
        }
        muxer.start();
        muxerStarted = true;
    }

    /** Encodes the whole supply of music into the muxer, then finishes the container. */
    static synchronized boolean finish() {
        if (videoEncoder == null) return false;
        try {
            // The last video frame is followed by the end-of-stream flag so the encoder flushes.
            final int lastIndex = videoEncoder.dequeueInputBuffer(100_000L);
            if (lastIndex >= 0) {
                videoEncoder.queueInputBuffer(lastIndex, 0, 0,
                        (frameIndex + 1) * 1_000_000L / frameRate,
                        MediaCodec.BUFFER_FLAG_END_OF_STREAM);
            }
            boolean ended = false;
            final long deadline = System.currentTimeMillis() + 20_000L;
            while (!ended && System.currentTimeMillis() < deadline) {
                final int index = videoEncoder.dequeueOutputBuffer(videoInfo, 10_000L);
                if (index == MediaCodec.INFO_TRY_AGAIN_LATER) continue;
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    ensureMuxerStarted();
                    continue;
                }
                if (index < 0) continue;
                final ByteBuffer output = videoEncoder.getOutputBuffer(index);
                if (output != null && videoInfo.size > 0) {
                    output.position(videoInfo.offset);
                    output.limit(videoInfo.offset + videoInfo.size);
                    if (muxerStarted && videoTrack >= 0) muxer.writeSampleData(videoTrack, output, videoInfo);
                }
                videoEncoder.releaseOutputBuffer(index, false);
                ended = (videoInfo.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0;
            }
            // The video track alone is still a complete recording, so the audio is best effort.
            if (pcmBytes != null) encodeAudio();
            release();
            Log.i(TAG, "encoder finished frames=" + frameIndex);
            return outputFile != null && outputFile.isFile() && outputFile.length() > 0;
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot finish encoder", error);
            abort();
            return false;
        }
    }

    /**
     * Encodes the recorder's float samples to AAC and muxes them with the video. The track starts
     * after the recording's opening lead so the music lines up with the chart.
     */
    private static void encodeAudio() {
        MediaCodec encoder = null;
        try {
            final int channels = 2;
            final MediaFormat format = MediaFormat.createAudioFormat(MIME_AUDIO, pcmSampleRate, channels);
            format.setInteger(MediaFormat.KEY_AAC_PROFILE,
                    MediaCodecInfo.CodecProfileLevel.AACObjectLC);
            format.setInteger(MediaFormat.KEY_BIT_RATE, 192_000);
            format.setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, 16384);
            encoder = MediaCodec.createEncoderByType(MIME_AUDIO);
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE);
            encoder.start();
            audioEncoder = encoder;

            final int floatBytes = 4;
            final int frameBytes = channels * floatBytes;
            final int totalFrames = pcmBytes.length / frameBytes;
            final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            final ByteBuffer shorts = ByteBuffer.allocateDirect(16384).order(ByteOrder.nativeOrder());
            ShortBuffer shortView = shorts.asShortBuffer();
            int cursor = 0;
            long presentedSamples = 0;
            boolean inputDone = false;

            while (true) {
                if (!inputDone) {
                    final int index = encoder.dequeueInputBuffer(10_000L);
                    if (index >= 0) {
                        final ByteBuffer input = encoder.getInputBuffer(index);
                        if (input != null) {
                            input.clear();
                            shorts.clear();
                            shortView = shorts.asShortBuffer();
                            int samples = 0;
                            while (cursor < totalFrames && samples + channels <= shortView.capacity()) {
                                final int base = cursor * frameBytes;
                                for (int c = 0; c < channels; ++c) {
                                    final int offset = base + c * floatBytes;
                                    final float value = Float.intBitsToFloat(
                                            (pcmBytes[offset] & 0xFF)
                                                    | ((pcmBytes[offset + 1] & 0xFF) << 8)
                                                    | ((pcmBytes[offset + 2] & 0xFF) << 16)
                                                    | ((pcmBytes[offset + 3] & 0xFF) << 24));
                                    final int scaled = (int) (Math.max(-1.0f, Math.min(1.0f, value)) * 32767.0f);
                                    shorts.putShort((short) scaled);
                                    samples++;
                                }
                                cursor++;
                            }
                            final int written = shorts.position();
                            input.put(shorts.array(), 0, Math.min(written, input.capacity()));
                            final long presentationUs = (pcmDelayMs * 1000L)
                                    + presentedSamples * 1_000_000L / pcmSampleRate;
                            final boolean last = cursor >= totalFrames;
                            encoder.queueInputBuffer(index, 0, Math.min(written, input.capacity()),
                                    presentationUs, last ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
                            presentedSamples += written / (channels * 2L);
                            if (last) inputDone = true;
                        }
                    }
                }
                final int index = encoder.dequeueOutputBuffer(info, 10_000L);
                if (index == MediaCodec.INFO_TRY_AGAIN_LATER) {
                    if (inputDone) break;
                    continue;
                }
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    if (audioTrack < 0) audioTrack = muxer.addTrack(encoder.getOutputFormat());
                    if (!muxerStarted) {
                        muxer.start();
                        muxerStarted = true;
                    }
                    continue;
                }
                if (index < 0) continue;
                final ByteBuffer output = encoder.getOutputBuffer(index);
                if (output != null && info.size > 0 && muxerStarted && audioTrack >= 0) {
                    output.position(info.offset);
                    output.limit(info.offset + info.size);
                    muxer.writeSampleData(audioTrack, output, info);
                }
                encoder.releaseOutputBuffer(index, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break;
            }
        } catch (IOException | RuntimeException error) {
            Log.e(TAG, "cannot encode audio", error);
        } finally {
            audioEncoder = null;
            if (encoder != null) {
                try {
                    encoder.stop();
                } catch (RuntimeException ignored) {
                }
                encoder.release();
            }
        }
    }

    static synchronized void abort() {
        try {
            release();
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot release encoder", error);
        }
        pcmBytes = null;
        videoEncoder = null;
        muxer = null;
        muxerStarted = false;
        videoTrack = -1;
        audioTrack = -1;
        frameIndex = 0;
    }

    private static void release() {
        if (videoEncoder != null) {
            try {
                videoEncoder.stop();
            } catch (RuntimeException ignored) {
            }
            videoEncoder.release();
            videoEncoder = null;
        }
        if (muxer != null) {
            try {
                if (muxerStarted) muxer.stop();
            } catch (RuntimeException ignored) {
            }
            muxer.release();
            muxer = null;
        }
        muxerStarted = false;
    }
}
