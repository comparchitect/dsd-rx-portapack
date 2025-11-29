/*
 * Copyright (C) 2025 comparchitect (https://github.com/comparchitect)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "dsd_app.hpp"

#include "audio.hpp"
#include "capture_thread.hpp"
#include "ch.h"
#include "baseband_api.hpp"
#include "event_m0.hpp"
#include "io_file.hpp"
#include "portapack.hpp"
#include "sd_card.hpp"
#include "string_format.hpp"
#include "rtc_time.hpp"
#include "file_path.hpp"
#include "apps/ambe_log_format.hpp"
#include "apps/ambe_processing.hpp"

#include <algorithm>
#include <cstring>

#ifndef DSDRX_USE_PREPARED_IMAGE
#define DSDRX_USE_PREPARED_IMAGE 1
#endif

using namespace portapack;

namespace ui {

namespace {
constexpr uint64_t kDefaultFrequency = 435000000ULL;
constexpr size_t kAudioCaptureWriteSize = 2048;
constexpr size_t kAudioCaptureBufferCount = 8;
constexpr uint8_t kMaxLoggedFrameErrors = 12;  // Frames with more errors still get logged
}  // namespace

DSDView::DSDView(NavigationView& nav)
    : nav_{nav} {
    if (DSDRX_USE_PREPARED_IMAGE) {
        baseband::run_prepared_image(portapack::memory::map::m4_code.base());
    } else {
        baseband::run_image(portapack::spi_flash::image_tag_dsd_rx);
    }

    add_children({&rssi_,
                  &audio_,
                  &field_rf_amp_,
                  &field_lna_,
                  &field_vga_,
                  &field_frequency_,
                  &field_volume_,
                  &text_status_,
                  &text_bursts_label_,
                  &field_bursts_,
                  &check_log_to_sd_,
                  #if DSD_AUDIO_TO_SD
                  &check_audio_to_sd_,
                  #endif
                  #if DEBUG
                  &frame_status_,
                  #endif
                  });

    audio::set_rate(audio::Rate::Hz_12000);
    audio::output::start();
    audio::output::unmute();
    audio::output::speaker_unmute();
    audio::output::update_audio_mute();
    receiver_model.enable();
    receiver_model.set_target_frequency(kDefaultFrequency);
    field_frequency_.set_value(kDefaultFrequency);
    receiver_model.set_modulation(ReceiverModel::Mode::NarrowbandFMAudio);
    receiver_model.set_baseband_bandwidth(1750000);
    text_status_.set("Listening");

    check_log_to_sd_.hidden(true);
    check_log_to_sd_.set_value(false);
    check_log_to_sd_.on_select = [this](Checkbox&, bool value) {
        if (value) {
            update_sd_card_availability();
            if (!sd_card_available_ || !open_log_file()) {
                check_log_to_sd_.set_value(false);
            }
        } else {
            close_log_file();
        }
    };

    #if DSD_AUDIO_TO_SD
    check_audio_to_sd_.hidden(true);
    check_audio_to_sd_.set_value(false);
    check_audio_to_sd_.on_select = [this](Checkbox&, bool value) {
        if (value) {
            update_sd_card_availability();
            if (!sd_card_available_ || !start_audio_capture()) {
                check_audio_to_sd_.set_value(false);
            }
        } else {
            stop_audio_capture();
        }
    };
    #endif

    update_sd_card_availability();
}

DSDView::~DSDView() {
    #if DSD_AUDIO_TO_SD
    stop_audio_capture();
    #endif
    close_log_file();
    baseband::shutdown();
    receiver_model.disable();
    audio::output::speaker_mute();
    audio::output::stop();
}

void DSDView::focus() {
    update_sd_card_availability();
    field_frequency_.focus();
}

void DSDView::on_stats(const DMRRxStatsMessage* message) {
    if (!message) {
        return;
    }
    field_bursts_.set_value(static_cast<int32_t>(message->bursts));
#if DEBUG
    drop_no_base_ = message->drops_no_base;
    drop_midamble_ = message->drops_midamble;
    drop_filtered_ = message->drops_filtered;
    drop_slot_color_ = message->drops_slot_color;
    sync_hits_ts1_ = message->sync_hits_ts1;
    execute_overruns_ = message->execute_overruns;
    update_frame_status();
#endif
}

void DSDView::on_voice_burst(const AMBEVoiceBurstMessage* message) {
    if (!message) {
        return;
    }

    update_sd_card_availability();

    const uint8_t frames = std::min<uint8_t>(message->frame_count, AMBEVoiceBurstMessage::kMaxFrames);
    if (frames == 0) {
        return;
    }

    ++bursts_received_;
#if DEBUG
    update_frame_status();
#endif

    if (!send_decode_request(message->data, frames)) {
        text_status_.set("Frame error");
    }
}

bool DSDView::send_decode_request(const uint8_t* burst_bytes, uint8_t frame_count) {
    if (!burst_bytes || frame_count == 0) {
        return true;
    }

    frame_count = std::min<uint8_t>(frame_count, AMBEVoiceBurstMessage::kMaxFrames);

    frames_received_ += frame_count;
#if DEBUG
    update_frame_status();
#endif

    char ambe_frames[AMBEVoiceBurstMessage::kMaxFrames][4][24];
    ambe_processing::deinterleave_ambe_burst(burst_bytes, ambe_frames);

    for (uint8_t frame = 0; frame < frame_count; ++frame) {
        if (!send_single_frame(ambe_frames[frame])) {
            return false;
        }
    }

    return true;
}

bool DSDView::send_single_frame(const char ambe_frame[4][24]) {
    char ambe_fr_raw[4][24];
    std::memcpy(ambe_fr_raw, ambe_frame, sizeof(ambe_fr_raw));

    if (logging_enabled()) {
        char ambe_fr_capture[4][24];
        std::memcpy(ambe_fr_capture, ambe_fr_raw, sizeof(ambe_fr_capture));
        char ambe_d[49]{};
        int errs2 = 0;
        ambe_processing::sanitize_frame(ambe_fr_capture, ambe_d, &errs2);
        if (errs2 > static_cast<int>(kMaxLoggedFrameErrors)) {
            ++frames_error_;
        }
        auto packed = ambe_processing::pack_frame(ambe_fr_capture, static_cast<uint8_t>(errs2));

        auto write_result = log_file_.write(packed);
        if (!write_result.is_ok() || *write_result != packed.size()) {
            text_status_.set("Log write err");
            check_log_to_sd_.set_value(false);
            close_log_file();
            return false;
        }
        ++frames_logged_;
        if (errs2 > static_cast<int>(kMaxLoggedFrameErrors)) {
#if DEBUG            
            update_frame_status();
#endif
        }
        if ((frames_logged_ % 50u) == 0u) {
            log_file_.sync();
        }
#if DEBUG
        update_frame_status();
#endif
    }

    return true;
}

/*
bool DSDView::send_single_frame(const char ambe_frame[4][24]) {
    char ambe_fr_raw[4][24];
    std::memcpy(ambe_fr_raw, ambe_frame, sizeof(ambe_fr_raw));

    if (logging_enabled()) {
        // Pack the raw AMBE frame without ECC/demodulate
        constexpr uint8_t errs2 = 0;
        auto packed = ambe_processing::pack_frame(ambe_fr_raw, errs2);

        auto write_result = log_file_.write(packed);
        if (!write_result.is_ok() || *write_result != packed.size()) {
            text_status_.set("Log write err");
            check_log_to_sd_.set_value(false);
            close_log_file();
            return false;
        }

        ++frames_logged_;
        if ((frames_logged_ % 50u) == 0u) {
            log_file_.sync();
        }
#if DEBUG
        update_frame_status();
#endif
    }
    return true;
}
*/

