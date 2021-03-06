/*
 * Copyright (c) 2015 Roc authors
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/resampler_profile.h"
#include "roc_core/crash.h"
#include "roc_core/heap_allocator.h"
#include "roc_core/log.h"
#include "roc_core/parse_duration.h"
#include "roc_core/scoped_destructor.h"
#include "roc_netio/transceiver.h"
#include "roc_packet/address_to_str.h"
#include "roc_packet/parse_address.h"
#include "roc_pipeline/receiver.h"
#include "roc_sndio/player.h"
#include "roc_sndio/sox.h"
#include "roc_sndio/sox_writer.h"

#include "roc_recv/cmdline.h"

using namespace roc;

namespace {

enum { MaxPacketSize = 2048, MaxFrameSize = 8192 };

} // namespace

int main(int argc, char** argv) {
    core::CrashHandler crash_handler;

    gengetopt_args_info args;

    const int code = cmdline_parser(argc, argv, &args);
    if (code != 0) {
        return code;
    }

    core::ScopedDestructor<gengetopt_args_info*, cmdline_parser_free> args_destructor(
        &args);

    core::Logger::instance().set_level(
        LogLevel(core::DefaultLogLevel + args.verbose_given));

    sndio::sox_setup();

    pipeline::PortConfig source_port;

    if (args.source_given) {
        if (!packet::parse_address(args.source_arg, source_port.address)) {
            roc_log(LogError, "can't parse source address: %s", args.source_arg);
            return 1;
        }
    }

    pipeline::PortConfig repair_port;
    if (args.repair_given) {
        if (!packet::parse_address(args.repair_arg, repair_port.address)) {
            roc_log(LogError, "can't parse repair address: %s", args.repair_arg);
            return 1;
        }
    }

    pipeline::ReceiverConfig config;

    switch ((unsigned)args.fec_arg) {
    case fec_arg_none:
        config.default_session.fec.codec = fec::NoCodec;
        source_port.protocol = pipeline::Proto_RTP;
        repair_port.protocol = pipeline::Proto_RTP;
        break;

    case fec_arg_rs:
        config.default_session.fec.codec = fec::ReedSolomon8m;
        source_port.protocol = pipeline::Proto_RTP_RSm8_Source;
        repair_port.protocol = pipeline::Proto_RSm8_Repair;
        break;

    case fec_arg_ldpc:
        config.default_session.fec.codec = fec::LDPCStaircase;
        source_port.protocol = pipeline::Proto_RTP_LDPC_Source;
        repair_port.protocol = pipeline::Proto_LDPC_Repair;
        break;

    default:
        break;
    }

    if (args.nbsrc_given) {
        if (config.default_session.fec.codec == fec::NoCodec) {
            roc_log(LogError, "--nbsrc can't be used when --fec=none)");
            return 1;
        }
        if (args.nbsrc_arg <= 0) {
            roc_log(LogError, "invalid --nbsrc: should be > 0");
            return 1;
        }
        config.default_session.fec.n_source_packets = (size_t)args.nbsrc_arg;
    }

    if (args.nbrpr_given) {
        if (config.default_session.fec.codec == fec::NoCodec) {
            roc_log(LogError, "--nbrpr can't be used when --fec=none");
            return 1;
        }
        if (args.nbrpr_arg <= 0) {
            roc_log(LogError, "invalid --nbrpr: should be > 0");
            return 1;
        }
        config.default_session.fec.n_repair_packets = (size_t)args.nbrpr_arg;
    }

    if (args.latency_given) {
        if (!core::parse_duration(args.latency_arg,
                                  config.default_session.target_latency)) {
            roc_log(LogError, "invalid --latency");
            return 1;
        }
    }

    if (args.min_latency_given) {
        if (!core::parse_duration(args.min_latency_arg,
                                  config.default_session.latency_monitor.min_latency)) {
            roc_log(LogError, "invalid --min-latency");
            return 1;
        }
    } else {
        config.default_session.latency_monitor.min_latency =
            config.default_session.target_latency * pipeline::DefaultMinLatencyFactor;
    }

    if (args.max_latency_given) {
        if (!core::parse_duration(args.max_latency_arg,
                                  config.default_session.latency_monitor.max_latency)) {
            roc_log(LogError, "invalid --max-latency");
            return 1;
        }
    } else {
        config.default_session.latency_monitor.max_latency =
            config.default_session.target_latency * pipeline::DefaultMaxLatencyFactor;
    }

    if (args.np_timeout_given) {
        if (!core::parse_duration(args.np_timeout_arg,
                                  config.default_session.watchdog.no_playback_timeout)) {
            roc_log(LogError, "invalid --np-timeout");
            return 1;
        }
    }

    if (args.bp_timeout_given) {
        if (!core::parse_duration(
                args.bp_timeout_arg,
                config.default_session.watchdog.broken_playback_timeout)) {
            roc_log(LogError, "invalid --bp-timeout");
            return 1;
        }
    }

    if (args.bp_window_given) {
        if (!core::parse_duration(
                args.bp_window_arg,
                config.default_session.watchdog.breakage_detection_window)) {
            roc_log(LogError, "invalid --bp-window");
            return 1;
        }
    }

    config.output.resampling = !args.no_resampling_flag;

    switch ((unsigned)args.resampler_profile_arg) {
    case resampler_profile_arg_low:
        config.default_session.resampler =
            audio::resampler_profile(audio::ResamplerProfile_Low);
        break;

    case resampler_profile_arg_medium:
        config.default_session.resampler =
            audio::resampler_profile(audio::ResamplerProfile_Medium);
        break;

    case resampler_profile_arg_high:
        config.default_session.resampler =
            audio::resampler_profile(audio::ResamplerProfile_High);
        break;

    default:
        break;
    }

    if (args.resampler_interp_given) {
        if (args.resampler_interp_arg <= 0) {
            roc_log(LogError, "invalid --resampler-interp: should be > 0");
            return 1;
        }
        config.default_session.resampler.window_interp =
            (size_t)args.resampler_interp_arg;
    }

    if (args.resampler_window_given) {
        if (args.resampler_window_arg <= 0) {
            roc_log(LogError, "invalid --resampler-window: should be > 0");
            return 1;
        }
        config.default_session.resampler.window_size = (size_t)args.resampler_window_arg;
    }

    size_t sample_rate = 0;
    if (args.rate_given) {
        if (args.rate_arg <= 0) {
            roc_log(LogError, "invalid --rate: should be > 0");
            return 1;
        }
        sample_rate = (size_t)args.rate_arg;
    } else {
        if (!config.output.resampling) {
            sample_rate = pipeline::DefaultSampleRate;
        }
    }

    config.output.poisoning = args.poisoning_flag;
    config.output.beeping = args.beeping_flag;

    core::HeapAllocator allocator;
    core::BufferPool<uint8_t> byte_buffer_pool(allocator, MaxPacketSize,
                                               args.poisoning_flag);
    core::BufferPool<audio::sample_t> sample_buffer_pool(allocator, MaxFrameSize,
                                                         args.poisoning_flag);
    packet::PacketPool packet_pool(allocator, args.poisoning_flag);

    sndio::SoxWriter writer(allocator, config.output.channels, sample_rate);

    if (!writer.open(args.output_arg, args.type_arg)) {
        roc_log(LogError, "can't open output file or device: %s %s", args.output_arg,
                args.type_arg);
        return 1;
    }

    config.output.timing = writer.is_file();
    config.output.sample_rate = writer.sample_rate();

    if (config.output.sample_rate == 0) {
        roc_log(LogError,
                "can't detect output sample rate, try to set it "
                "explicitly with --rate option");
        return 1;
    }

    rtp::FormatMap format_map;

    pipeline::Receiver receiver(config, format_map, packet_pool, byte_buffer_pool,
                                sample_buffer_pool, allocator);
    if (!receiver.valid()) {
        roc_log(LogError, "can't create receiver pipeline");
        return 1;
    }

    sndio::Player player(sample_buffer_pool, receiver, writer, writer.frame_size(),
                         args.oneshot_flag);
    if (!player.valid()) {
        roc_log(LogError, "can't create player");
        return 1;
    }

    netio::Transceiver trx(packet_pool, byte_buffer_pool, allocator);
    if (!trx.valid()) {
        roc_log(LogError, "can't create network transceiver");
        return 1;
    }

    if (!trx.add_udp_receiver(source_port.address, receiver)) {
        roc_log(LogError, "can't register udp receiver: %s",
                packet::address_to_str(source_port.address).c_str());
        return 1;
    }
    if (!receiver.add_port(source_port)) {
        roc_log(LogError, "can't add udp port: %s",
                packet::address_to_str(source_port.address).c_str());
        return 1;
    }

    if (config.default_session.fec.codec != fec::NoCodec) {
        if (!trx.add_udp_receiver(repair_port.address, receiver)) {
            roc_log(LogError, "can't register udp receiver: %s",
                    packet::address_to_str(repair_port.address).c_str());
            return 1;
        }
        if (!receiver.add_port(repair_port)) {
            roc_log(LogError, "can't add udp port: %s",
                    packet::address_to_str(repair_port.address).c_str());
            return 1;
        }
    }

    if (!trx.start()) {
        roc_log(LogError, "can't start transceiver");
        return 1;
    }

    int status = 1;

    if (player.start()) {
        player.join();
        status = 0;
    } else {
        roc_log(LogError, "can't start player");
    }

    trx.stop();
    trx.join();

    trx.remove_port(source_port.address);

    if (config.default_session.fec.codec != fec::NoCodec) {
        trx.remove_port(repair_port.address);
    }

    return status;
}
