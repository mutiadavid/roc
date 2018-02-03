/*
 * Copyright (c) 2018 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <string.h>

#include "roc_sndio/pulse_writer.h"
#include "roc_core/log.h"
#include "roc_core/panic.h"

namespace roc {
namespace sndio {

PulseWriter::PulseWriter(packet::channel_mask_t channels, size_t sample_rate)
    : device_(NULL)
    , sample_rate_(sample_rate)
    , num_channels_(packet::num_channels(channels))
    , open_done_(false)
    , opened_(false)
    , mainloop_(NULL)
    , context_(NULL)
    , stream_(NULL)
    , operation_(NULL) {
}

PulseWriter::~PulseWriter() {
    close();
}

bool PulseWriter::open(const char* name, const char* type) {
    if (mainloop_) {
        roc_panic("pulse writer: can't call open() twice");
    }

    if (type && *type && strcmp(type, "pulseaudio") != 0) {
        roc_panic("pulse writer: unexpected type, expected empty or pulseaudio, got %s",
                  type);
    }

    roc_log(LogDebug, "pulse writer: opening writer");

    if (!start_()) {
        return false;
    }

    if (!open_(name)) {
        return false;
    }

    return true;
}

void PulseWriter::close() {
    if (!mainloop_) {
        return;
    }

    roc_log(LogDebug, "pulse writer: closing writer");

    pa_threaded_mainloop_lock(mainloop_);

    close_operation_();
    close_stream_();
    close_context_();

    pa_threaded_mainloop_unlock(mainloop_);

    pa_threaded_mainloop_stop(mainloop_);
    pa_threaded_mainloop_free(mainloop_);

    mainloop_ = NULL;
}

size_t PulseWriter::frame_size() const {
    check_started_();

    // FIXME: return latency
    return 1024;
}

size_t PulseWriter::sample_rate() const {
    check_started_();

    pa_threaded_mainloop_lock(mainloop_);

    check_opened_();

    const size_t ret = sample_rate_;

    pa_threaded_mainloop_unlock(mainloop_);

    return ret;
}

bool PulseWriter::is_file() const {
    check_started_();

    return false;
}

void PulseWriter::write(audio::Frame& frame) {
    check_started_();

    const audio::sample_t* data = frame.data();
    size_t size = frame.size();

    while (size > 0) {
        const size_t ret = write_stream_(data, size);
        data += ret;
        size -= ret;
    }
}

void PulseWriter::check_started_() const {
    if (!mainloop_) {
        roc_panic("pulse writer: can't use unopened writer");
    }
}

void PulseWriter::check_opened_() const {
    if (!opened_) {
        roc_panic("pulse writer: can't use unopened writer");
    }
}

void PulseWriter::set_opened_(bool opened) {
    if (opened) {
        roc_log(LogTrace, "pulse writer: successfully opened writer");
    } else {
        roc_log(LogError, "pulse writer: failed to open writer");
    }

    open_done_ = true;
    opened_ = opened;

    pa_threaded_mainloop_signal(mainloop_, 0);
}

bool PulseWriter::start_() {
    mainloop_ = pa_threaded_mainloop_new();
    if (!mainloop_) {
        roc_log(LogError, "pulse writer: pa_threaded_mainloop_new() failed");
        return false;
    }

    if (int err = pa_threaded_mainloop_start(mainloop_)) {
        roc_log(LogError, "pulse writer: pa_threaded_mainloop_start(): %s", pa_strerror(err));
        return false;
    }

    return true;
}

bool PulseWriter::open_(const char* device) {
    pa_threaded_mainloop_lock(mainloop_);

    device_ = device;

    if (open_context_()) {
        while (!open_done_) {
            pa_threaded_mainloop_wait(mainloop_);
        }
    }

    const bool ret = opened_;

    pa_threaded_mainloop_unlock(mainloop_);

    return ret;
}

bool PulseWriter::open_context_() {
    roc_log(LogTrace, "pulse writer: opening context");

    context_ = pa_context_new(pa_threaded_mainloop_get_api(mainloop_), "Roc");
    if (!context_) {
        roc_log(LogError, "pulse writer: pa_context_new() failed");
        return false;
    }

    pa_context_set_state_callback(context_, context_state_cb_, this);

    if (int err = pa_context_connect(context_, NULL, PA_CONTEXT_NOFLAGS, NULL)) {
        roc_log(LogError, "pulse writer: pa_context_connect(): %s", pa_strerror(err));
        return false;
    }

    return true;
}

void PulseWriter::close_context_() {
    if (!context_) {
        return;
    }

    roc_log(LogTrace, "pulse writer: closing context");

    pa_context_disconnect(context_);
    pa_context_unref(context_);

    context_ = NULL;
}

bool PulseWriter::open_stream_() {
    roc_panic_if_not(context_);

    roc_log(LogDebug,
            "pulse writer: opening stream: device=%s n_channels=%lu sample_rate=%lu",
            device_, (unsigned long)num_channels_, (unsigned long)sample_rate_);

    stream_ = pa_stream_new(context_, "Roc", &sample_spec_, NULL);
    if (!stream_) {
        roc_log(LogError, "pulse writer: pa_stream_new(): %s",
                pa_strerror(pa_context_errno(context_)));
        return false;
    }

    const pa_stream_flags_t flags = PA_STREAM_ADJUST_LATENCY;

    pa_stream_set_state_callback(stream_, stream_state_cb_, this);
    pa_stream_set_write_callback(stream_, stream_write_cb_, this);

    int err =
        pa_stream_connect_playback(stream_, device_, &buffer_attrs_, flags, NULL, NULL);

    if (err != 0) {
        roc_log(LogError, "pulse writer: pa_stream_connect_playback(): %s",
                pa_strerror(err));
        return false;
    }

    return true;
}

void PulseWriter::close_stream_() {
    if (!stream_) {
        return;
    }

    roc_log(LogTrace, "pulse writer: closing stream");

    pa_stream_disconnect(stream_);
    pa_stream_unref(stream_);

    stream_ = NULL;
}

size_t PulseWriter::write_stream_(const audio::sample_t* data, size_t size) {
    pa_threaded_mainloop_lock(mainloop_);

    check_opened_();

    size_t writable_size;

    for (;;) {
        writable_size = pa_stream_writable_size(stream_);

        roc_log(LogTrace, "pulse writer: write: requested_size=%lu writable_size=%lu",
                (unsigned long)size, (unsigned long)writable_size);

        if (writable_size != 0) {
            break;
        }

        pa_threaded_mainloop_wait(mainloop_);
    }

    if (size > writable_size) {
        size = writable_size;
    }

    int err = pa_stream_write(stream_, data, size * sizeof(audio::sample_t), NULL, 0,
                              PA_SEEK_RELATIVE);

    if (err != 0) {
        roc_log(LogError, "pulse writer: pa_stream_write(): %s", pa_strerror(err));
    }

    pa_threaded_mainloop_unlock(mainloop_);

    return size;
}

bool PulseWriter::request_sink_info_() {
    roc_log(LogTrace, "pulse writer: requesting sink info");

    operation_ = pa_context_get_sink_info_by_name(context_, device_, sink_info_cb_, this);

    return operation_;
}

void PulseWriter::close_operation_() {
    if (!operation_) {
        return;
    }

    pa_operation_cancel(operation_);
    pa_operation_unref(operation_);

    operation_ = NULL;
}

void PulseWriter::init_params_(const pa_sink_info& info) {
    if (sample_rate_ == 0) {
        sample_rate_ = (size_t)info.sample_spec.rate;
    }

    sample_spec_.format = PA_SAMPLE_FLOAT32LE;
    sample_spec_.rate = (uint32_t)sample_rate_;
    sample_spec_.channels = (uint8_t)num_channels_;

    buffer_attrs_.maxlength = (uint32_t)-1;
    buffer_attrs_.tlength = (uint32_t)-1;
    buffer_attrs_.prebuf = (uint32_t)-1;
    buffer_attrs_.minreq = (uint32_t)-1;
    buffer_attrs_.fragsize = (uint32_t)-1;
}

void PulseWriter::context_state_cb_(pa_context* context, void* userdata) {
    PulseWriter& self = *(PulseWriter*)userdata;

    if (self.opened_) {
        return;
    }

    const pa_context_state_t state = pa_context_get_state(context);

    switch ((unsigned)state) {
    case PA_CONTEXT_READY:
        roc_log(LogTrace, "pulse writer: successfully opened context");

        if (!self.request_sink_info_()) {
            self.set_opened_(false);
        }

        break;

    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        roc_log(LogError, "pulse writer: failed to open context");

        self.set_opened_(false);
        break;

    default:
        break;
    }
}

void PulseWriter::stream_state_cb_(pa_stream* stream, void* userdata) {
    PulseWriter& self = *(PulseWriter*)userdata;

    if (self.opened_) {
        return;
    }

    const pa_stream_state_t state = pa_stream_get_state(stream);

    switch ((unsigned)state) {
    case PA_STREAM_READY:
        roc_log(LogTrace, "pulse writer: successfully opened stream");

        self.set_opened_(true);
        break;

    case PA_STREAM_FAILED:
    case PA_STREAM_TERMINATED:
        roc_log(LogError, "pulse writer: failed to open stream");

        self.set_opened_(false);
        break;

    default:
        break;
    }
}

void PulseWriter::stream_write_cb_(pa_stream*, size_t length, void* userdata) {
    PulseWriter& self = *(PulseWriter*)userdata;

    if (length != 0) {
        pa_threaded_mainloop_signal(self.mainloop_, 0);
    }
}

void PulseWriter::sink_info_cb_(pa_context*,
                                const pa_sink_info* info,
                                int,
                                void* userdata) {
    PulseWriter& self = *(PulseWriter*)userdata;

    roc_log(LogTrace, "pulse writer: successfully retrieved sink info");

    self.close_operation_();

    self.init_params_(*info);

    if (!self.open_stream_()) {
        self.set_opened_(false);
    }
}

} // namespace sndio
} // namespace roc
