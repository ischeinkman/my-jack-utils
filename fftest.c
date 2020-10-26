#include <jack/jack.h>
#include <jack/ringbuffer.h>

// for printing
#include <stdio.h>
// for `malloc()` and `free()`
#include <stdint.h>
#include <stdlib.h>

#include <complex.h>
#include <math.h>
#include <tgmath.h>

// for `sleep()`
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "sigwrapping.h"

#define PI (3.14159274101257324f)

typedef jack_default_audio_sample_t Sample;
typedef complex float Complex;

typedef struct AppState {
    jack_client_t* client;
    jack_port_t* input_port;
    jack_ringbuffer_t* sample_buffer;
} AppState;

int process(jack_nframes_t n_frames, void* arg) {
    int res = 0;
    AppState* state = (AppState*) arg;
    if (state == NULL) {
        return -1;
    }
    Sample* buffer = (Sample*) jack_port_get_buffer(state->input_port, n_frames);
    if (buffer == NULL) {
        return -2;
    }
    size_t buffer_byte_len = ((size_t) n_frames) * sizeof(Sample);

    if (jack_ringbuffer_write_space(state->sample_buffer) >= buffer_byte_len) {
        jack_ringbuffer_write(state->sample_buffer, (char*) buffer, buffer_byte_len);
    }
    return res;
}

inline float midi_to_frequency(uint8_t note) {
    /// Midi notes range from 0-127, with each number being a half-step from the previous
    /// and A4 being located at midi note 69.
    /// Each half-step scales a frequency from F to F * 2**(1/12), so that a full 12-note octave doubles
    /// the frequency.
    /// Using A4 = 440 Hz tuning, the formula is then f(n) = 440 * 2**((n - 69)/12).

#ifndef A4_tuning
#define A4_tuning (440.0)
#endif

    float expn = (((float) note) - 69.0) / 12.0;
    return A4_tuning * exp2(expn);
}

// Frequency Amplitude(f) = sum(n, s[n] * exp(-2 * i pi * t[n] * f))

Complex frequency_dot_prod(float frequency, Sample* buffer, size_t buffer_len, jack_nframes_t srate) {
    Complex retvl = 0.0;
    for (size_t idx = 0; idx < buffer_len; idx++) {
        Sample cur = buffer[idx];
        Complex freq_pn = I * -2.0 * PI / ((float) srate) * ((float) idx) * frequency;
        Complex coeff = cexp(freq_pn);
        retvl += coeff * (Complex) cur;
    }
    return retvl;
}

int main() {
    int res = 0;

    jack_status_t status;
    jack_client_t* client = jack_client_open("testclient", JackNoStartServer, &status);
    if (client == NULL) {
        fprintf(stderr, "Could not open jack client. Status: 0x%x\n", status);
        return -1;
    }

    jack_port_t* input_port = jack_port_register(client, "input_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    if (input_port == NULL) {
        fprintf(stderr, "Could not open jack port.\n");
        jack_client_close(client);
        return -1;
    }

    jack_nframes_t sample_rate = jack_get_sample_rate(client);
    size_t buffer_len = 4 * sample_rate; // 4 seconds for MAXIMUM POSSIBLE PADDING
    jack_ringbuffer_t* sample_buffer = jack_ringbuffer_create(buffer_len * sizeof(Sample));
    if (sample_buffer == NULL) {
        fprintf(stderr, "Could not allocate sample buffer.");
        jack_port_unregister(client, input_port);
        jack_client_close(client);
        return -1;
    }

    AppState* state_ptr = (AppState*) malloc(sizeof(AppState));
    if (state_ptr == NULL) {
        fprintf(stderr, "Could not allocate state.\n");
        jack_ringbuffer_free(sample_buffer);
        jack_port_unregister(client, input_port);
        jack_client_close(client);
        return -1;
    }

    state_ptr->client = client;
    state_ptr->input_port = input_port;
    state_ptr->sample_buffer = sample_buffer;

    res = jack_set_process_callback(client, process, state_ptr);
    if (res != 0) {
        fprintf(stderr, "Failed setting process callback. Error code: 0x%x\n", res);
        jack_ringbuffer_free(sample_buffer);
        jack_port_unregister(client, input_port);
        jack_client_close(client);
        free(state_ptr);
        return -1;
    }

    res = jack_activate(client);
    if (res != 0) {
        fprintf(stderr, "Failed activating client. Error code: 0x%x\n", res);
        jack_ringbuffer_free(sample_buffer);
        jack_port_unregister(client, input_port);
        jack_client_close(client);
        free(state_ptr);
        return -1;
    }
    register_all_handlers();
    // Goal is approximately 1 window = 10 ms; then [windows/second] = [1000 ms/second] * [1 window/10 ms] = [100
    // windows]/[second], and [frames/window] = [srate frames]/[1 second] * [1 second]/[100 windows = (srate/100)
    // [frames/window]
    size_t window_frame_len = sample_rate / 10;
    Sample* local_sample_buffer = (Sample*) calloc(window_frame_len, sizeof(Sample));
    Complex* freq_buffer = (Complex*) calloc(128, sizeof(Complex));
    while (!has_signal()) {
        if (jack_ringbuffer_read_space(sample_buffer) <= window_frame_len * sizeof(Sample)) {
            continue;
        }
        jack_ringbuffer_read(sample_buffer, (char*) local_sample_buffer, window_frame_len * sizeof(Sample));
        uint8_t mxidx = 0;
        float mxabs = 0.0;
        for (uint8_t n = 0; n < 128; n++) {
            float frq = midi_to_frequency(n);
            Complex coeff = frequency_dot_prod(frq, local_sample_buffer, window_frame_len, sample_rate);
            freq_buffer[n] = coeff;
            if (cabs(coeff) > mxabs) {
                mxabs = cabs(coeff);
                mxidx = n;
            }
        }
        printf("\033c");
        for(uint8_t n = 0 ; n < 128 ; n ++) {
            if(n == mxidx) {
                printf("\033[41m");
            }
            else {
                printf("\033[0m");

            }
            printf("%3u => %.2e", n, cabs(freq_buffer[n]));
            if(n % 4 != 3) {
                printf("\t");
            }
            else {
                printf("\n");
            }
        }
    }
    printf("Exiting with current code: %d.\n", res);
    jack_client_close(client);
    jack_ringbuffer_free(sample_buffer);
    free(state_ptr);
    free(local_sample_buffer);
    free(freq_buffer);
    if (res != 0) {
        return res;
    }
    return reraise();
}