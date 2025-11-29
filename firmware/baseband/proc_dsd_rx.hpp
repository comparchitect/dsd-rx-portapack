/*
 * Copyright (C) 2025 comparchitect (https://github.com/comparchitect)
 *
 * This file is part of PortaPack.
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

#ifndef __PROC_DSD_RX_H__
#define __PROC_DSD_RX_H__

#define DSD_AUDIO_TO_SD 0
#define DEBUG 0

#include "audio_output.hpp"
#include "baseband_processor.hpp"
#include "baseband_thread.hpp"
#include "rssi_thread.hpp"

#include "dsp_decimate.hpp"
#include "dsp_demodulate.hpp"
#include "dsp_fir_taps.hpp"

#include "stream_input.hpp"

#include "message.hpp"
#include "portapack_shared_memory.hpp"

#include <array>
#include <memory>
#include <variant>
#include <cstdint>
#include <functional>
#include <vector>

/* A decimator that just returns the source buffer. */
class NoopDecim {
   public:
    static constexpr int decimation_factor = 1;

    template <typename Buffer>
    Buffer execute(const Buffer& src, const Buffer&) {
        return {src.p, src.count, src.sampling_rate};
    }
};

/* Decimator wrapper that can hold one of a set of decimators and dispatch at runtime. */
template <typename... Args>
class MultiDecimator {
   public:
    template <typename Source, typename Destination>
    Destination execute(
        const Source& src,
        const Destination& dst) {
        return std::visit(
            [&src, &dst](auto&& arg) -> Destination {
                return arg.execute(src, dst);
            },
            decimator_);
    }

    size_t decimation_factor() const {
        return std::visit(
            [](auto&& arg) -> size_t {
                return arg.decimation_factor;
            },
            decimator_);
    }

    template <typename Decimator>
    Decimator& set() {
        decimator_ = Decimator{};
        return std::get<Decimator>(decimator_);
    }

   private:
    std::variant<Args...> decimator_{};
};

class DSDRxProcessor : public BasebandProcessor {
   public:
    DSDRxProcessor() { configure_defaults(); }
    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

    enum class SyncPatternId : uint8_t {
        Unknown = 0,
        BsVoice,
        BsData,
        MsVoice,
        MsData,
        DirectTs1Voice,
        DirectTs1Data,
        DirectTs2Voice,
        DirectTs2Data,
        BsVoiceInverted,
        BsDataInverted,
        MsVoiceInverted,
        MsDataInverted,
        DirectTs1VoiceInverted,
        DirectTs1DataInverted,
        DirectTs2VoiceInverted,
        DirectTs2DataInverted
    };

   private:
    static constexpr uint16_t MAX_BUFFER_SIZE{512};

    enum Parse_State {
        Parse_State_Search_Sync = 0,
        Parse_State_Process_Voice,
        Parse_State_Process_Data
    };

    size_t baseband_fs = 3072000;
    uint32_t stat_update_threshold = 200;

    void process_decided_symbol(uint8_t symbol);

    // DSD.test-style functions
    void resetToDefaultState();
    void process_demod_block(const buffer_s16_t& audio);
    void update_symbol_statistics(int32_t symbol_value);
    bool getSymbolFromBuffer(const std::vector<int16_t>& samples,
                             std::size_t& offset,
                             int& sum_out,
                             int& count_out,
                             int32_t& symbol_out);
    void build_sync_window(char* dest) const;
    void push_sync_char(char c);
    void handle_carrier_loss();

    void configure_defaults();
    void send_live_stats();
    
    std::array<complex16_t, MAX_BUFFER_SIZE> dst{};
    const buffer_c16_t dst_buffer{
        dst.data(),
        dst.size()};

    dsp::demodulate::FM demod{};

    // Audio processing for FM demodulation (must hold an entire decimated block)
    std::array<int16_t, MAX_BUFFER_SIZE> audio{};
    const buffer_s16_t audio_buffer{
        audio.data(),
        audio.size()};
    
    AudioOutput audio_output{};

    uint32_t live_total_bursts_{0};
    
    bool configured{false};

    Parse_State parseState_{Parse_State_Search_Sync};
    
    MultiDecimator<
        dsp::decimate::FIRC8xR16x24FS4Decim4,
        dsp::decimate::FIRC8xR16x24FS4Decim8>
        decim_0{};
    MultiDecimator<
        dsp::decimate::FIRC16xR16x16Decim2,
        dsp::decimate::FIRC16xR16x32Decim8,
        NoopDecim>
        decim_1{};