void DSDView::update_sd_card_availability() {
    const bool available = (sd_card::status() == sd_card::Status::Mounted);
    if (available != sd_card_available_) {
        sd_card_available_ = available;
        check_log_to_sd_.hidden(!sd_card_available_);
        #if DSD_AUDIO_TO_SD
        check_audio_to_sd_.hidden(!sd_card_available_);
        #endif
        if (!sd_card_available_) {
            if (check_log_to_sd_.value()) {
                check_log_to_sd_.set_value(false);
            }
            #if DSD_AUDIO_TO_SD
            if (check_audio_to_sd_.value()) {
                check_audio_to_sd_.set_value(false);
            }
            #endif
            close_log_file();
        }
    }
}

bool DSDView::open_log_file() {
    if (!sd_card_available_) {
        text_status_.set("SD card not ready");
        return false;
    }

    const auto dir_result = ensure_directory(captures_dir);
    if (dir_result.code() != FR_OK) {
        text_status_.set("Dir error: " + dir_result.what());
        return false;
    }

    if (!ensure_session_filename_prefix()) {
        return false;
    }

    std::string filename = session_filename_prefix_ + ".ambe";

    auto create_result = log_file_.create(captures_dir / filename);
    if (create_result) {
        text_status_.set("File create failed");
        release_session_prefix_if_idle();
        return false;
    }

    const ambe_log::Header header = ambe_log::make_header();
    auto write_header = log_file_.write(&header, sizeof(header));
    if (!write_header.is_ok() || *write_header != sizeof(header)) {
        text_status_.set("Header write err");
        log_file_.close();
        release_session_prefix_if_idle();
        return false;
    }

    log_file_open_ = true;
    current_log_filename_ = filename;
    reset_frame_counters();
    text_status_.set("Logging " + filename);
    return true;
}

