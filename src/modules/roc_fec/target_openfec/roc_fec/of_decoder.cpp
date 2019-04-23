/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdlib.h>

extern "C" {
#include <of_mem.h>
}

#include "roc_core/log.h"
#include "roc_core/panic.h"
#include "roc_fec/of_decoder.h"

namespace roc {
namespace fec {

OFDecoder::OFDecoder(const Config& config,
                     size_t payload_size,
                     core::BufferPool<uint8_t>& buffer_pool,
                     core::IAllocator& allocator)
    : sblen_(0)
    , rblen_(0)
    , payload_size_(payload_size)
    , of_sess_(NULL)
    , of_sess_params_(NULL)
    , buffer_pool_(buffer_pool)
    , buff_tab_(allocator)
    , data_tab_(allocator)
    , recv_tab_(allocator)
    , status_(allocator)
    , has_new_packets_(false)
    , decoding_finished_(false)
    , valid_(false) {
    if (config.codec == ReedSolomon8m) {
        roc_log(LogDebug, "of decoder: initializing Reed-Solomon decoder");

        codec_id_ = OF_CODEC_REED_SOLOMON_GF_2_M_STABLE;
        codec_params_.rs_params_.m = config.rs_m;

        of_sess_params_ = (of_parameters_t*)&codec_params_.rs_params_;
    } else if (config.codec == LDPCStaircase) {
        roc_log(LogDebug, "of decoder: initializing LDPC decoder");

        codec_id_ = OF_CODEC_LDPC_STAIRCASE_STABLE;
        codec_params_.ldpc_params_.prng_seed = config.ldpc_prng_seed;
        codec_params_.ldpc_params_.N1 = config.ldpc_N1;

        of_sess_params_ = (of_parameters_t*)&codec_params_.ldpc_params_;
    } else {
        roc_panic("of decoder: invalid codec");
    }

    of_sess_params_->encoding_symbol_length = (uint32_t)payload_size_;
    of_verbosity = 0;

    valid_ = true;
}

OFDecoder::~OFDecoder() {
    if (of_sess_) {
        destroy_session_();
    }
}

bool OFDecoder::valid() const {
    return valid_;
}

bool OFDecoder::begin(size_t sblen, size_t rblen) {
    roc_panic_if_not(valid());

    if (sblen_ == sblen && rblen_ == rblen) {
        return true;
    }

    if (!resize_tabs_(sblen + rblen)) {
        return false;
    }

    sblen_ = sblen;
    rblen_ = rblen;

    update_session_params_(sblen, rblen);
    reset_session_();

    return true;
}

void OFDecoder::set(size_t index, const core::Slice<uint8_t>& buffer) {
    roc_panic_if_not(valid());

    if (index >= sblen_ + rblen_) {
        roc_panic("of decoder: index out of bounds: index=%lu, size=%lu",
                  (unsigned long)index,
                  (unsigned long)(sblen_ + rblen_));
    }

    if (!buffer) {
        roc_panic("of decoder: null buffer");
    }

    if (buffer.size() != payload_size_) {
        roc_panic("of decoder: invalid payload size: size=%lu, expected=%lu",
                  (unsigned long)buffer.size(), (unsigned long)payload_size_);
    }

    if (buff_tab_[index]) {
        roc_panic("of decoder: can't overwrite buffer: index=%lu", (unsigned long)index);
    }

    has_new_packets_ = true;

    buff_tab_[index] = buffer;
    data_tab_[index] = buffer.data();
    recv_tab_[index] = true;

    // register new packet and try to repair more packets
    if (of_decode_with_new_symbol(of_sess_, data_tab_[index], (unsigned int)index)
        != OF_STATUS_OK) {
        roc_panic("of decoder: can't add packet to OF session");
    }
}

core::Slice<uint8_t> OFDecoder::repair(size_t index) {
    roc_panic_if_not(valid());

    if (!buff_tab_[index]) {
        update_();
        fix_buffer_(index);
    }

    return buff_tab_[index];
}

void OFDecoder::end() {
    if (of_sess_ != NULL) {
        report_();
        destroy_session_();
    }

    reset_session_();
    reset_tabs_();

    has_new_packets_ = false;
    decoding_finished_ = false;
}

void OFDecoder::update_session_params_(size_t sblen, size_t rblen) {
    of_sess_params_->nb_source_symbols = (uint32_t)sblen;
    of_sess_params_->nb_repair_symbols = (uint32_t)rblen;
}

void OFDecoder::reset_tabs_() {
    for (size_t i = 0; i < buff_tab_.size(); ++i) {
        buff_tab_[i] = core::Slice<uint8_t>();
        data_tab_[i] = NULL;
        recv_tab_[i] = false;
    }
}

bool OFDecoder::resize_tabs_(size_t size) {
    if (!buff_tab_.resize(size)) {
        return false;
    }
    if (!data_tab_.resize(size)) {
        return false;
    }
    if (!recv_tab_.resize(size)) {
        return false;
    }
    if (!status_.resize(size + 2)) {
        return false;
    }

    return true;
}

void OFDecoder::update_() {
    roc_panic_if(of_sess_ == NULL);

    if (!has_new_packets_) {
        return;
    }

    decode_();

    of_get_source_symbols_tab(of_sess_, &data_tab_[0]);

    has_new_packets_ = false;
}

void OFDecoder::decode_() {
    if (decoding_finished_ && is_optimal_()) {
        return;
    }

    if (!has_n_packets_(sblen_)) {
        return;
    }

    if (decoding_finished_) {
        // it's not allowed to decode twice, so we recreate the session
        reset_session_();

        if (of_set_available_symbols(of_sess_, &data_tab_[0]) != OF_STATUS_OK) {
            roc_panic("of decoder: can't add packets to OF session");
        }
    }

    // try to repair more packets
    if (of_finish_decoding(of_sess_) == OF_STATUS_OK) {
        decoding_finished_ = true;
    }
}

// note: we have to calculate this every time because OpenFEC
// doesn't always report to us when it repairs a packet
bool OFDecoder::has_n_packets_(size_t n_packets) const {
    size_t n = 0;
    for (size_t i = 0; i < data_tab_.size(); i++) {
        if (data_tab_[i]) {
            if (++n >= n_packets) {
                return true;
            }
        }
    }
    return false;
}

// returns true if the codec requires exactly k packets
// (number of source packets in block) to repair any
// source packet
//
// non-optimal codecs may require more packets, and the
// exact amount may be different every block
bool OFDecoder::is_optimal_() const {
    return codec_id_ == OF_CODEC_REED_SOLOMON_GF_2_M_STABLE;
}

void OFDecoder::reset_session_() {
    if (of_sess_ != NULL) {
        of_release_codec_instance(of_sess_);
        of_sess_ = NULL;
    }

    if (OF_STATUS_OK != of_create_codec_instance(&of_sess_, codec_id_, OF_DECODER, 0)) {
        roc_panic("of decoder: of_create_codec_instance() failed");
    }

    roc_panic_if(of_sess_ == NULL);

    if (OF_STATUS_OK != of_set_fec_parameters(of_sess_, of_sess_params_)) {
        roc_panic("of decoder: of_set_fec_parameters() failed");
    }

    if (OF_STATUS_OK
        != of_set_callback_functions(
            of_sess_, source_cb_,
            // OpenFEC doesn't repair fec-packets in case of Reed-Solomon FEC
            // and prints curses to the console if we give it the callback for that
            codec_id_ == OF_CODEC_REED_SOLOMON_GF_2_M_STABLE ? NULL : repair_cb_,
            (void*)this)) {
        roc_panic("of decoder: of_set_callback_functions() failed");
    }
}

void OFDecoder::destroy_session_() {
    of_release_codec_instance(of_sess_);
    of_sess_ = NULL;

    // OpenFEC may allocate memory without calling source_cb_()
    // we should free() such memory manually
    for (size_t i = 0; i < sblen_; i++) {
        if (data_tab_[i] == NULL) {
            continue;
        }
        if (buff_tab_[i] && buff_tab_[i].data() == data_tab_[i]) {
            continue;
        }
        of_free(data_tab_[i]);
        data_tab_[i] = NULL;
    }
}

void OFDecoder::report_() {
    size_t n_lost = 0, n_repaired = 0;

    status_[sblen_] = ' ';

    for (size_t i = 0; i < buff_tab_.size(); ++i) {
        char* status = (i < sblen_ ? &status_[i] : &status_[i + 1]);

        if (buff_tab_[i] || data_tab_[i]) {
            if (recv_tab_[i]) {
                *status = '.';
            } else {
                *status = 'r';
                n_repaired++;
                n_lost++;
            }
        } else {
            if (i < sblen_) {
                *status = 'X';
            } else {
                *status = 'x';
            }
            n_lost++;
        }
    }

    if (n_lost == 0) {
        return;
    }

    roc_log(LogDebug, "of decoder: repaired %u/%u/%u %s", (unsigned)n_repaired,
            (unsigned)n_lost, (unsigned)buff_tab_.size(), &status_[0]);
}

// OpenFEC may allocate memory without calling source_cb_()
// we need our own buffers, so we handle this case here
void OFDecoder::fix_buffer_(size_t index) {
    if (!buff_tab_[index] && data_tab_[index]) {
        if (void* buff = make_buffer_(index)) {
            memcpy(buff, data_tab_[index], payload_size_);
        }
    }
}

void* OFDecoder::make_buffer_(size_t index) {
    core::Slice<uint8_t> buffer = new (buffer_pool_) core::Buffer<uint8_t>(buffer_pool_);

    if (!buffer) {
        roc_log(LogError, "of decoder: can't allocate buffer");
        return NULL;
    }

    if (buffer.capacity() < payload_size_) {
        roc_log(LogError, "of decoder: packet size too large: size=%lu max=%lu",
                (unsigned long)payload_size_, (unsigned long)buffer.capacity());
        return NULL;
    }

    buffer.resize(payload_size_);
    buff_tab_[index] = buffer;

    return buffer.data();
}

// called when OpenFEC allocates a source packet
void* OFDecoder::source_cb_(void* context, uint32_t size, uint32_t index) {
    roc_panic_if(context == NULL);
    (void)size;
    OFDecoder& self = *(OFDecoder*)context;
    return self.make_buffer_(index);
}

// called when OpenFEC created a repair packet
// the return value is ignored
void* OFDecoder::repair_cb_(void*, uint32_t, uint32_t) {
    return NULL;
}

} // namespace fec
} // namespace roc
