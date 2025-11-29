/*
 * Copyright (C) 2025 comparchitect (https://github.com/comparchitect)
 */

#include "mbelib_app.hpp"

#include "audio.hpp"
#include "ch.h"
#include "baseband_api.hpp"
#include "portapack.hpp"
#include "sd_card.hpp"
#include "string_format.hpp"
#include "file_path.hpp"
#include "rtc_time.hpp"
#include "apps/ambe_log_format.hpp"
#include "ui_fileman.hpp"
#include "event_m0.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>

#ifndef MBELIB_USE_PREPARED_IMAGE
#define MBELIB_USE_PREPARED_IMAGE 1
#endif

using namespace portapack;

namespace ui {

namespace {
constexpr size_t kSamplesPerFrame = AMBEPCMFrameMessage::kMaxSamples;
constexpr uint16_t kPlaybackSampleRate = 48000;
constexpr size_t kWavHeaderSize = 44;
constexpr size_t kDecodeThreadStack = 4096;
constexpr tprio_t kDecodeThreadPriority = NORMALPRIO + 4;
constexpr uint32_t kMaxInFlightFrames = 4;
constexpr systime_t kInflightSleepMs = 5;

class MutexGuard {
   public:
    explicit MutexGuard(Mutex& m)
        : mutex_{m} {
        chMtxLock(&mutex_);
    }

    ~MutexGuard() {
        chMtxUnlock();
    }

    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;

