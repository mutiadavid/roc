/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_fec/target_openfec/roc_fec/of_encoder.h
//! @brief Encoder implementation using OpenFEC library.

#ifndef ROC_FEC_OF_ENCODER_H_
#define ROC_FEC_OF_ENCODER_H_

#include "roc_core/array.h"
#include "roc_core/buffer_pool.h"
#include "roc_core/iallocator.h"
#include "roc_core/noncopyable.h"
#include "roc_core/slice.h"
#include "roc_fec/config.h"
#include "roc_fec/iencoder.h"
#include "roc_packet/units.h"

extern "C" {
#include <of_openfec_api.h>
}

#ifndef OF_USE_ENCODER
#error "OF_USE_ENCODER undefined"
#endif

#ifndef OF_USE_LDPC_STAIRCASE_CODEC
#error "OF_USE_LDPC_STAIRCASE_CODEC undefined"
#endif

namespace roc {
namespace fec {

//! Encoder implementation using OpenFEC library.
class OFEncoder : public IEncoder, public core::NonCopyable<> {
public:
    //! Initialize.
    explicit OFEncoder(const Config& config,
                       size_t payload_size,
                       core::IAllocator& allocator);

    virtual ~OFEncoder();

    //! Check if object is successfully constructed.
    bool valid() const;

    //! Get buffer alignment requirement.
    virtual size_t alignment() const;

    //! Start block.
    //!
    //! @remarks
    //!  Perform an initial setup for a current block. Should be called as a
    //!  first encoder operation for the current block.
    virtual bool begin(size_t sblen, size_t rblen);

    //! Store packet data for current block.
    virtual void set(size_t index, const core::Slice<uint8_t>& buffer);

    //! Fill repair packets.
    virtual void fill();

    //! Finish block.
    //!
    //! @remarks
    //!  Cleanups the resources allocated for a current block. Should be called
    //!  as a last encoder operation for the current block.
    virtual void end();

private:
    void update_session_params_(size_t sblen, size_t rblen);
    bool resize_tabs_(size_t size);
    void reset_session_();

    enum { Alignment = 8 };

    size_t sblen_;
    size_t rblen_;

    of_session_t* of_sess_;
    of_parameters_t* of_sess_params_;

    of_codec_id_t codec_id_;
    union {
        of_ldpc_parameters ldpc_params_;
        of_rs_2_m_parameters_t rs_params_;
    } codec_params_;

    core::Array<core::Slice<uint8_t> > buff_tab_;
    core::Array<void*> data_tab_;

    bool valid_;
};

} // namespace fec
} // namespace roc

#endif // ROC_FEC_OF_ENCODER_H_
