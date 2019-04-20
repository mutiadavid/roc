/*
 * Copyright (c) 2017 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

//! @file roc_fec/composer.h
//! @brief FECFRAME packet composer.

#ifndef ROC_FEC_COMPOSER_H_
#define ROC_FEC_COMPOSER_H_

#include "roc_core/alignment.h"
#include "roc_core/log.h"
#include "roc_core/noncopyable.h"
#include "roc_fec/headers.h"
#include "roc_packet/icomposer.h"

namespace roc {
namespace fec {

//! FECFRAME packet composer.
template <class PayloadID, PayloadID_Type Type, PayloadID_Pos Pos>
class Composer : public packet::IComposer, public core::NonCopyable<> {
public:
    //! Initialization.
    //! @remarks
    //!  Composes FECFRAME header or footer and passes the rest to @p inner_composer
    //!  if it's not null.
    Composer(packet::IComposer* inner_composer)
        : inner_composer_(inner_composer) {
    }

    //! Adjust buffer to align payload.
    virtual bool
    align(core::Slice<uint8_t>& buffer, size_t header_size, size_t payload_alignment) {
        if (Pos == Header) {
            header_size += sizeof(PayloadID);
        }

        if (inner_composer_ == NULL) {
            const size_t padding = core::padding(header_size, payload_alignment);

            if (buffer.capacity() < padding) {
                roc_log(
                    LogDebug,
                    "fec composer: not enough space for alignment: padding=%lu cap=%lu",
                    (unsigned long)padding, (unsigned long)buffer.capacity());
                return false;
            }

            buffer.resize(padding);
            buffer = buffer.range(buffer.size(), buffer.size());

            return true;
        } else {
            return inner_composer_->align(buffer, header_size, payload_alignment);
        }
    }

    //! Prepare buffer for composing a packet.
    //!  capacity is not enough.
    virtual bool
    prepare(packet::Packet& packet, core::Slice<uint8_t>& buffer, size_t payload_size) {
        core::Slice<uint8_t> payload_id = buffer.range(0, 0);

        if (Pos == Header) {
            if (payload_id.capacity() < sizeof(PayloadID)) {
                roc_log(LogDebug,
                        "fec composer: not enough space for fec header: size=%lu cap=%lu",
                        (unsigned long)sizeof(PayloadID),
                        (unsigned long)payload_id.capacity());
                return false;
            }
            payload_id.resize(sizeof(PayloadID));
        }

        core::Slice<uint8_t> payload =
            payload_id.range(payload_id.size(), payload_id.size());

        if (inner_composer_) {
            if (!inner_composer_->prepare(packet, payload, payload_size)) {
                return false;
            }
        } else {
            payload.resize(payload_size);
        }

        if (Pos == Footer) {
            payload_id = payload.range(payload.size(), payload.size());

            if (payload_id.capacity() < sizeof(PayloadID)) {
                roc_log(LogDebug,
                        "fec composer: not enough space for fec header: size=%lu cap=%lu",
                        (unsigned long)sizeof(PayloadID),
                        (unsigned long)payload_id.capacity());
                return false;
            }
            payload_id.resize(sizeof(PayloadID));
        }

        if (Type == Repair) {
            packet.add_flags(packet::Packet::FlagRepair);
        }

        packet.add_flags(packet::Packet::FlagFEC);

        packet::FEC& fec = *packet.fec();

        fec.payload_id = payload_id;
        fec.payload = payload;

        buffer.resize(payload_id.size() + payload.size());

        return true;
    }

    //! Truncate packet payload.
    virtual bool truncate(packet::Packet& packet, size_t payload_size) {
        if (!packet.fec()) {
            roc_panic("rtp composer: unexpected non-rtp packet");
        }

        packet::FEC& fec = *packet.fec();

        if (fec.payload.size() < payload_size) {
            roc_log(LogDebug,
                    "fec composer: can't truncate packet to a larger size:"
                    " old_size=%lu new_size=%lu",
                    (unsigned long)fec.payload.size(), (unsigned long)payload_size);
            return false;
        }

        if (inner_composer_) {
            if (!inner_composer_->truncate(packet, payload_size)) {
                return false;
            }
        }

        fec.payload = fec.payload.range(0, payload_size);

        return true;
    }

    //! Compose packet to buffer.
    virtual bool compose(packet::Packet& packet) {
        if (!packet.fec()) {
            roc_panic("fec composer: unexpected non-fec packet");
        }

        if (packet.fec()->payload_id.size() != sizeof(PayloadID)) {
            roc_panic("fec composer: unexpected payload id size");
        }

        packet::FEC& fec = *packet.fec();

        PayloadID& payload_id = *(PayloadID*)fec.payload_id.data();

        payload_id.clear();
        payload_id.set_sbn(fec.source_block_number);

        roc_panic_if(fec.source_block_length >= (uint16_t)-1);
        payload_id.set_k((uint16_t)fec.source_block_length);

        roc_panic_if(fec.encoding_symbol_id >= (uint16_t)-1);
        payload_id.set_esi((uint16_t)fec.encoding_symbol_id);

        if (inner_composer_) {
            return inner_composer_->compose(packet);
        }

        return true;
    }

private:
    packet::IComposer* inner_composer_;
};

} // namespace fec
} // namespace roc

#endif // ROC_FEC_COMPOSER_H_
