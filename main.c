#include <jack/jack.h>

// for printing
#include <stdio.h>
// for `malloc()` and `free()`
#include <stdlib.h>

// for CTRL-C responding.
#include <signal.h>

// for the waveform
#include <tgmath.h>

#include <float.h>

// for `sleep()`
#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#include "sigwrapping.h"

#define PI (3.14159274101257324f)

typedef jack_default_audio_sample_t Sample;

typedef struct AppState {
    jack_time_t start_usecs;
    jack_client_t* client;
    jack_port_t* output_port;
} AppState;

Sample waveform(jack_time_t usecs) {
    jack_time_t freq = 440;                     // waves/sec
    jack_time_t seconds_frac = usecs % 1000000; // microseconds into current second
    jack_time_t fraced = seconds_frac * freq;   // fraction into the current wave number * 10**6
    return (Sample)(0.05 * sin(2.0 * PI * ((float) fraced) / (1000000.0)));
}

int process(jack_nframes_t n_frames, void* arg) {
    int res;
    AppState* state = (AppState*) arg;
    if (state == NULL) {
        return -1;
    }
    Sample* buffer = (Sample*) jack_port_get_buffer(state->output_port, n_frames);
    if (buffer == NULL) {
        return -2;
    }
    jack_nframes_t cur_frames;
    jack_time_t cur_usecs;
    jack_time_t nxt_usecs;
    float period_usecs;
    res = jack_get_cycle_times(state->client, &cur_frames, &cur_usecs, &nxt_usecs, &period_usecs);
    if (res != 0) {
        return res;
    }

    jack_time_t buffer_usecs = nxt_usecs - cur_usecs;
    double usecs_per_frame = ((double) buffer_usecs) / ((double) n_frames);
    for (uint64_t idx = 0; idx < n_frames; idx++) {
        jack_time_t idx_usecs = cur_usecs + (jack_time_t)((double) idx * usecs_per_frame);
        buffer[idx] = waveform(idx_usecs);
    }

    return res;
}

int main() {
    int res = 0;

    jack_status_t status;
    jack_client_t* client = jack_client_open("testclient", JackNoStartServer, &status);
    if (client == NULL) {
        fprintf(stderr, "Could not open jack client. Status: 0x%x\n", status);
        return -1;
    }

    jack_port_t* output_port =
        jack_port_register(client, "output_1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput | JackPortIsTerminal, 0);
    if (output_port == NULL) {
        fprintf(stderr, "Could not open jack port.\n");
        jack_client_close(client);
        return -1;
    }

    AppState* state_ptr = (AppState*) malloc(sizeof(AppState));
    if (state_ptr == NULL) {
        fprintf(stderr, "Could not allocate state.\n");
        jack_port_unregister(client, output_port);
        jack_client_close(client);
        return -1;
    }
    state_ptr->client = client;
    state_ptr->output_port = output_port;
    state_ptr->start_usecs = jack_get_time();

    res = jack_set_process_callback(client, process, state_ptr);
    if (res != 0) {
        fprintf(stderr, "Failed setting process callback. Error code: 0x%x\n", res);
        free(state_ptr);
        jack_port_unregister(client, output_port);
        jack_client_close(client);
        return -1;
    }

    res = jack_activate(client);
    if (res != 0) {
        fprintf(stderr, "Failed activating client. Error code: 0x%x\n", res);
        free(state_ptr);
        jack_port_unregister(client, output_port);
        jack_client_close(client);
        return -1;
    }
    register_all_handlers();
    sleep(-1);
    printf("Exiting with current code: %d.\n", res);
    free(state_ptr);
    jack_port_unregister(client, output_port);
    jack_client_close(client);
    return reraise();
}