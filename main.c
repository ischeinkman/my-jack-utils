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

#include "audio_framebuffer.h"
#include "sigwrapping.h"

#define PI (3.14159274101257324f)

typedef jack_default_audio_sample_t Sample;

typedef struct AppState {
    jack_time_t start_usecs;
    jack_client_t* client;
    jack_port_t* output_port;
    AudioReader buff;
} AppState;

Sample waveform_generator(size_t cur_frame, jack_nframes_t sample_rate) {
    size_t goal_frq = 440;
    size_t samples_into_wave = ((cur_frame % sample_rate) * goal_frq) % sample_rate;
    Sample raw_wave = sin(2.0 * PI * ((float)samples_into_wave) / ((float) sample_rate) );
    return 0.005 * raw_wave;
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
    return read_buffer(&state->buff, cur_frames, n_frames, buffer);
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
    AudioBuffer buff =
        AudioBuffer_from_cb(&waveform_generator, (size_t) jack_get_sample_rate(client), jack_get_sample_rate(client));
    BufferCursor cursor = BufferCursor_new(jack_frame_time(client));

    state_ptr->client = client;
    state_ptr->output_port = output_port;
    state_ptr->start_usecs = jack_get_time();
    state_ptr->buff = (AudioReader){buff, cursor};

    res = jack_set_process_callback(client, process, state_ptr);
    if (res != 0) {
        fprintf(stderr, "Failed setting process callback. Error code: 0x%x\n", res);
        jack_port_unregister(client, output_port);
        jack_client_close(client);
        free(state_ptr);
        return -1;
    }

    res = jack_activate(client);
    if (res != 0) {
        fprintf(stderr, "Failed activating client. Error code: 0x%x\n", res);
        jack_port_unregister(client, output_port);
        jack_client_close(client);
        free(state_ptr);
        return -1;
    }
    register_all_handlers();
    sleep(-1);
    printf("Exiting with current code: %d.\n", res);
    jack_port_unregister(client, output_port);
    jack_client_close(client);
    free(state_ptr);
    return reraise();
}