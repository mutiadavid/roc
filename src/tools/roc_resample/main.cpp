/*
 * Copyright (c) 2015 Mikhail Baranov
 * Copyright (c) 2015 Victor Gaydov
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "roc_audio/null_writer.h"
#include "roc_audio/profiling_writer.h"
#include "roc_audio/resampler_writer.h"
#include "roc_core/buffer_pool.h"
#include "roc_core/crash.h"
#include "roc_core/heap_allocator.h"
#include "roc_core/log.h"
#include "roc_sndio/sox.h"
#include "roc_sndio/sox_reader.h"
#include "roc_sndio/sox_writer.h"

#include "roc_resample/cmdline.h"

using namespace roc;

namespace {

enum { MaxFrameSize = 65 * 1024, Channels = 0x3 };

} // namespace

int main(int argc, char** argv) {
    core::CrashHandler crash_handler;

    gengetopt_args_info args;

    const int code = cmdline_parser(argc, argv, &args);
    if (code != 0) {
        return code;
    }

    core::Logger::instance().set_level(
        LogLevel(core::DefaultLogLevel + args.verbose_given));

    sndio::sox_setup();

    core::HeapAllocator allocator;
    core::BufferPool<audio::sample_t> pool(allocator, MaxFrameSize, 1);

    size_t chunk_size = 0;
    if (args.chunk_given) {
        chunk_size = (size_t)args.chunk_arg;
    }

    sndio::SoxReader sox_reader(pool, Channels, chunk_size, 0);

    if (!sox_reader.open(args.input_arg, NULL)) {
        roc_log(LogError, "can't open input file: %s", args.input_arg);
        return 1;
    }

    if (!sox_reader.is_file()) {
        roc_log(LogError, "not a file file: %s", args.input_arg);
        return 1;
    }

    size_t writer_sample_rate;
    if (args.rate_given) {
        writer_sample_rate = (size_t)args.rate_arg;
    } else {
        writer_sample_rate = sox_reader.sample_rate();
    }

    sndio::SoxWriter sox_writer(allocator, Channels, writer_sample_rate);
    audio::NullWriter null_writer;

    audio::IWriter* writer;
    if (args.output_given) {
        writer = &sox_writer;
    } else {
        roc_log(LogInfo, "no output given, using null output");
        writer = &null_writer;
    }

    audio::ResamplerConfig resampler_config;

    if (args.interp_given) {
        resampler_config.window_interp = (size_t)args.interp_arg;
    }

    if (args.window_given) {
        resampler_config.window_size = (size_t)args.window_arg;
    }

    if (args.frame_given) {
        resampler_config.frame_size = (size_t)args.frame_arg;
    }

    audio::ResamplerWriter resampler(*writer, pool, allocator, resampler_config, Channels);
    if (!resampler.valid()) {
        roc_log(LogError, "can't create resampler");
        return 1;
    }

    if (!resampler.set_scaling((float)sox_reader.sample_rate() / writer_sample_rate)) {
        roc_log(LogError, "can't set resampler scaling");
        return 1;
    }

    if (args.output_given) {
        if (!sox_writer.open(args.output_arg, NULL)) {
            roc_log(LogError, "can't open output file: %s", args.output_arg);
            return 1;
        }

        if (!sox_writer.is_file()) {
            roc_log(LogError, "not a file file: %s", args.output_arg);
            return 1;
        }
    }

    audio::ProfilingWriter profiler(resampler, Channels, sox_reader.sample_rate());

    int status = 1;

    if (sox_reader.start(profiler)) {
        sox_reader.join();
        status = 0;
        roc_log(LogInfo, "done");
    } else {
        roc_log(LogError, "can't start reader");
    }

    if (args.output_given) {
        sox_writer.close();
    }

    return status;
}