    BasebandThread baseband_thread{baseband_fs, this, baseband::Direction::Receive};
    RSSIThread rssi_thread{};

    // Voice processing state
    uint8_t active_burst_index_{0};
    uint64_t current_burst_start_absolute_{0};
    
    // Burst processing
    static constexpr size_t DMR_CACH_SYMBOLS{12};
    static constexpr size_t DMR_FRAME_SYMBOLS{36};
    static constexpr size_t DMR_FRAME2_HALF_SYMBOLS{18};
    static constexpr size_t DMR_SYNC_SYMBOLS{24};
    static constexpr size_t DMR_CACH_START{0};
    static constexpr size_t DMR_SLOT_TYPE_OFFSET_FROM_CACH{49};
    static constexpr size_t DMR_FRAME1_START{DMR_CACH_START + DMR_CACH_SYMBOLS};
    static constexpr size_t DMR_FRAME2A_START{DMR_FRAME1_START + DMR_FRAME_SYMBOLS};
    static constexpr size_t DMR_SYNC_OFFSET_FROM_BURST_START{DMR_FRAME2A_START + DMR_FRAME2_HALF_SYMBOLS};
    static constexpr size_t DMR_FRAME2B_START{DMR_SYNC_OFFSET_FROM_BURST_START + DMR_SYNC_SYMBOLS};
    static constexpr size_t DMR_FRAME3_START{DMR_FRAME2B_START + DMR_FRAME2_HALF_SYMBOLS};
    static constexpr size_t DMR_BURST_SYMBOLS{DMR_FRAME3_START + DMR_FRAME_SYMBOLS};
    //static constexpr size_t DMR_FINAL_SUPERFRAME_SKIP{65};  // Extra skip after 6th burst (CACH + partial slot-type gap)

    static constexpr size_t DIBIT_BUF_SIZE{288 * 6};
    std::array<uint8_t, DIBIT_BUF_SIZE> dibit_buf_{};
    size_t dibit_buf_index_{0};
    uint8_t voice_dibits_[108]{};
    size_t dibit_index_{0};
    uint32_t sync_search_symbol_count_{0};
    bool carrier_present_{true};
    uint64_t symbol_counter_{0};
    uint64_t absolute_sample_index_{0};
    static constexpr size_t kSyncHistorySize{10240};
    std::array<char, kSyncHistorySize> sync_history_{};
    size_t sync_history_pos_{0};
    size_t sync_history_count_{0};
    int data_sync_hold_symbols_{263};

    // Level tracking buffers (24-symbol window)
    int32_t lbuf1_[24]{};
    int32_t lbuf2_[24]{};
    int lbuf1_pos_{0};
    int32_t lmin_{0};
    int32_t lmax_{0};

    std::array<int32_t, 10> symbol_samples_{};
    int current_symbol_sum_{0};
    int current_symbol_count_{0};
    int jitter_{-1};
    bool jitter_enabled_{false};
    bool dmr_filter_enabled_{false};

    int center_{0};                   // Adaptive threshold center (like dsd.test)
    int umid_{0};                     // Upper mid threshold (like dsd.test)
    int lmid_{0};                     // Lower mid threshold (like dsd.test)
    int32_t max_sample_{15000};
    int32_t min_sample_{-15000};
    int32_t max_ref_{12000};
    int32_t min_ref_{-12000};
    int32_t last_filtered_sample_{0};
    int symbol_center_{4};
    static constexpr int kCarrierLossSymbolLimit{1800};

    std::vector<int16_t> pending_samples_;
    
    // Statistics counter
    uint32_t stat_counter{0};
    uint32_t stats_drop_no_base_{0};
    uint32_t stats_drop_midamble_{0};
    uint32_t stats_drop_filtered_{0};
    uint32_t stats_drop_slot_color_{0};
    uint32_t stats_sync_hits_ts1_{0};
    uint32_t execute_overrun_count_{0};

    #if DSD_AUDIO_TO_SD
    bool capture_to_sd_active_{false};
    #endif

    // Voice data processing
    SyncPatternId decode_sync_string(const char* sync_chars) const;
    void handle_external_voice(const uint8_t* voice_bytes);
};

#endif /*__PROC_DSD_RX_H__*/