   private:
    Mutex& mutex_;
};


}  // namespace

MBELIBView::MBELIBView(NavigationView& nav)
    : nav_{nav} {

    chMtxInit(&file_io_mutex_);

    add_children({&rssi_,
                  &audio_,
                  &field_volume_,
                  &text_status_,
                  &text_selected_file_,
                  &button_select_file_,
                  &button_decode_,
                  &button_play_wav_,
                  &text_m0_stats_});

    button_select_file_.on_select = [this](Button&) {
        select_file();
    };

    button_decode_.on_select = [this](Button&) {
        decode_selected_file();
    };

    button_play_wav_.hidden(true);
    button_play_wav_.on_select = [this](Button&) {
        if (is_playing_) {
            stop_wav_playback();
        } else {
            start_wav_playback();
        }
    };

    update_sd_card_state();
    update_play_button();
    update_ready_status();
    update_m0_stats_text();
}

MBELIBView::~MBELIBView() {
    decode_abort_ = true;
    decode_in_progress_ = false;
    stop_wav_playback();
    close_file();
    output_ready_ = false;
    close_output_file();
    baseband::shutdown();
}

void MBELIBView::focus() {
    update_sd_card_state();
    update_play_button();
    update_ready_status();
    button_select_file_.focus();
}

void MBELIBView::update_sd_card_state() {
    const bool available = (sd_card::status() == sd_card::Status::Mounted);
    sd_card_available_ = available;
    if (!sd_card_available_) {
        close_file();
        stop_wav_playback();
        wav_available_ = false;
    }
    update_play_button();
}

void MBELIBView::select_file() {
    update_sd_card_state();
    if (!sd_card_available_) {
        text_status_.set("SD card not ready");
        return;
    }

    auto loader = nav_.push<FileLoadView>(".ambe");
    loader->push_dir(captures_dir);
    loader->on_changed = [this](std::filesystem::path new_file_path) {
        selected_file_ = new_file_path;
        text_selected_file_.set("File: " + new_file_path.filename().string());
        text_selected_file_.set_dirty();
        wav_file_ = new_file_path;
        wav_file_.replace_extension(".wav");
        wav_available_ = wav_exists();
        update_play_button();

        total_frames_expected_ = 0;
        File info_file;
        if (!decode_in_progress_ && !info_file.open(new_file_path, true, false)) {
            const auto size = info_file.size();
            info_file.close();
            if (size >= sizeof(ambe_log::Header)) {
                const auto payload = size - sizeof(ambe_log::Header);
                total_frames_expected_ = static_cast<uint32_t>(payload / ambe_log::kFrameBytes);
            }
        }
        update_ready_status();
    };
}

void MBELIBView::decode_selected_file() {
    update_sd_card_state();
    if (!sd_card_available_) {
        text_status_.set("SD card not ready");
        return;
    }

    if (selected_file_.empty()) {
        text_status_.set("Select a file first");
        return;
    }

    if (decode_in_progress_ || decode_thread_) {
        text_status_.set("Decode already running");
        return;
    }

    decode_abort_.store(false, std::memory_order_relaxed);
    stop_wav_playback();
    wav_file_ = selected_file_;
    wav_file_.replace_extension(".wav");
    wav_available_ = false;
    update_play_button();

    baseband::shutdown();
    chThdSleepMilliseconds(20);
    if (MBELIB_USE_PREPARED_IMAGE) {
        baseband::run_prepared_image(portapack::memory::map::m4_code.base());
    } else {
        // Legacy tag for bundled image
        baseband::run_image(portapack::spi_flash::image_tag_ambe2_decode);
    }
    chThdSleepMilliseconds(10);
    baseband::mbelib_decode_reset();

    chSysLock();
    frames_sent_ = 0;
    frames_completed_ = 0;
    total_samples_written_ = 0;
    frame_error_count_ = 0;
    decode_finalized_ = false;
    decode_thread_finished_ = false;
    m4_completion_ack_received_ = false;
    // Reset upsampling state
    prev_sample_ = 0;
    frame_count_ = 0;
    frames_processed_latest_ = 0;
    frames_in_flight_ = 0;
    max_frames_in_flight_ = 0;
    frames_read_total_ = 0;
    read_error_count_ = 0;
    chSysUnlock();
    decode_result_ = {};
    m4_pcm_dropped_ = 0;
    update_m0_stats_text();

    if (total_frames_expected_ == 0) {
        File info;
        if (!info.open(selected_file_, true, false)) {
            const auto size = info.size();
            info.close();
            if (size >= sizeof(ambe_log::Header)) {
                const auto payload = size - sizeof(ambe_log::Header);
                total_frames_expected_ = static_cast<uint32_t>(payload / ambe_log::kFrameBytes);
            }
        }
    }

    decode_in_progress_ = true;
    std::snprintf(decode_result_.status, sizeof(decode_result_.status), "%s", "Decoding...");
    update_progress_text();
    button_decode_.set_text("Decoding...");
    button_decode_.set_dirty();

    start_decode_thread();
}

void MBELIBView::start_decode_thread() {
    if (decode_thread_) {
        return;
    }

    decode_thread_ = chThdCreateFromHeap(
        nullptr,
        kDecodeThreadStack,
        kDecodeThreadPriority,
        decode_thread_fn,
        this);

    if (!decode_thread_) {
        decode_in_progress_ = false;
        text_status_.set("Thread start failed");
        button_decode_.set_text("Decode");
        button_decode_.set_dirty();
    }
}

msg_t MBELIBView::decode_thread_fn(void* arg) {
    auto* self = static_cast<MBELIBView*>(arg);
    if (!self) {
        return static_cast<msg_t>(0);
    }
    self->decode_thread();
    return static_cast<msg_t>(0);
}

void MBELIBView::decode_thread() {
    decode_result_ = read_frames_and_decode();
    chSysLock();
    decode_thread_finished_ = true;
    chSysUnlock();

    // Send stop command to M4 to signal end of frame stream
    baseband::mbelib_decode_stop();

    RequestSignalMessage message{RequestSignalMessage::Signal::AmbeDecodeDone};
    EventDispatcher::send_message(message);
}

MBELIBView::DecodeResult MBELIBView::read_frames_and_decode() {
    DecodeResult result{};
    std::snprintf(result.status, sizeof(result.status), "%s", "Decode failed");

    close_file();
    close_output_file();
    output_ready_ = false;

    const auto open_result = [&]() {
        MutexGuard lock{file_io_mutex_};
        return input_file_.open(selected_file_, true, false);
    }();
    if (open_result) {
        std::snprintf(result.status, sizeof(result.status), "%s", "Open failed");
        return result;
    }
    file_open_ = true;

    ambe_log::Header header{};
    const auto header_result = [&]() {
        MutexGuard lock{file_io_mutex_};
        return input_file_.read(&header, sizeof(header));
    }();
    if (!header_result.is_ok() || *header_result != sizeof(header) || !ambe_log::validate(header)) {
        std::snprintf(result.status, sizeof(result.status), "%s", "Bad header");
        close_file();
        return result;
    }

    const auto wav_create = [&]() {
        MutexGuard lock{file_io_mutex_};
        return output_file_.create(wav_file_);
    }();
    if (wav_create) {
        std::snprintf(result.status, sizeof(result.status), "%s", "WAV create failed");
        close_file();
        return result;
    }

    if (!write_wav_header(output_file_, 0, kPlaybackSampleRate)) {
        std::snprintf(result.status, sizeof(result.status), "%s", "Header write err");
        close_output_file();
        close_file();
        auto wav_path_string = wav_file_.string();
        ::remove(wav_path_string.c_str());
        return result;
    }

    const auto data_seek = [&]() {
        MutexGuard lock{file_io_mutex_};
        return output_file_.seek(kWavHeaderSize);
    }();
    if (data_seek.is_error()) {
        std::snprintf(result.status, sizeof(result.status), "%s", "Seek failed");
        close_output_file();
        close_file();
        auto wav_path_string = wav_file_.string();
        ::remove(wav_path_string.c_str());
        return result;
    }

    output_ready_ = true;
    chSysLock();
    frames_completed_ = 0;
    total_samples_written_ = 0;
    frames_in_flight_ = 0;
    // Reset upsampling state
    prev_sample_ = 0;
    frame_count_ = 0;
    // Reset completion acknowledgment
    m4_completion_ack_received_ = false;
    chSysUnlock();

    std::array<uint8_t, ambe_log::kFrameBytes> packed{};
    auto reset_inflight = [this]() {
        chSysLock();
        frames_in_flight_ = 0;
        chSysUnlock();
    };

    while (true) {
        if (decode_abort_.load(std::memory_order_relaxed)) {
            result.cancelled = true;
            break;
        }

        const auto read_result = [&]() {
            MutexGuard lock{file_io_mutex_};
            return input_file_.read(packed.data(), packed.size());
        }();
        if (!read_result.is_ok()) {
            const auto& err = read_result.error();
            const auto msg = err.what();
            chSysLock();
            ++read_error_count_;
            chSysUnlock();
            std::snprintf(result.status, sizeof(result.status), "Read error: %lu (%-.30s)",
                          static_cast<unsigned long>(err.code()),
                          msg.c_str());
            output_ready_ = false;
            close_output_file();
            close_file();
            auto wav_path_string = wav_file_.string();
            ::remove(wav_path_string.c_str());
            reset_inflight();
            return result;
        }
        if (*read_result == 0) {
            break;
        }
        if (*read_result != packed.size()) {
            chSysLock();
            ++read_error_count_;
            chSysUnlock();
            std::snprintf(result.status, sizeof(result.status), "%s", "Partial frame");
            output_ready_ = false;
            close_output_file();
            close_file();
            auto wav_path_string = wav_file_.string();
            ::remove(wav_path_string.c_str());
            reset_inflight();
            return result;
        }

        while (true) {
            if (decode_abort_.load(std::memory_order_relaxed)) {
                result.cancelled = true;
                break;
            }

            uint32_t inflight = 0;
            chSysLock();
            inflight = frames_in_flight_;
            chSysUnlock();

            if (inflight < kMaxInFlightFrames) {
                break;
            }

            RequestSignalMessage throttle_update{RequestSignalMessage::Signal::AmbeDecodeHostStats};
            EventDispatcher::send_message(throttle_update);
            chThdSleepMilliseconds(kInflightSleepMs);
        }

        if (result.cancelled) {
            break;
        }

        uint32_t sequence = 0;
        chSysLock();
        sequence = frames_sent_++;
        ++frames_in_flight_;
        ++frames_read_total_;
        if (frames_in_flight_ > max_frames_in_flight_) {
            max_frames_in_flight_ = frames_in_flight_;
        }
        chSysUnlock();
        chThdSleepMilliseconds(50);
        baseband::mbelib_decode_send_frame(packed.data());

        RequestSignalMessage host_stats{RequestSignalMessage::Signal::AmbeDecodeHostStats};
        EventDispatcher::send_message(host_stats);

        if (((sequence + 1u) <= 10u) || ((sequence + 1u) % 25u) == 0u) {
            RequestSignalMessage progress{RequestSignalMessage::Signal::AmbeDecodeProgress};
            EventDispatcher::send_message(progress);
        }
    }

    close_file();

    if (result.cancelled) {
        output_ready_ = false;
        close_output_file();
        auto wav_path_string = wav_file_.string();
        ::remove(wav_path_string.c_str());
        reset_inflight();
        std::snprintf(result.status, sizeof(result.status), "%s", "Decode cancelled");
        return result;
    }

    {
        RequestSignalMessage progress{RequestSignalMessage::Signal::AmbeDecodeProgress};
        EventDispatcher::send_message(progress);
    }

    baseband::mbelib_decode_flush();

    result.success = true;
    uint32_t sent = 0;
    chSysLock();
    sent = frames_sent_;
    chSysUnlock();
    result.frames = sent;
    std::snprintf(result.status, sizeof(result.status), "%s", "Decoding...");
    return result;
}

void MBELIBView::close_file() {
    if (file_open_) {
        MutexGuard lock{file_io_mutex_};
        input_file_.close();
        file_open_ = false;
    }
}

void MBELIBView::close_output_file() {
    MutexGuard lock{file_io_mutex_};
    output_file_.close();
}


void MBELIBView::handle_decode_complete() {
    if (decode_thread_) {
        chThdWait(decode_thread_);
        decode_thread_ = nullptr;
    }

    if (!decode_result_.success) {
        decode_in_progress_ = false;
        output_ready_ = false;
        close_output_file();
        text_status_.set(decode_result_.status);
        auto wav_path_string = wav_file_.string();
        if (!wav_path_string.empty()) {
            ::remove(wav_path_string.c_str());
        }
        button_decode_.set_text("Decode");
        button_decode_.set_dirty();
        update_play_button();
        baseband::shutdown();
        update_m0_stats_text();
        return;
    }

    update_progress_text();
    finalize_decode_if_ready();
}

bool MBELIBView::wav_exists() const {
    if (wav_file_.empty()) {
        return false;
    }

    File test;
    auto result = test.open(wav_file_, true, false);
    if (result.is_valid()) {
        return false;
    }
    test.close();
    return true;
}

void MBELIBView::update_play_button() {
    const bool wav_present = wav_exists();
    wav_available_ = wav_present && !decode_in_progress_;
    const bool show = sd_card_available_ && wav_present && !decode_in_progress_;
    button_play_wav_.hidden(!show);
    button_play_wav_.set_text(is_playing_ ? "Stop" : "Play WAV");
    button_play_wav_.set_dirty();
}

void MBELIBView::start_wav_playback() {
    if (decode_in_progress_) {
        text_status_.set("Decode in progress");
        return;
    }

    if (is_playing_) {
        return;
    }

    if (!sd_card_available_) {
        text_status_.set("SD card not ready");
        return;
    }

    if (!wav_exists()) {
        text_status_.set("No WAV found");
        update_play_button();
        return;
    }

    auto reader = std::make_unique<WAVFileReader>();
    if (!reader->open(wav_file_)) {
        text_status_.set("WAV open failed");
        return;
    }

    if ((reader->channels() != 1) ||
        !((reader->bits_per_sample() == 8) || (reader->bits_per_sample() == 16))) {
        text_status_.set("Unsupported WAV format");
        return;
    }

    baseband::shutdown();
    chThdSleepMilliseconds(20);
    baseband::run_image(portapack::spi_flash::image_tag_audio_tx);
    chThdSleepMilliseconds(10);

    if (replay_thread_) {
        replay_thread_.reset();
    }

    const uint32_t sample_rate = reader->sample_rate();
    const uint16_t bits_per_sample = reader->bits_per_sample();

    baseband::set_audiotx_config(
        1536000 / 20,
        portapack::transmitter_model.channel_bandwidth(),
        0,
        8,
        bits_per_sample,
        0,
        false, false, false, false);

    baseband::set_sample_rate(sample_rate);
    portapack::transmitter_model.set_sampling_rate(1536000);

    portapack::transmitter_model.enable();

    ready_signal_ = false;
    replay_thread_ = std::make_unique<ReplayThread>(
        std::move(reader),
        2048,
        3,
        &ready_signal_,
        [](uint32_t return_code) {
            ReplayThreadDoneMessage message{return_code};
            EventDispatcher::send_message(message);
        });

    const audio::Rate codec_rate =
        (sample_rate <= 12000)
            ? audio::Rate::Hz_12000
            : (sample_rate <= 24000) ? audio::Rate::Hz_24000 : audio::Rate::Hz_48000;
    audio::set_rate(codec_rate);

    audio::output::start();
    audio::output::unmute();
    audio::output::speaker_unmute();
    audio::output::update_audio_mute();

    is_playing_ = true;
    text_status_.set("Streaming WAV...");
    update_play_button();
}

void MBELIBView::stop_wav_playback() {
    if (!is_playing_ && !replay_thread_) {
        return;
    }

    if (replay_thread_) {
        replay_thread_.reset();
    }

    audio::output::stop();
    audio::output::speaker_mute();
    portapack::transmitter_model.disable();
    baseband::shutdown();
    ready_signal_ = false;
    is_playing_ = false;
    update_play_button();
}

void MBELIBView::on_replay_done(uint32_t return_code) {
    stop_wav_playback();
    switch (return_code) {
        case ReplayThread::END_OF_FILE:
            text_status_.set("Playback complete");
            break;
        case ReplayThread::READ_ERROR:
            text_status_.set("Playback read error");
            break;
        default:
            text_status_.set("Playback stopped");
            break;
    }
}

void MBELIBView::on_pcm_frame(const AMBEPCMFrameMessage& message) {
    if (!decode_in_progress_ || decode_finalized_ || !output_ready_) {
        return;
    }

    if (decode_abort_.load(std::memory_order_relaxed)) {
        return;
    }

    const size_t sample_count = message.sample_count;

    // Upsample and smooth the AGC-processed int16_t data from M4 (6x to 48kHz)
    std::array<int16_t, 960> upsampled_buffer{};
    size_t upsampled_count = 0;
    upsample_and_smooth(message.samples, sample_count, upsampled_buffer.data(), &upsampled_count);

    // Apply final clamping to -32768 to +32767 range (dsd.test style)
    for (size_t i = 0; i < upsampled_count; ++i) {
        if (upsampled_buffer[i] > 32767) upsampled_buffer[i] = 32767;
        if (upsampled_buffer[i] < -32768) upsampled_buffer[i] = -32768;
    }

    // Write the upsampled and smoothed data to WAV
    const size_t byte_count = upsampled_count * sizeof(int16_t);
    const auto write_result = [&]() {
        MutexGuard lock{file_io_mutex_};
        return output_file_.write(upsampled_buffer.data(), byte_count);
    }();

    if (write_result.is_error()) {
        output_ready_ = false;
        decode_in_progress_ = false;
        decode_finalized_ = true;
        text_status_.set("WAV write err");
        close_output_file();
        auto wav_path_string = wav_file_.string();
        if (!wav_path_string.empty()) {
            ::remove(wav_path_string.c_str());
        }
        button_decode_.set_text("Decode");
        button_decode_.set_dirty();
        update_play_button();
        baseband::shutdown();
        return;
    }

    bool trigger_update = false;
    chSysLock();
    total_samples_written_ += upsampled_count;  // Count upsampled samples, not original
    ++frames_completed_;
    if (frames_in_flight_ > 0) {
        --frames_in_flight_;
    }
    if (frames_completed_ > frames_processed_latest_) {
        frames_processed_latest_ = frames_completed_;
    }
    const uint32_t local_completed = frames_completed_;
    trigger_update = (local_completed == 1u) || ((local_completed % 5u) == 0u);
    chSysUnlock();

    update_m0_stats_text();

    if (trigger_update) {
        update_progress_text();
    }

    finalize_decode_if_ready();
}

void MBELIBView::on_decode_stats(const AMBE2DecodeStatsMessage& message) {
    if (!decode_in_progress_ || decode_finalized_) {
        return;
    }

    // Check for M4 completion acknowledgment
    if (message.completed) {
        m4_completion_ack_received_ = true;
    }

    frame_error_count_ = message.errors;
    m4_pcm_dropped_ = message.pcm_drop;

    chSysLock();
    if (message.frames > frames_processed_latest_) {
        frames_processed_latest_ = message.frames;
    }
    chSysUnlock();

    update_progress_text();
    update_m0_stats_text();

    finalize_decode_if_ready();
}

void MBELIBView::finalize_decode_if_ready() {
    if (decode_finalized_ || !decode_result_.success) {
        return;
    }

    bool finished = false;
    bool m4_ack = false;
    chSysLock();
    finished = decode_thread_finished_;
    m4_ack = m4_completion_ack_received_;
    chSysUnlock();

    // Wait for both M0 thread to finish AND M4 completion acknowledgment
    if (!finished || !m4_ack) {
        return;
    }

    decode_finalized_ = true;
    decode_in_progress_ = false;
    output_ready_ = false;

    bool wav_ok = true;
    if (!write_wav_header(output_file_, total_samples_written_, kPlaybackSampleRate)) {
        wav_ok = false;
    } else {
        const auto sync = [&]() {
            MutexGuard lock{file_io_mutex_};
            return output_file_.sync();
        }();
        if (sync.is_valid()) {
            wav_ok = false;
        }
    }

    close_output_file();
    baseband::shutdown();

    if (wav_ok) {
        decode_result_.wav_written = true;
        decode_result_.frames = frames_completed_;
        decode_result_.samples = total_samples_written_;
        if (frame_error_count_ > 0) {
            text_status_.set("WAV saved (with errors)");
            std::snprintf(decode_result_.status, sizeof(decode_result_.status), "WAV saved (%lu errs)",
                          static_cast<unsigned long>(frame_error_count_));
        } else {
            //text_status_.set("WAV saved");
            std::snprintf(decode_result_.status, sizeof(decode_result_.status), "WAV saved, %lums",
                          static_cast<unsigned long>(m4_pcm_dropped_));
            text_status_.set(decode_result_.status);
        }
        wav_available_ = true;
    } else {
        decode_result_.wav_written = false;
        wav_available_ = false;
        text_status_.set("WAV finalize err");
        auto wav_path_string = wav_file_.string();
        if (!wav_path_string.empty()) {
            ::remove(wav_path_string.c_str());
        }
    }

    button_decode_.set_text("Decode");
    button_decode_.set_dirty();
    update_play_button();
    // Skip update_m0_stats_text() to avoid potential stack issues
    // update_m0_stats_text();
}

void MBELIBView::update_progress_text() {
    if (!decode_in_progress_) {
        return;
    }

    uint32_t sent = 0;
    uint32_t completed = 0;
    uint32_t processed_latest = 0;
    uint32_t total_expected = total_frames_expected_;
    uint32_t error_count = frame_error_count_;
    uint32_t pcm_drop = m4_pcm_dropped_;
    chSysLock();
    sent = frames_sent_;
    completed = frames_completed_;
    processed_latest = frames_processed_latest_;
    chSysUnlock();

    const uint32_t processed = std::max(processed_latest, completed);
    const uint32_t total = (total_expected != 0) ? total_expected : std::max(sent, processed);
    uint32_t percent = 0;
    if (total > 0) {
        const uint64_t scaled = static_cast<uint64_t>(processed) * 100ULL;
        percent = static_cast<uint32_t>((scaled >= static_cast<uint64_t>(total) * 100ULL)
                                            ? 100
                                            : (scaled / total));
    }
    char status[96];

    int len = std::snprintf(status, sizeof(status), "M4: %lu%% %lu/%lu",
                            static_cast<unsigned long>(percent),
                            static_cast<unsigned long>(processed),
                            static_cast<unsigned long>(total));
    if ((len > 0) && ((error_count > 0) || (pcm_drop > 0))) {
        const size_t remaining = sizeof(status) - static_cast<size_t>(len);
        const int appended = std::snprintf(status + len, remaining, " (err %lu drop %lu)",
                                           static_cast<unsigned long>(error_count),
                                           static_cast<unsigned long>(pcm_drop));
        if (appended > 0) {
            len += appended;
        }
    }

    text_status_.set(status);
}

void MBELIBView::update_m0_stats_text() {
    uint32_t sent = 0;
    uint32_t completed = 0;
    uint32_t in_flight = 0;
    uint32_t max_in_flight = 0;
    uint32_t read_total = 0;
    uint32_t read_errors = 0;

    chSysLock();
    sent = frames_sent_;
    completed = frames_completed_;
    in_flight = frames_in_flight_;
    max_in_flight = max_frames_in_flight_;
    read_total = frames_read_total_;
    read_errors = read_error_count_;
    chSysUnlock();

    const uint32_t pending = in_flight;

    if (!decode_in_progress_) {
        text_m0_stats_.set("M0: idle");
        return;
    }

    char line[80];
    std::snprintf(line, sizeof(line), "M0: s%lu c%lu q%lu/%u",// max%lu read%lu err%lu",
                  static_cast<unsigned long>(sent),
                  static_cast<unsigned long>(completed),
                  static_cast<unsigned long>(pending),
                  static_cast<unsigned>(kMaxInFlightFrames));//,
                  //static_cast<unsigned long>(max_in_flight),
                  //static_cast<unsigned long>(read_total),
                  //static_cast<unsigned long>(read_errors));
    text_m0_stats_.set(line);
}

void MBELIBView::update_ready_status() {
    if (decode_in_progress_) {
        return;
    }

    if (selected_file_.empty()) {
        text_status_.set("Select .ambe file");
        return;
    }

    if (total_frames_expected_ > 0) {
        char status[64];
        std::snprintf(status, sizeof(status), "Ready: %lu frames",
                      static_cast<unsigned long>(total_frames_expected_));
        text_status_.set(status);
    } else {
        text_status_.set("Ready: 0 frames");
    }
}

void MBELIBView::upsample_and_smooth(const int16_t* input, size_t input_count, int16_t* output, size_t* output_count) {
    if (!input || !output || !output_count || input_count == 0) {
        *output_count = 0;
        return;
    }

    size_t out_idx = 0;

    // Triangle interpolation upsampling (6x) - matches dsd.test
    for (size_t i = 0; i < input_count; ++i) {
        const int16_t current_sample = input[i];
        const int16_t prev_sample = (i == 0 && frame_count_ == 0) ? current_sample : prev_sample_;

        // Generate 6 interpolated samples using triangle interpolation
        // Convert back to float for interpolation, then back to int16
        const float curr_f = static_cast<float>(current_sample);
        const float prev_f = static_cast<float>(prev_sample);

        output[out_idx++] = static_cast<int16_t>((curr_f * 0.166f) + (prev_f * 0.834f));
        output[out_idx++] = static_cast<int16_t>((curr_f * 0.332f) + (prev_f * 0.668f));
        output[out_idx++] = static_cast<int16_t>((curr_f * 0.5f) + (prev_f * 0.5f));
        output[out_idx++] = static_cast<int16_t>((curr_f * 0.668f) + (prev_f * 0.332f));
        output[out_idx++] = static_cast<int16_t>((curr_f * 0.834f) + (prev_f * 0.166f));
        output[out_idx++] = current_sample;

        prev_sample_ = current_sample;
    }

    // Apply temporal smoothing (5-point moving average) after enough frames
    if (frame_count_ > 0 && out_idx >= 24) {
        for (size_t i = 12; i < out_idx - 12; ++i) {
            // Convert to int32 for averaging to avoid overflow
            const int32_t sum = static_cast<int32_t>(output[i-2]) +
                               static_cast<int32_t>(output[i-1]) +
                               static_cast<int32_t>(output[i]) +
                               static_cast<int32_t>(output[i+1]) +
                               static_cast<int32_t>(output[i+2]);
            output[i] = static_cast<int16_t>(sum / 5);
        }
    }

    *output_count = out_idx;
    frame_count_++;
}

bool MBELIBView::write_wav_header(File& wav_file, uint32_t sample_count, uint32_t sample_rate) {
    MutexGuard lock{file_io_mutex_};

    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t data_chunk_size = sample_count * block_align;
    const uint32_t riff_chunk_size = 36 + data_chunk_size;

    struct WAVHeader {
        char riff_id[4];
        uint32_t chunk_size;
        char wave_id[4];
        char fmt_id[4];
        uint32_t fmt_size;
        uint16_t audio_format;
        uint16_t num_channels;
        uint32_t sample_rate;
        uint32_t byte_rate;
        uint16_t block_align;
        uint16_t bits_per_sample;
        char data_id[4];
        uint32_t data_size;
    } __attribute__((packed));

    WAVHeader header{
        {'R', 'I', 'F', 'F'},
        riff_chunk_size,
        {'W', 'A', 'V', 'E'},
        {'f', 'm', 't', ' '},
        16,
        1,
        channels,
        sample_rate,
        byte_rate,
        block_align,
        bits_per_sample,
        {'d', 'a', 't', 'a'},
        data_chunk_size};

    static_assert(sizeof(WAVHeader) == kWavHeaderSize, "Unexpected WAV header size");

    auto seek_result = wav_file.seek(0);
    if (seek_result.is_error()) {
        return false;
    }

    auto write_result = wav_file.write(&header, sizeof(header));
    if (write_result.is_error()) {
        return false;
    }

    return true;
}

}  // namespace ui
