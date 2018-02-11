/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_sndio/target_sox/roc_sndio/sox_writer.h
//! @brief SoX audio writer.

#ifndef ROC_SNDIO_SOX_WRITER_H_
#define ROC_SNDIO_SOX_WRITER_H_

#include <sox.h>

#include "roc_core/iallocator.h"
#include "roc_core/noncopyable.h"
#include "roc_core/stddefs.h"
#include "roc_core/unique_ptr.h"
#include "roc_packet/units.h"
#include "roc_sndio/iwriter.h"

namespace roc {
namespace sndio {

//! SoX audio writer.
//! @remarks
//!  Encodes samples them and and writes to output file or audio driver.
class SoxWriter : public IWriter, public core::NonCopyable<> {
public:
    //! Initialize.
    SoxWriter(core::IAllocator& allocator,
              packet::channel_mask_t channels,
              size_t sample_rate);

    virtual ~SoxWriter();

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
    bool prepare_();
    bool open_(const char* name, const char* type);
    void write_(const sox_sample_t* samples, size_t n_samples);
    void close_();

    sox_format_t* output_;
    sox_signalinfo_t out_signal_;

    core::IAllocator& allocator_;

    core::UniquePtr<sox_sample_t> buffer_;
    size_t buffer_size_;

    bool is_file_;
};

} // namespace sndio
} // namespace roc

#endif // ROC_SNDIO_SOX_WRITER_H_
