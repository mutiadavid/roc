/*
 * Copyright (c) 2018 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_sndio/iwriter.h
//! @brief Audio writer.

#ifndef ROC_SNDIO_IWRITER_H_
#define ROC_SNDIO_IWRITER_H_

#include "roc_audio/iwriter.h"
#include "roc_core/stddefs.h"

namespace roc {
namespace sndio {

//! Audio writer.
class IWriter : public audio::IWriter {
public:
    virtual ~IWriter();

    //! Open output file or device.
    //!
    //! @b Parameters
    //!  - @p name is output file or device name, "-" for stdout.
    //!  - @p type is codec or driver name.
    //!
    //! @remarks
    //!  If @p name or @p type are NULL, they're autodetected.
    virtual bool open(const char* name, const char* type) = 0;

    //! Close output file or device.
    //!
    //! @pre
    //!  Should be opened.
    virtual void close() = 0;

    //! Returns recommended frame size.
    //!
    //! @pre
    //!  Should be opened.
    virtual size_t frame_size() const = 0;

    //! Get sample rate of an output file or a device.
    //!
    //! @pre
    //!  Should be opened.
    virtual size_t sample_rate() const = 0;

    //! Returns true if output is a real file.
    //!
    //! @pre
    //!  Should be opened.
    virtual bool is_file() const = 0;
};

} // namespace sndio
} // namespace roc

#endif // ROC_SNDIO_IWRITER_H_
