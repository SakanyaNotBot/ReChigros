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
    private static volatile boolean audioPrepared;

    // Progress of the music encode. The track is encoded on its own thread because it takes as
    // long as the song does; the recorder polls this before it starts producing frames, which is
    // the order the container needs (a muxer only takes new tracks before it is started).
    private static final int AUDIO_NONE = 0;
    private static final int AUDIO_PREPARING = 1;
    private static final int AUDIO_READY = 2;
    private static final int AUDIO_FAILED = 3;
    private static volatile int audioState = AUDIO_NONE;
    // Bumped whenever a run starts or is aborted. A worker compares the value it captured with
    // the current one and drops its result if they differ, so a track from a cancelled run can
    // never be attached to the next one.
    private static volatile int audioGeneration;
    private static Thread audioThread;

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
            // The music track is *not* encoded here. It arrives through setAudioTrack, and encoding
            // it takes as long as the whole song: doing that inside this call runs the encode on the
            // thread that draws, which freezes the interface for the duration. It is started on its
            // own thread instead and the caller polls audioTrackState() before producing frames.
            audioGeneration++;
            audioState = AUDIO_NONE;
            audioThread = null;
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
        // The bytes are loaded here because the file belongs to the caller and is deleted as soon as
        // this returns; the encode itself is handed to a worker. Reading the file costs well under a
        // second, encoding the whole track costs as long as the song.
        if (pcmBytes != null && videoEncoder != null && !audioPrepared) {
            startAudioEncode();
        }
        return pcmBytes != null;
    }

    /**
     * Reports how far the music encode has got, so the caller can wait for it without blocking the
     * thread that draws. See the AUDIO_* constants.
     */
    static int audioTrackState() {
        // Deliberately not synchronized: the caller polls this once per frame, while the frame
        // hand-off holds the class lock for the whole copy. Sharing that lock made the poll wait
        // for a frame to be copied (measured 638 ms on the device), which is exactly the stall
        // this method exists to avoid. Both fields are volatile, so a poll always sees a whole
        // value, and a stale read at worst repeats one frame of waiting.
        if (audioPrepared) return AUDIO_READY;
        return audioState;
    }

    /**
     * Encodes the loaded track on a worker thread. The worker holds no lock while it runs, so the
     * caller stays free to draw and to poll audioTrackState(); only the short hand-offs at either
     * end are synchronised.
     */
    private static void startAudioEncode() {
        if (audioState == AUDIO_PREPARING) return;
        audioState = AUDIO_PREPARING;
        final int generation = audioGeneration;
        final Thread worker = new Thread(new Runnable() {
            @Override
            public void run() {
                boolean ok = false;
                try {
                    ok = prepareAudioTrack(generation);
                } catch (Throwable failure) {
                    // The caller waits on this state, so it has to leave the "preparing" value
                    // behind no matter how the worker ends. An escaping failure would otherwise
                    // leave the recording waiting forever for a track that will never arrive.
                    Log.e(TAG, "audio encode failed", failure);
                    ok = false;
                }
                synchronized (VideoEncoderBridge.class) {
                    // A stale worker must not touch the state of the run that replaced it.
                    if (generation == audioGeneration) {
                        audioState = ok ? AUDIO_READY : AUDIO_FAILED;
                    }
                }
            }
        }, "ReChigrosAudioEncode");
        worker.setDaemon(true);
        audioThread = worker;
        worker.start();
    }

    /**
     * Queues one frame in the encoder's planar layout: every luma row, then both chroma planes at
     * half size. The graphics device produced those bytes, so nothing here has to convert them;
     * only the row-by-row copy into the codec's own buffers is left.
     */
    static synchronized boolean queueFrame(java.nio.ByteBuffer frame, int w, int h) {
        if (videoEncoder == null || frame == null || !frame.isDirect()) return false;
        final int lumaSize = w * h;
        final int chromaSize = (w / 2) * (h / 2);
        if (w != width || h != height || frame.capacity() < lumaSize + chromaSize * 2) return false;
        try {
            final long presentationUs = frameIndex * 1_000_000L / frameRate;
            if (!submitFrame(frame, presentationUs)) return false;
            frameIndex++;
            return true;
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot queue frame", error);
            return false;
        }
    }

    private static boolean submitFrame(java.nio.ByteBuffer frame, long presentationUs) {
        // The caller already paces itself against its own queue, so the input buffer is normally
        // free the moment it asks. A blocking wait here would add latency for no benefit, so the
        // call only polls and any output that has piled up is drained while retrying.
        int index = videoEncoder.dequeueInputBuffer(0L);
        for (int attempt = 0; index < 0 && attempt < 64; ++attempt) {
            drainVideo(false);
            index = videoEncoder.dequeueInputBuffer(0L);
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
        copyLuma(planes[0], frame, width, height);
        // Both colour channels live in one interleaved pair of buffers: each row holds every
        // sample for that row of the picture, with the two channels alternating. The pair is
        // written together so neither channel overwrites the other.
        copyChroma(planes[1], frame, lumaSize, width / 2, height / 2);
        // The size is the whole frame in its planar form. Reporting zero makes the codec discard
        // the buffer as carrying nothing, and the plane capacities are not a substitute: they are
        // separate views that describe more bytes than the one buffer they share.
        final int frameSize = width * height * 3 / 2;
        videoEncoder.queueInputBuffer(index, 0, frameSize, presentationUs, 0);
        return drainVideo(false);
    }

    /** Writes the picture's brightness into the codec's luma buffer. */
    private static void copyLuma(android.media.Image.Plane plane, java.nio.ByteBuffer source,
            int width, int height) {
        final ByteBuffer buffer = plane.getBuffer();
        final int rowStride = plane.getRowStride();
        final int pixelStride = plane.getPixelStride();
        buffer.position(0);
        for (int y = 0; y < height; ++y) {
            final int rowStart = y * width;
            if (pixelStride == 1) {
                final int run = Math.min(width, buffer.remaining());
                final java.nio.ByteBuffer row = source.duplicate();
                row.position(rowStart);
                row.limit(rowStart + run);
                buffer.put(row);
            } else {
                for (int x = 0; x < width; ++x) {
                    if (!buffer.hasRemaining()) return;
                    buffer.put(source.get(rowStart + x));
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
    private static void copyChroma(android.media.Image.Plane planeU, java.nio.ByteBuffer source,
            int sourceOffset, int chromaWidth, int chromaHeight) {
        final ByteBuffer buffer = planeU.getBuffer();
        final int rowStride = planeU.getRowStride();
        final int pixelStride = planeU.getPixelStride();
        // The two channels sit in separate source planes but interleave in the destination, so one
        // row of each is read into scratch and then written as alternating pairs. Reading through
        // the buffer a sample at a time costs a bounds check and a call per byte, which on a full
        // frame is more work than the copy itself.
        final byte[] rowU = new byte[chromaWidth];
        final byte[] rowV = new byte[chromaWidth];
        final java.nio.ByteBuffer sourceU = source.duplicate();
        final java.nio.ByteBuffer sourceV = source.duplicate();
        buffer.position(0);
        for (int y = 0; y < chromaHeight; ++y) {
            sourceU.position(sourceOffset + y * chromaWidth);
            sourceU.get(rowU, 0, chromaWidth);
            sourceV.position(sourceOffset + chromaWidth * chromaHeight + y * chromaWidth);
            sourceV.get(rowV, 0, chromaWidth);
            for (int x = 0; x < chromaWidth; ++x) {
                if (buffer.remaining() < 2) return;
                buffer.put(rowU[x]);
                buffer.put(rowV[x]);
                if (pixelStride > 2) {
                    buffer.position(buffer.position() + Math.min(pixelStride - 2,
                            buffer.remaining()));
                }
            }
            final int padding = rowStride - chromaWidth * pixelStride;
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
        Log.i(TAG, "muxer started videoTrack=" + videoTrack + " audioTrack=" + audioTrack);
        flushAudioPackets();
    }

    /** Writes the audio packets buffered before the container was opened. */
    private static void flushAudioPackets() {
        if (!muxerStarted || audioTrack < 0) return;
        long byteCount = 0;
        long firstPresentationUs = -1;
        long lastPresentationUs = -1;
        final int packetCount = audioPackets.size();
        for (int i = 0; i < packetCount; ++i) {
            final byte[] packet = audioPackets.get(i);
            final MediaCodec.BufferInfo info = audioPacketInfo.get(i);
            final ByteBuffer buffer = ByteBuffer.wrap(packet);
            muxer.writeSampleData(audioTrack, buffer, info);
            byteCount += packet.length;
            if (firstPresentationUs < 0) firstPresentationUs = info.presentationTimeUs;
            lastPresentationUs = info.presentationTimeUs;
        }
        Log.i(TAG, "audio muxed track=" + audioTrack + " packets=" + packetCount
                + " bytes=" + byteCount + " firstPtsUs=" + firstPresentationUs
                + " lastPtsUs=" + lastPresentationUs);
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
    private static boolean prepareAudioTrack(int generation) {
        if (audioPrepared) return true;
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

            // The encode runs on a worker while the drawing thread stays free to call abort(), so
            // everything it produces is collected in locals and published in one step at the end.
            // Writing the shared fields directly would let a cancelled run leave half a track
            // behind that the next run then mixes with its own.
            final byte[] pcm = pcmBytes;
            final java.util.List<byte[]> packets = new java.util.ArrayList<>();
            final java.util.List<MediaCodec.BufferInfo> packetInfo = new java.util.ArrayList<>();
            MediaFormat producedFormat = null;
            int producedTrack = -1;

            final int floatBytes = 4;
            final int frameBytes = channels * floatBytes;
            final int totalFrames = pcm.length / frameBytes;
            final MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
            final ByteBuffer shorts = ByteBuffer.allocate(16384).order(ByteOrder.nativeOrder());
            ShortBuffer shortView = shorts.asShortBuffer();
            int cursor = 0;
            long presentedSamples = 0;
            long encodedBytes = 0;
            long firstPresentationUs = -1;
            long lastPresentationUs = -1;
            boolean inputDone = false;
            boolean sawEndOfStream = false;
            final long idleTimeoutNs = 10_000_000_000L;
            long lastProgressNs = System.nanoTime();

            while (true) {
                if (generation != audioGeneration) {
                    throw new IllegalStateException("the audio encode was cancelled");
                }
                if (!inputDone) {
                    final int index = encoder.dequeueInputBuffer(10_000L);
                    if (index >= 0) {
                        final ByteBuffer input = encoder.getInputBuffer(index);
                        if (input == null) {
                            throw new IllegalStateException("audio encoder returned a null input buffer");
                        }
                        input.clear();
                        shorts.clear();
                        shortView = shorts.asShortBuffer();
                        final int sampleCapacity =
                                Math.min(shortView.capacity(), input.remaining() / 2);
                        if (cursor < totalFrames && sampleCapacity < channels) {
                            throw new IllegalStateException("audio encoder input buffer is too small");
                        }
                        int samples = 0;
                        while (cursor < totalFrames && samples + channels <= sampleCapacity) {
                            final int base = cursor * frameBytes;
                            for (int c = 0; c < channels; ++c) {
                                final int offset = base + c * floatBytes;
                                final float value = Float.intBitsToFloat(
                                        (pcm[offset] & 0xFF)
                                                | ((pcm[offset + 1] & 0xFF) << 8)
                                                | ((pcm[offset + 2] & 0xFF) << 16)
                                                | ((pcm[offset + 3] & 0xFF) << 24));
                                final int scaled = (int) (Math.max(-1.0f, Math.min(1.0f, value)) * 32767.0f);
                                shorts.putShort((short) scaled);
                                samples++;
                            }
                            cursor++;
                        }
                        final int written = shorts.position();
                        input.put(shorts.array(), 0, written);
                        final long presentationUs = (pcmDelayMs * 1000L)
                                + presentedSamples * 1_000_000L / pcmSampleRate;
                        final boolean last = cursor >= totalFrames;
                        encoder.queueInputBuffer(index, 0, written, presentationUs,
                                last ? MediaCodec.BUFFER_FLAG_END_OF_STREAM : 0);
                        presentedSamples += written / (channels * 2L);
                        if (last) inputDone = true;
                        lastProgressNs = System.nanoTime();
                    }
                }
                final int index = encoder.dequeueOutputBuffer(info, 10_000L);
                if (index == MediaCodec.INFO_TRY_AGAIN_LATER) {
                    if (System.nanoTime() - lastProgressNs >= idleTimeoutNs) {
                        throw new IllegalStateException("audio encoder output timed out");
                    }
                    continue;
                }
                if (index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED) {
                    // The track is added now, while the muxer is still unstarted; its samples are
                    // held until the video track exists and the container has been opened.
                    if (muxerStarted || audioTrack >= 0) {
                        throw new IllegalStateException("audio track format changed too late");
                    }
                    // The muxer itself is only touched in the single locked step at the end: it is
                    // released from the drawing thread when a run is torn down, and calling into it
                    // from here would race that release.
                    producedFormat = encoder.getOutputFormat();
                    lastProgressNs = System.nanoTime();
                    continue;
                }
                if (index < 0) {
                    if (System.nanoTime() - lastProgressNs >= idleTimeoutNs) {
                        throw new IllegalStateException("audio encoder output timed out");
                    }
                    continue;
                }
                lastProgressNs = System.nanoTime();
                final ByteBuffer output = encoder.getOutputBuffer(index);
                if (output != null && info.size > 0
                        && (info.flags & MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0) {
                    final byte[] packet = new byte[info.size];
                    output.position(info.offset);
                    output.limit(info.offset + info.size);
                    output.get(packet);
                    final MediaCodec.BufferInfo copy = new MediaCodec.BufferInfo();
                    copy.set(0, packet.length, info.presentationTimeUs, info.flags);
                    packets.add(packet);
                    packetInfo.add(copy);
                    encodedBytes += packet.length;
                    if (firstPresentationUs < 0) firstPresentationUs = info.presentationTimeUs;
                    lastPresentationUs = info.presentationTimeUs;
                }
                encoder.releaseOutputBuffer(index, false);
                if ((info.flags & MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) {
                    sawEndOfStream = true;
                    break;
                }
            }
            if (!sawEndOfStream || producedFormat == null) {
                throw new IllegalStateException("audio encoder did not finish its track");
            }
            // Publishing is the only step that touches shared state, and it happens in one place:
            // a reader either sees the whole track or none of it. The track is registered with the
            // muxer here rather than while encoding, so the muxer is never called from two threads.
            synchronized (VideoEncoderBridge.class) {
                if (generation != audioGeneration) {
                    throw new IllegalStateException("the audio encode was cancelled");
                }
                audioFormat = producedFormat;
                audioTrack = muxer.addTrack(producedFormat);
                audioPackets.addAll(packets);
                audioPacketInfo.addAll(packetInfo);
                audioPrepared = true;
                producedTrack = audioTrack;
            }
            Log.i(TAG, "audio encoded pcmFrames=" + presentedSamples
                    + " packets=" + packets.size() + " bytes=" + encodedBytes
                    + " track=" + producedTrack + " firstPtsUs=" + firstPresentationUs
                    + " lastPtsUs=" + lastPresentationUs + " eos=" + sawEndOfStream);
            return true;
        } catch (IOException | RuntimeException error) {
            // A recording without music is still a complete recording, so this is not fatal.
            Log.e(TAG, "cannot prepare audio", error);
            return false;
        } finally {
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
        // Bumping the generation is what tells a worker still encoding that its result belongs to
        // a run that no longer exists. It is not joined: the encode can be minutes long, and the
        // caller of abort is the frame that has to keep drawing.
        audioGeneration++;
        audioThread = null;
        audioState = AUDIO_NONE;
        try {
            release();
        } catch (RuntimeException error) {
            Log.e(TAG, "cannot release encoder", error);
        }
        pcmBytes = null;
        audioPackets.clear();
        audioPacketInfo.clear();
        audioFormat = null;
        audioPrepared = false;
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
