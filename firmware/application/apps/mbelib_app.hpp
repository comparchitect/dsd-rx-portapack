/*
 * Offline MBELIB decoder view.
 */

#ifndef __MBELIB_APP_H__
#define __MBELIB_APP_H__

#include "ui_widget.hpp"
#include "ui_audio.hpp"
#include "ui_rssi.hpp"
#include "ui_receiver.hpp"
#include "ch.h"
#include "audio.hpp"
#include "baseband_api.hpp"
#include "message.hpp"
#include "file.hpp"
#include "replay_thread.hpp"
#include "io_wave.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace ui {

class NavigationView;

class MBELIBView : public View {
   public:
    MBELIBView(NavigationView& nav);
    ~MBELIBView();

    MBELIBView(const MBELIBView&) = delete;
    MBELIBView& operator=(const MBELIBView&) = delete;

    std::string title() const override { return "MBELIB"; }
    void focus() override;

   private:
    void update_sd_card_state();
    void select_file();
    void decode_selected_file();
    void start_decode_thread();
    static msg_t decode_thread_fn(void* arg);
    void decode_thread();
    struct DecodeResult {
        bool success{false};
        bool cancelled{false};
        bool wav_written{false};
        uint32_t frames{0};
        uint32_t samples{0};
        char status[64];
    };
    DecodeResult read_frames_and_decode();
    void handle_decode_complete();
    void close_file();
    void update_play_button();
    bool wav_exists() const;
    void start_wav_playback();
    void stop_wav_playback();
    void on_replay_done(uint32_t return_code);
    bool write_wav_header(File& wav_file, uint32_t sample_count, uint32_t sample_rate);
    void on_pcm_frame(const AMBEPCMFrameMessage& message);
    void on_decode_stats(const AMBE2DecodeStatsMessage& message);
    void finalize_decode_if_ready();
    void update_progress_text();
    void update_ready_status();
    void update_m0_stats_text();
    void close_output_file();
    void upsample_and_smooth(const int16_t* input, size_t input_count, int16_t* output, size_t* output_count);

    NavigationView& nav_;

    // Upsampling state
    int16_t prev_sample_{0};
    size_t frame_count_{0};

    Text text_m0_stats_{
        {2 * 8, 1 * 16, UI_POS_WIDTH_REMAINING(4), 16},
        "M0: idle"};

    Text text_status_{
        {2 * 8, 2 * 16, 20 * 8, 16},
        "Select .ambe file (ext baseband required)"};

    Text text_selected_file_{
        {2 * 8, 3 * 16, UI_POS_WIDTH_REMAINING(4), 16},
        "File: <none>"};

    Button button_select_file_{
        {2 * 8, 5 * 16, 10 * 8, 16},
        "Select"};

    Button button_decode_{
        {14 * 8, 5 * 16, 10 * 8, 16},
        "Decode"};

    Button button_play_wav_{
        {2 * 8, 6 * 16, 10 * 8, 16},
        "Play WAV"};

    RSSI rssi_{
        {19 * 8 - 4, 3, UI_POS_WIDTH_REMAINING(26), 4}};

    Audio audio_{
        {19 * 8 - 4, 8, UI_POS_WIDTH_REMAINING(26), 4}};

    AudioVolumeField field_volume_{
        {UI_POS_X_RIGHT(2), UI_POS_Y(0)}};

    bool sd_card_available_{false};
    std::filesystem::path selected_file_{};
    std::filesystem::path wav_file_{};
    File input_file_{};
    File output_file_{};
    bool file_open_{false};
    bool output_ready_{false};
    bool wav_available_{false};
    bool decode_in_progress_{false};
    std::atomic<bool> decode_abort_{false};
    Thread* decode_thread_{nullptr};
    DecodeResult decode_result_{};
    uint32_t frames_sent_{0};
    uint32_t frames_completed_{0};
    uint32_t total_samples_written_{0};
    uint32_t frame_error_count_{0};
    bool decode_thread_finished_{false};
    bool m4_completion_ack_received_{false};
    bool decode_finalized_{false};
    uint32_t frames_processed_latest_{0};
    uint32_t total_frames_expected_{0};
    uint32_t frames_in_flight_{0};
    uint32_t max_frames_in_flight_{0};
    uint32_t frames_read_total_{0};
    uint32_t read_error_count_{0};
    uint32_t m4_pcm_dropped_{0};
    bool is_playing_{false};
    bool ready_signal_{false};
    std::unique_ptr<ReplayThread> replay_thread_{};
    Mutex file_io_mutex_{};
    MessageHandlerRegistration replay_done_handler_{
        Message::ID::ReplayThreadDone,
        [this](const Message* const p) {
            const auto message = *reinterpret_cast<const ReplayThreadDoneMessage*>(p);
            on_replay_done(message.return_code);
        }};
    MessageHandlerRegistration request_signal_handler_{
        Message::ID::RequestSignal,
        [this](const Message* const p) {
            const auto message = static_cast<const RequestSignalMessage*>(p);
            switch (message->signal) {
                case RequestSignalMessage::Signal::FillRequest:
                    if (replay_thread_) {
                        ready_signal_ = true;
                    }
                    break;
                case RequestSignalMessage::Signal::AmbeDecodeDone:
                    handle_decode_complete();
                    break;
                case RequestSignalMessage::Signal::AmbeDecodeProgress:
                    update_progress_text();
                    update_m0_stats_text();
                    break;
                case RequestSignalMessage::Signal::AmbeDecodeHostStats:
                    update_m0_stats_text();
                    break;
                default:
                    break;
            }
        }};
    MessageHandlerRegistration pcm_frame_handler_{
        Message::ID::AMBEPCMFrame,
        [this](const Message* const p) {
            const auto& message = *reinterpret_cast<const AMBEPCMFrameMessage*>(p);
            on_pcm_frame(message);
        }};
    MessageHandlerRegistration decode_stats_handler_{
        Message::ID::AMBE2DecodeStats,
        [this](const Message* const p) {
            const auto& message = *reinterpret_cast<const AMBE2DecodeStatsMessage*>(p);
            on_decode_stats(message);
        }};
};

}  // namespace ui

#endif /* __MBELIB_APP_H__ */
