#pragma once

#include <signal.h>

static sig_atomic_t prev_signal;

static void on_signal(int sig) { prev_signal = sig; }

static inline void register_handler(int sig) {
    prev_signal = 0;
    signal(sig, on_signal);
}

static inline void register_all_handlers() {
    register_handler(SIGTERM);
    register_handler(SIGINT);
}

/**
 * Re-raises a previously captured signal using the default handler.
 * @returns 0 on success, -1 if no signal has been captured yet, or non-zero if `raise()` returns an error.
 */
static inline int reraise() {
    if (prev_signal == 0) {
        return -1;
    }
    signal(prev_signal, SIG_DFL);
    int res = raise(prev_signal);
    register_handler(prev_signal);
    return res;
}
