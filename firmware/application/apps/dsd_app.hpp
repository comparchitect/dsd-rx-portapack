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

#ifndef __DSD_APP_H__
#define __DSD_APP_H__

#include "ui_widget.hpp"
#include "ui_receiver.hpp"
#include "ui_freq_field.hpp"
#include "ui_rssi.hpp"
#include "audio.hpp"
#include "baseband_api.hpp"
#include "message.hpp"
#include "file.hpp"
// Define to 1 to enable Audio-to-SD capture UI/logic (disabled to save flash)
#ifndef DSD_AUDIO_TO_SD
#define DSD_AUDIO_TO_SD 0
#endif

#ifndef DEBUG
#define DEBUG 0
#endif

#if DSD_AUDIO_TO_SD
#include "capture_thread.hpp"
#endif

#include <memory>

namespace ui {

class DSDView : public View {
   public:
    DSDView(NavigationView& nav);
    ~DSDView();

    std::string title() const override { return "DSD RX"; }
    void focus() override;

   private:
    void on_stats(const DMRRxStatsMessage* message);
    void on_voice_burst(const AMBEVoiceBurstMessage* message);

    bool send_decode_request(const uint8_t* burst_bytes, uint8_t frame_count);
    bool send_single_frame(const char ambe_frame[4][24]);
    void update_sd_card_availability();
    bool open_log_file();
    void close_log_file();
    bool logging_enabled() const;

    NavigationView& nav_;

    RSSI rssi_{
        {19 * 8 - 4, 3, UI_POS_WIDTH_REMAINING(26), 4}};
    Audio audio_{
        {19 * 8 - 4, 8, UI_POS_WIDTH_REMAINING(26), 4}};

    RFAmpField field_rf_amp_{
        {11 * 8, UI_POS_Y(0)}};
    LNAGainField field_lna_{
        {13 * 8, UI_POS_Y(0)}};
    VGAGainField field_vga_{
        {16 * 8, UI_POS_Y(0)}};

    RxFrequencyField field_frequency_{
        {UI_POS_X(0), 0 * 8},
        nav_};

    AudioVolumeField field_volume_{
        {UI_POS_X_RIGHT(2), UI_POS_Y(0)}};

    Text text_status_{
        {2 * 8, 2 * 16, 20 * 8, 16},
        "Listening"};

    Text text_bursts_label_{
        {2 * 8, 3 * 16, 7 * 8, 16},
        "Bursts:"};

    NumberField field_bursts_{
        {9 * 8, 3 * 16},
        5,
        {0, 99999},
        1,
        ' '};

    Checkbox check_log_to_sd_{
        {2 * 8, 4 * 16},
        12,
        "Log to SD",
        true};

    #if DSD_AUDIO_TO_SD
    Checkbox check_audio_to_sd_{
        {2 * 8, 5 * 16},
        12,
        "Audio to SD",
        true};
    #endif

    #if DEBUG
    Text frame_status_{
        {2 * 8, 6 * 16, 20 * 8, 16},
        ""};
    #endif

    bool sd_card_available_{false};
    File log_file_{};
    bool log_file_open_{false};
    std::string current_log_filename_{};
    std::string session_filename_prefix_{};
    #if DSD_AUDIO_TO_SD
    std::unique_ptr<CaptureThread> audio_capture_thread_{};
    #endif
    uint32_t frames_logged_{0};
    uint32_t frames_error_{0};
    uint32_t frames_received_{0};
    uint32_t bursts_received_{0};
    uint32_t drop_no_base_{0};
    uint32_t drop_midamble_{0};
    int32_t drop_filtered_{0};
    int32_t drop_slot_color_{0};
    int32_t sync_hits_ts1_{0};
    uint32_t execute_overruns_{0};
    uint32_t frame_reset_count_{0};
    bool log_session_started_{false};
    MessageHandlerRegistration message_handler_stats_{
        Message::ID::DMRRxStats,
        [this](const Message* const p) {
            on_stats(reinterpret_cast<const DMRRxStatsMessage*>(p));
        }};

    MessageHandlerRegistration message_handler_voice_{
        Message::ID::AMBEVoiceBurst,
        [this](const Message* const p) {
            on_voice_burst(reinterpret_cast<const AMBEVoiceBurstMessage*>(p));
        }};

    #if DSD_AUDIO_TO_SD
    MessageHandlerRegistration message_handler_audio_capture_done_{
        Message::ID::CaptureThreadDone,
        [this](const Message* const p) {
            const auto* message = reinterpret_cast<const CaptureThreadDoneMessage*>(p);
            handle_capture_thread_error(message->error);
        }};
    #endif

    #if DSD_AUDIO_TO_SD
    bool start_audio_capture();
    void stop_audio_capture();
    bool audio_capture_active() const;
    #endif
    bool ensure_session_filename_prefix();
    void release_session_prefix_if_idle();
    void reset_frame_counters();
#if DEBUG
    void update_frame_status();
#endif
    #if DSD_AUDIO_TO_SD
    void handle_capture_thread_error(uint32_t error_code);
    #endif
    std::string make_timestamp_prefix() const;
};

}  // namespace ui

#endif /*__DSD_APP_H__*/
