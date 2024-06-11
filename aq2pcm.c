/* this implements a hard-to-soft-realtime interconnect between an AudioQueue input and
 stdout using a large ring buffer and a C11 atomic, such that no syscalls are made within
 the hard-rt code */
#include <AudioToolbox/AudioQueue.h>

#define ERROR_ANSI "\x1B[31;1merror:\x1B[0m"
#define NOPE(...) do { fprintf(stderr, ERROR_ANSI " " __VA_ARGS__); exit(EXIT_FAILURE); } while(0)

struct context {
    /* only read and written by the hard-rt callback */
    unsigned long written_private;

    /* written by the hard-rt callback, read by soft-rt */
    _Atomic unsigned long writer_cursor;

    size_t size;
    unsigned char * ring_buffer;
};

static void callback(void * ctxv, struct OpaqueAudioQueue * aq, AudioQueueBuffer * buffer,
                     const AudioTimeStamp * in_start_time, uint32_t in_num_packets,
                     const AudioStreamPacketDescription * inPacketDesc) {
    (void)in_start_time;
    (void)in_num_packets;
    (void)inPacketDesc;
    struct context * const ctx = ctxv;

    size_t bytes_remaining = buffer->mAudioDataByteSize;
    const unsigned char * restrict src = buffer->mAudioData;

    const unsigned long writer_cursor_prior = ctx->writer_cursor;
    unsigned char * restrict dst = ctx->ring_buffer + (writer_cursor_prior % ctx->size);
    const size_t bytes_until_wraparound = (ctx->ring_buffer + ctx->size) - dst;

    /* if we are near the end of the ring buffer, do the memcpy in two steps */
    if (bytes_until_wraparound < buffer->mAudioDataByteSize) {
        memcpy(dst, src, bytes_until_wraparound);
        dst = ctx->ring_buffer;
        bytes_remaining -= bytes_until_wraparound;
        src += bytes_until_wraparound;
    }

    memcpy(dst, src, bytes_remaining);

    ctx->writer_cursor = writer_cursor_prior + buffer->mAudioDataByteSize;

    AudioQueueEnqueueBuffer(aq, buffer, 0, NULL);
}

int main(const int argc, char ** const argv) {
    size_t C = 1;
    double fs = 11025;

    for (size_t iarg = 2; iarg < (size_t)argc; iarg += 2) {
        if (!strcmp(argv[iarg - 1], "fs")) fs = strtod(argv[iarg], NULL);
        else if (!strcmp(argv[iarg - 1], "C")) C = strtoul(argv[iarg], NULL, 10);
        else NOPE("%s: %s %s: argument unrecognized\n", argv[0], argv[iarg - 1], argv[iarg]);
    }

    struct context * const ctx = &(struct context) {
        .size = 524288,
        .ring_buffer = malloc(524288)
    };

    struct OpaqueAudioQueue * queue;
    if (AudioQueueNewInput(&(AudioStreamBasicDescription) {
        .mFormatID = kAudioFormatLinearPCM,
        .mSampleRate = fs,
        .mFormatFlags = kLinearPCMFormatFlagIsSignedInteger,
        .mBitsPerChannel = sizeof(int16_t) * 8,
        .mChannelsPerFrame = (unsigned int)C,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = sizeof(int16_t) * C,
        .mBytesPerPacket = sizeof(int16_t) * C * 1,
    }, callback, ctx, NULL, NULL, 0, &queue))
        NOPE("%s: AudioQueueNewInput(): %s\n", argv[0], strerror(errno));

    const size_t number_of_buffers = 3;
    const size_t buffer_size_in_bytes = sizeof(int16_t) * C * 1024;

    for (size_t ibuffer = 0; ibuffer < number_of_buffers; ibuffer++) {
        AudioQueueBuffer * buffer;
        if (AudioQueueAllocateBuffer(queue, (unsigned int)buffer_size_in_bytes, &buffer))
            NOPE("%s: AudioQueueAllocateBuffer(): %s\n", argv[0], strerror(errno));
        if (AudioQueueEnqueueBuffer(queue, buffer, 0, NULL))
            NOPE("%s: AudioQueueEnqueueBuffer(): %s\n", argv[0], strerror(errno));
    }

    /* start the hard-realtime code */
    AudioQueueStart(queue, NULL);

    /* loop in softer-realtime which reads from the ring buffer and fwrites to stdout */
    for (unsigned long reader_cursor = 0;;) {
        /* sleep until there is new data in the ring buffer */
        unsigned long writer_cursor_now;
        while (reader_cursor == (writer_cursor_now = ctx->writer_cursor)) usleep(100000);

        const unsigned char * slot = ctx->ring_buffer + (reader_cursor % ctx->size);
        const size_t contiguous = (ctx->ring_buffer + ctx->size) - slot;
        const size_t remaining = writer_cursor_now - reader_cursor;
        const size_t bytes_to_write = contiguous < remaining ? contiguous : remaining;

        const ssize_t ret = write(STDOUT_FILENO, slot, bytes_to_write);
        if (-1 == ret) break;

        reader_cursor += ret;
    }

    free(ctx->ring_buffer);
}
