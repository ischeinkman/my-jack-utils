#pragma once

#include <jack/types.h>
#include <math.h>
#include <string.h>

typedef jack_default_audio_sample_t Sample;

/**
 * A callback used to generate samples.
 * @param cur_frame the current time use in the generator, measured in frames since 0.
 * @param srate the number of frames in a second, used for time conversions.
 * @returns an audio sample between `-1.0` and `1.0`.
 */
typedef Sample SampleGenerator(size_t cur_frame, jack_nframes_t srate);

typedef struct {
    /// The pointer to the samples in this buffer.
    Sample* buffer;
    /// The length of the sample buffer, in number of samples.
    size_t buffer_len;
    /// The number of samples that a second of audio corresponds to in this buffer.
    jack_nframes_t srate;
} AudioBuffer;

/**
 * Constructs a new `AudioBuffer` from a sample list, the length of the list, and the samples per second.
 * @param buffer the pointer to the samples
 * @param buffer_len the number of samples in `buffer`
 * @param srate the number of samples that correspond to 1 second of audio
 * @returns an `AudioBuffer` wrapping the parameters
 */
static inline AudioBuffer AudioBuffer_new(Sample* buffer, size_t buffer_len, jack_nframes_t srate) {
    return (AudioBuffer){buffer, buffer_len, srate};
}

/**
 * Constructs a new `AudioBuffer` from a function generator by caching and looping a number of frames.
 * @param generator the function to use to pre-generate samples
 * @param buffer_len the number of frames to generate and cache
 * @param srate the number of samples that correspond to 1 second of audio
 * @returns an `AudioBuffer` filled with exactly `buffer_len` total samples calculated from `generator`
 */
static inline AudioBuffer AudioBuffer_from_cb(SampleGenerator* generator, size_t buffer_len, jack_nframes_t srate) {
    Sample* buffer = (Sample*) calloc(buffer_len, sizeof(Sample));
    for (size_t idx = 0; idx < buffer_len; idx++) {
        buffer[idx] = generator(idx, srate);
    }
    return (AudioBuffer){buffer, buffer_len, srate};
}

typedef struct {
    size_t cur_idx;
    jack_nframes_t cur_time;
} BufferCursor;

static inline BufferCursor BufferCursor_new(jack_nframes_t cur_time) { return (BufferCursor){cur_idx : 0, cur_time}; }

typedef struct {
    AudioBuffer buffer;
    BufferCursor cursor;
} AudioReader;

/**
 * Steps an `AudioReader` until its cursor reachs a clock time of `frames`.
 * If the cursor's internal clock is greater than `frames`, no action occurs.
 */
static inline void AudioReader_step_to(AudioReader* reader, jack_nframes_t frames) {
    if (frames <= reader->cursor.cur_time) {
        return;
    }
    jack_nframes_t diff = frames - reader->cursor.cur_time;
    reader->cursor.cur_time = frames;
    reader->cursor.cur_idx = (((size_t) diff) + reader->cursor.cur_idx) % reader->buffer.buffer_len;
}

/**
 * Steps an `AudioReader`'s cursor by a number of frames.
 */
static inline void AudioReader_step_by(AudioReader* reader, jack_nframes_t frames) {
    reader->cursor.cur_idx = (reader->cursor.cur_idx + (size_t) frames) % reader->buffer.buffer_len;
    reader->cursor.cur_time += frames;
}

/**
 * Gets the pointer into the audio buffer to the sample for the current frame.
 * @returns the audio buffer pointer offset by the cursor frame count, periodically
 */
static inline Sample* AudioReader_get_pointer(AudioReader* reader) {
    return reader->buffer.buffer + reader->cursor.cur_idx;
}

/**
 * Reads audio samples from an `AudioBuffer` into an output buffer.
 * The buffer is read in a periodic fashion; if the end of `buffer` is reached before the necessary frames
 * have been copied, `buffer` loops back to the beginning of the sample list and reads from their.
 *
 * @param buffer the buffer to read from
 * @param start_frames the clock time, in frames, that corresponds to the first sample of `output_buffer`
 * @param num_frames the number of frames to read into `output_buffer`
 * @param output_buffer the buffer to read into
 * @returns `0` on success, non-zero on error
 */
static inline int read_buffer(AudioReader* buffer, jack_nframes_t start_frames, jack_nframes_t num_frames,
                              Sample* output_buffer) {
    AudioReader_step_to(buffer, start_frames);

    size_t buffer_left = buffer->buffer.buffer_len - buffer->cursor.cur_idx;
    size_t first_copy_size = (buffer_left > (size_t) num_frames) ? (size_t) num_frames : buffer_left;
    memcpy(output_buffer, AudioReader_get_pointer(buffer), first_copy_size * sizeof(Sample));
    output_buffer += first_copy_size;
    AudioReader_step_by(buffer, (jack_nframes_t) first_copy_size);

    jack_nframes_t frames_left = num_frames - (jack_nframes_t) first_copy_size;
    while (frames_left > 0) {
        size_t cur_copy_size =
            ((size_t) frames_left > buffer->buffer.buffer_len) ? buffer->buffer.buffer_len : (size_t) frames_left;
        // We don't need AudioReader_get_pointer since if the buffer did not wrap around in the previous call, it would
        // break and not copy any data;
        memcpy(output_buffer, buffer->buffer.buffer, cur_copy_size * sizeof(Sample));
        AudioReader_step_by(buffer, (jack_nframes_t) cur_copy_size);
        output_buffer += cur_copy_size;
        frames_left -= (jack_nframes_t) cur_copy_size;
    }
    return 0;
}
