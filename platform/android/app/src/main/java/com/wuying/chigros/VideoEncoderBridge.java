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

    /** Compressed AAC packets, held until the muxer has started. */
    private static final java.util.List<byte[]> audioPackets = new java.util.ArrayList<>();
    private static final java.util.List<MediaCodec.BufferInfo> audioPacketInfo =
            new java.util.ArrayList<>();
    private static MediaFormat audioFormat;

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
            audioPackets.clear();
            audioPacketInfo.clear();
            audioFormat = null;
            rgbaScratch = new byte[w * h * 4];
            lumaScratch = new int[w * h];
            // The music is encoded up front. A muxer refuses new tracks once it has started, and
            // the video track's format only becomes known while frames are flowing, so the audio
            // track has to exist before that.
            if (pcmBytes != null) prepareAudioTrack();
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

    /**
     * Queues one frame in the encoder's planar layout: every luma row, then both chroma planes at
     * half size. The graphics device produced those bytes, so nothing here has to convert them;
     * only the row-by-row copy into the codec's own buffers is left.
     */
    static synchronized boolean queueFrame(byte[] planes, int w, int h) {
        if (videoEncoder == null || planes == null) return false;
        final int lumaSize = w * h;
        final int chromaSize = (w / 2) * (h / 2);
        if (w != width || h != height || planes.length < lumaSize + chromaSize * 2) return false;
        try {
            final long presentationUs = frameIndex * 1_000_000L / frameRate;
            if (!submitFrame(planes, presentationUs)) return false;
            frameIndex++;
            return true;
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot queue frame", error);
            return false;
        }
    }

    private static boolean submitFrame(byte[] source, long presentationUs) {
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
        final int lumaSize = width * height;
        final int chromaSize = (width / 2) * (height / 2);
        copyLuma(planes[0], source, width, height);
        // Both colour channels live in one interleaved pair of buffers: each row holds every
        // sample for that row of the picture, with the two channels alternating. The pair is
        // written together so neither channel overwrites the other.
        copyChroma(planes[1], planes[2], source, lumaSize, width / 2, height / 2);
        // The size is the whole frame in its planar form. Reporting zero makes the codec discard
        // the buffer as carrying nothing, and the plane capacities are not a substitute: they are
        // separate views that describe more bytes than the one buffer they share.
        final int frameSize = width * height * 3 / 2;
        videoEncoder.queueInputBuffer(index, 0, frameSize, presentationUs, 0);
        return drainVideo(false);
    }

    /** Writes the picture's brightness into the codec's luma buffer. */
    private static void copyLuma(android.media.Image.Plane plane, byte[] source, int width,
            int height) {
        final ByteBuffer buffer = plane.getBuffer();
        final int rowStride = plane.getRowStride();
        final int pixelStride = plane.getPixelStride();
        buffer.position(0);
        for (int y = 0; y < height; ++y) {
            final int rowStart = y * width;
            if (pixelStride == 1) {
                buffer.put(source, rowStart, Math.min(width, buffer.remaining()));
            } else {
                for (int x = 0; x < width; ++x) {
                    if (!buffer.hasRemaining()) return;
                    buffer.put(source[rowStart + x]);
                    buffer.position(buffer.position() + Math.min(pixelStride - 1,
                            buffer.remaining()));
                }
            }
            final int padding = rowStride - width * pixelStride;
            if (padding > 0) buffer.position(buffer.position() + Math.min(padding,
                    buffer.remaining()));
        }
    }

    /**
     * Writes the picture's colour into the codec's chroma buffer. Both channels share one buffer
     * with their samples alternating, so a pair of samples is written per step: the first channel
     * at the current position, the second one slot further on. The two plane views describe the
     * same bytes from different starting points, so writing through both would overlap.
     */
    private static void copyChroma(android.media.Image.Plane planeU, android.media.Image.Plane planeV,
            byte[] source, int sourceOffset, int chromaWidth, int chromaHeight) {
        final ByteBuffer buffer = planeU.getBuffer();
        final int rowStride = planeU.getRowStride();
        final int pixelStride = planeU.getPixelStride();
        buffer.position(0);
        final int step = Math.max(1, pixelStride);
        // The source keeps the two channels in separate planes, one after the other, while the
        // destination interleaves them, so each step reads one sample from each source plane.
        final int sourceU = sourceOffset;
        final int sourceV = sourceOffset + chromaWidth * chromaHeight;
        for (int y = 0; y < chromaHeight; ++y) {
            final int rowStart = y * chromaWidth;
            for (int x = 0; x < chromaWidth; ++x) {
                if (buffer.remaining() < 2) return;
                buffer.put(source[sourceU + rowStart + x]);
                buffer.put(source[sourceV + rowStart + x]);
                if (step > 2) {
                    final int skip = Math.min(step - 2, buffer.remaining());
                    buffer.position(buffer.position() + skip);
                }
            }
            final int padding = rowStride - chromaWidth * step;
            if (padding > 0) buffer.position(buffer.position() + Math.min(padding, buffer.remaining()));
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

    /**
     * Starts the muxer once every track format is known. A muxer only accepts tracks until it is
     * started: adding one afterwards throws, which is why the audio encoder has to be running
     * before the first video frame is written.
     */
    private static void ensureMuxerStarted() {
        if (muxerStarted || muxer == null) return;
        if (videoTrack < 0) {
            videoTrack = muxer.addTrack(videoEncoder.getOutputFormat());
        }
        muxer.start();
        muxerStarted = true;
        flushAudioPackets();
    }

    /** Writes the audio packets buffered before the container was opened. */
    private static void flushAudioPackets() {
        if (!muxerStarted || audioTrack < 0) return;
        for (int i = 0; i < audioPackets.size(); ++i) {
            final byte[] packet = audioPackets.get(i);
            final ByteBuffer buffer = ByteBuffer.wrap(packet);
            muxer.writeSampleData(audioTrack, buffer, audioPacketInfo.get(i));
        }
        audioPackets.clear();
        audioPacketInfo.clear();
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
     * Encodes the recorder's float samples to AAC up front and keeps the compressed packets and
     * the track format. The muxer needs the format before it starts, and it has to start before
     * any sample is written, so the audio cannot be encoded lazily beside the video.
     */
    private static void prepareAudioTrack() {
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
                    // The track is added now, while the muxer is still unstarted; its samples are
                    // held until the video track exists and the container has been opened.
                    audioFormat = encoder.getOutputFormat();
                    audioTrack = muxer.addTrack(audioFormat);
                    continue;
                }
                if (index < 0) continue;
                final ByteBuffer output = encoder.getOutputBuffer(index);
                if (output != null && info.size > 0) {
                    final byte[] packet = new byte[info.size];
                    output.position(info.offset);
                    output.limit(info.offset + info.size);
                    output.get(packet);
                    final MediaCodec.BufferInfo copy = new MediaCodec.BufferInfo();
                    copy.set(0, packet.length, info.presentationTimeUs, info.flags);
                    audioPackets.add(packet);
                    audioPacketInfo.add(copy);
                }
                encoder.releaseOutputBuffer(index, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) break;
            }
        } catch (IOException | RuntimeException error) {
            // A recording without music is still a complete recording, so this is not fatal.
            Log.e(TAG, "cannot prepare audio", error);
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
        audioPackets.clear();
        audioPacketInfo.clear();
        audioFormat = null;
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
