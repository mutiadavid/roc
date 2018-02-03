/*
 * Copyright (c) 2018 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_sndio/target_pulseaudio/roc_sndio/pulse_writer.h
//! @brief PulseAudio writer.

#ifndef ROC_SNDIO_PULSE_WRITER_H_
#define ROC_SNDIO_PULSE_WRITER_H_

#include <pulse/pulseaudio.h>

#include "roc_core/noncopyable.h"
#include "roc_core/stddefs.h"
#include "roc_packet/units.h"
#include "roc_sndio/iwriter.h"

namespace roc {
namespace sndio {

//! PulseAudio writer.
//! @remarks
//!  Encodes samples them and and writes to PulseAudio.
class PulseWriter : public IWriter, public core::NonCopyable<> {
public:
    //! Initialize.
    PulseWriter(packet::channel_mask_t channels, size_t sample_rate);

    ~PulseWriter();

    //! Open output file or device.
    virtual bool open(const char* name, const char* type);

    //! Close output file or device.
    virtual void close();

    //! Returns recommended frame size.
    virtual size_t frame_size() const;

    //! Get sample rate of an output file or a device.
    virtual size_t sample_rate() const;

    //! Returns true if output is a real file.
    virtual bool is_file() const;

    //! Write audio frame.
    virtual void write(audio::Frame& frame);

private:
    static void context_state_cb_(pa_context* context, void* userdata);

    static void stream_state_cb_(pa_stream* stream, void* userdata);
    static void stream_write_cb_(pa_stream* stream, size_t length, void* userdata);

    static void
    sink_info_cb_(pa_context* context, const pa_sink_info* info, int eol, void* userdata);

    void check_started_() const;
    void check_opened_() const;

    void set_opened_(bool opened);

    bool start_();
    bool open_(const char* device);

    bool open_context_();
    void close_context_();

    bool open_stream_();
    void close_stream_();
    size_t write_stream_(const audio::sample_t* data, size_t size);

    bool request_sink_info_();
    void close_operation_();

    void init_params_(const pa_sink_info& info);

    const char* device_;
    size_t sample_rate_;
    const size_t num_channels_;

    bool open_done_;
    bool opened_;

    pa_threaded_mainloop* mainloop_;
    pa_context* context_;
    pa_stream* stream_;
    pa_operation* operation_;

    pa_sample_spec sample_spec_;
    pa_buffer_attr buffer_attrs_;
};

} // namespace sndio
} // namespace roc

#endif // ROC_SNDIO_PULSE_WRITER_H_