void DSDView::close_log_file() {
    if (log_file_open_) {
        log_file_.sync();
        log_file_.close();
        log_file_open_ = false;
        current_log_filename_.clear();
        log_session_started_ = false;
    }
    release_session_prefix_if_idle();
}

void DSDView::reset_frame_counters() {
    if (!log_session_started_) {
        frame_reset_count_ = 0;
        log_session_started_ = true;
    } else {
        ++frame_reset_count_;
    }
    frames_logged_ = 0;
    frames_error_ = 0;
    frames_received_ = 0;
    bursts_received_ = 0;
}

#if DEBUG
void DSDView::update_frame_status() {
    text_status_.set("D" +
                     to_string_dec_uint(drop_no_base_) + "/" +
                     to_string_dec_uint(drop_midamble_)+ "/ovr" +
                     to_string_dec_uint(execute_overruns_));
    frame_status_.set(to_string_dec_int(drop_filtered_) + "/" +
                     to_string_dec_int(drop_slot_color_) + "/" +
                     to_string_dec_int(sync_hits_ts1_));
}
#endif

bool DSDView::logging_enabled() const {
    return sd_card_available_ && check_log_to_sd_.value() && log_file_open_;
}

#if DSD_AUDIO_TO_SD
bool DSDView::start_audio_capture() {
    if (audio_capture_active()) {
        return true;
    }

    if (!sd_card_available_) {
        text_status_.set("SD card not ready");
        return false;
    }

    const auto dir_result = ensure_directory(captures_dir);
    if (dir_result.code() != FR_OK) {
        text_status_.set("Dir error: " + dir_result.what());
        return false;
    }

    if (!ensure_session_filename_prefix()) {
        return false;
    }

    const auto raw_path = captures_dir / (session_filename_prefix_ + ".raw");
    auto writer = std::make_unique<RawFileWriter>();
    if (const auto create_error = writer->create(raw_path); create_error.is_valid()) {
        text_status_.set("Audio file err");
        release_session_prefix_if_idle();
        return false;
    }

    audio_capture_thread_ = std::make_unique<CaptureThread>(
        std::move(writer),
        kAudioCaptureWriteSize,
        kAudioCaptureBufferCount,
        std::function<void()>{},
        [this](File::Error error) {
            CaptureThreadDoneMessage message{error.code()};
            EventDispatcher::send_message(message);
        });

    return true;
}

void DSDView::stop_audio_capture() {
    if (!audio_capture_active()) {
        return;
    }

    audio_capture_thread_.reset();
    release_session_prefix_if_idle();
}

bool DSDView::audio_capture_active() const {
    return static_cast<bool>(audio_capture_thread_);
}
#endif // DSD_AUDIO_TO_SD

bool DSDView::ensure_session_filename_prefix() {
    if (!session_filename_prefix_.empty()) {
        return true;
    }

    session_filename_prefix_ = make_timestamp_prefix();
    return true;
}

void DSDView::release_session_prefix_if_idle() {
    #if DSD_AUDIO_TO_SD
    const bool audio_active = audio_capture_active();
    #else
    const bool audio_active = false;
    #endif
    if (!log_file_open_ && !audio_active) {
        session_filename_prefix_.clear();
    }
}

#if DSD_AUDIO_TO_SD
void DSDView::handle_capture_thread_error(uint32_t error_code) {
    if (!audio_capture_active()) {
        return;
    }

    text_status_.set("Audio write err: " + to_string_dec_uint(error_code));
    check_audio_to_sd_.set_value(false);
}
#endif // DSD_AUDIO_TO_SD

std::string DSDView::make_timestamp_prefix() const {
    rtc::RTC datetime;
    rtc_time::now(datetime);
    return to_string_dec_uint(datetime.year(), 4, '0') + "." +
           to_string_dec_uint(datetime.month(), 2, '0') + "." +
           to_string_dec_uint(datetime.day(), 2, '0') + "-" +
           to_string_dec_uint(datetime.hour(), 2, '0') +
           to_string_dec_uint(datetime.minute(), 2, '0');
}


}  // namespace ui
