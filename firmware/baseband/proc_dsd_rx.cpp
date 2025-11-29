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

#include "proc_dsd_rx.hpp"
#include "dsp_decimate.hpp"
#include "audio_dma.hpp"

#include "event_m4.hpp"
#include "message.hpp"
#include "dsp_iir_config.hpp"
#include "dsp_fir_taps.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <cstdlib>
#include <vector>

#include "mathdef.hpp"

namespace {

using SyncPatternId = DSDRxProcessor::SyncPatternId;

struct SyncPatternDescriptor {
    SyncPatternId id;
    const char* pattern;
};

constexpr std::array<SyncPatternDescriptor, 8> kSyncPatterns{{
    {SyncPatternId::DirectTs1Voice, "113111131333131311133333"},
    {SyncPatternId::DirectTs1Data, "331333313111313133311111"},
    {SyncPatternId::DirectTs2Voice, "133133333111331111311133"},
    {SyncPatternId::DirectTs2Data, "311311111333113333133311"},
    {SyncPatternId::BsVoice, "131111333113313313113313"},
    {SyncPatternId::BsData, "313131111133331113111133"},
    {SyncPatternId::MsVoice, "133313311131311113313331"},
    {SyncPatternId::MsData, "331333113133111133331111"}
}};

int compare_int32(const void* a, const void* b) {
    const auto av = *static_cast<const int32_t*>(a);
    const auto bv = *static_cast<const int32_t*>(b);
    if (av < bv) return -1;
    if (av > bv) return 1;
    return 0;
}

constexpr int kDmrNZeros = 60;
static constexpr int32_t dmr_coeffs_q23[61] = {
    37032,
    33065,
    19611,
    -1611,
    -26605,
    -49737,
    -64869,
    -66786,
    -52609,
    -22867,
    18080,
    62446,
    100273,
    121365,
    117566,
    84935,
    25326,
    -53007,
    -136037,
    -205827,
    -243380,
    -232032,
    -160770,
    -26851,
    162827,
    391930,
    636544,
    868433,
    1059184,
    1184549,
    1228249,
    1184549,
    1059184,
    868433,
    636544,
    391930,
    162827,
    -26851,
    -160770,
    -232032,
    -243380,
    -205827,
    -136037,
    -53007,
    25326,
    84935,
    117566,
    121365,
    100273,
    62446,
    18080,
    -22867,
    -52609,
    -66786,
    -64869,
    -49737,
    -26605,
    -1611,
    19611,
    33065,
    37032
};

// Integer delay line: same as before, raw input samples
static int16_t dmr_v[kDmrNZeros + 1]{};

inline int16_t saturate_to_i16(int32_t x)
{
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(x);
}

// ------------------------------------------------------------------
// Drop-in replacement: higher-precision fixed-point version
// ------------------------------------------------------------------
inline int16_t dmr_filter(int16_t sample)
{
    // Shift delay line
    for (int i = 0; i < kDmrNZeros; ++i) {
        dmr_v[i] = dmr_v[i + 1];
    }
    dmr_v[kDmrNZeros] = sample;

    // 64-bit accumulator, value in Q23
    int64_t acc = 0;

    for (int i = 0; i <= kDmrNZeros; ++i) {
        acc += static_cast<int64_t>(dmr_coeffs_q23[i]) *
               static_cast<int64_t>(dmr_v[i]);
    }

    // acc is Q23 (coeff Q23 * sample Q0), shift down to Q0
    int32_t y = static_cast<int32_t>(acc >> 23);

    return saturate_to_i16(y);
}

}

void DSDRxProcessor::resetToDefaultState() {
    dibit_buf_index_ = 0;
    stat_counter = 0;
    parseState_ = Parse_State_Search_Sync;
    active_burst_index_ = 0;
    current_burst_start_absolute_ = 0;
    live_total_bursts_ = 0;
    sync_search_symbol_count_ = 0;
    carrier_present_ = true;
    symbol_counter_ = 0;
    absolute_sample_index_ = 0;
    //data_sync_hold_symbols_ = 0;
    //dibit_index_ = 0;
    //std::fill(std::begin(voice_dibits_), std::end(voice_dibits_), 0);

    stats_drop_no_base_ = 0;
    stats_drop_midamble_ = 0;
    stats_drop_filtered_ = 0;
    stats_drop_slot_color_ = 0;

    symbol_samples_.fill(0);
    current_symbol_sum_ = 0;
    current_symbol_count_ = 0;
    center_ = 0;
    umid_ = 0;
    lmid_ = 0;
    max_sample_ = 15000;
    min_sample_ = -15000;
    max_ref_ = 12000;
    min_ref_ = -12000;
    last_filtered_sample_ = 0;
    symbol_center_ = 4;
    jitter_ = -1;
    jitter_enabled_ = false;
    dmr_filter_enabled_ = false;

    //std::fill(std::begin(lbuf1_), std::end(lbuf1_), 0);
    //std::fill(std::begin(lbuf2_), std::end(lbuf2_), 0);
    lbuf1_pos_ = 0;
    lmin_ = 0;
    lmax_ = 0;

    sync_history_.fill('0');
    sync_history_pos_ = 0;
    sync_history_count_ = 0;
    //pending_samples_.clear();
}

void DSDRxProcessor::execute(const buffer_c8_t& buffer) {
    if (!configured) { return; }

    static bool execute_running = false;
    if (execute_running) {
        execute_overrun_count_++;
        return;
    }
    execute_running = true;

    // Count how many execute() blocks have run (for debugging).
    //stats_drop_midamble_++;

    // Basic signal processing (decimation, AGC, filtering) - similar to original
    const auto decim_0_out = decim_0.execute(buffer, dst_buffer);
    auto decim_1_out = decim_1.execute(decim_0_out, dst_buffer);

    // FM demodulation for symbol processing
    auto audio_out = demod.execute(decim_1_out, audio_buffer);

    #if DSD_AUDIO_TO_SD
    if (capture_to_sd_active_) {
        // When Audio-to-SD is enabled, write raw demodulated audio
        // to the capture stream only, and skip symbol processing.
        audio_output.write(audio_out);
    } else
    #endif
    {
        process_demod_block(audio_out);
        audio_output.write(audio_out);
    }

#if DEBUG
    // Update stats periodically
    stat_counter++;
    if (stat_counter >= stat_update_threshold) {
        send_live_stats();
        stat_counter = 0;
    }
#endif

    execute_running = false;
}

void DSDRxProcessor::on_message(const Message* const message) {
    switch (message->id) {
        case Message::ID::CaptureConfig: {
            const auto* cfg = reinterpret_cast<const CaptureConfigMessage*>(message);
            if (cfg->config) {
                audio_output.set_stream(std::make_unique<StreamInput>(cfg->config));
                #if DSD_AUDIO_TO_SD
                capture_to_sd_active_ = true;
                #endif
            } else {
                audio_output.set_stream(nullptr);
                #if DSD_AUDIO_TO_SD
                capture_to_sd_active_ = false;
                #endif
            }
            break;
        }

        default:
            break;
    }
}

void DSDRxProcessor::configure_defaults() {
    baseband_fs = 3072000;
    baseband_thread.set_sampling_rate(baseband_fs);

    // DMR chain: 3.072 MHz -> 384 kHz -> 48 kHz (SPS ≈ 10)
    decim_0.set<dsp::decimate::FIRC8xR16x24FS4Decim8>().configure(taps_dmr_decim_0.taps);
    decim_1.set<dsp::decimate::FIRC16xR16x32Decim8>().configure(taps_dmr_decim_1.taps);

    symbol_center_ = 4;

    //demod.configure(48000, 6000.0f);
    demod.configure(48000, 5000.0f);

    resetToDefaultState();
    dibit_buf_.fill(0);

    // No squelch, no filtering, just pure output
    audio_output.configure(false);
   
    configured = true;
}

void DSDRxProcessor::process_decided_symbol(uint8_t dibit) {
    dibit_buf_[dibit_buf_index_] = dibit;
    dibit_buf_index_ = (dibit_buf_index_ + 1) % DIBIT_BUF_SIZE;

    if (parseState_ == Parse_State_Search_Sync) {
        ++sync_search_symbol_count_;
        SyncPatternId match_id = SyncPatternId::Unknown;

        if (sync_history_count_ >= DMR_SYNC_SYMBOLS &&
            (max_ref_ != max_sample_ || min_ref_ != min_sample_)) {
            max_ref_ = max_sample_;
            min_ref_ = min_sample_;
        }

        char sync_window[DMR_SYNC_SYMBOLS + 1]{};
        std::fill_n(sync_window, DMR_SYNC_SYMBOLS + 1, '\0');
        if (sync_history_count_ >= DMR_SYNC_SYMBOLS) {
            build_sync_window(sync_window);
            match_id = decode_sync_string(sync_window);
        }

        if (match_id == SyncPatternId::DirectTs1Voice || match_id == SyncPatternId::DirectTs1Data) {
            carrier_present_ = true;
            sync_search_symbol_count_ = 0;

            std::memcpy(lbuf2_, lbuf1_, sizeof(lbuf1_));
            ::qsort(lbuf2_, 24, sizeof(int32_t), compare_int32);
            lmin_ = (lbuf2_[1] + lbuf2_[2] + lbuf2_[3]) / 3;
            const int t_max = 24;
            lmax_ = (lbuf2_[t_max - 3] + lbuf2_[t_max - 2] + lbuf2_[t_max - 1]) / 3;

            max_sample_ = (max_sample_ + lmax_) / 2;
            min_sample_ = (min_sample_ + lmin_) / 2;
            center_ = (max_sample_ + min_sample_) / 2;
            umid_ = (((max_sample_ - center_) * 5) / 8) + center_;
            lmid_ = (((min_sample_ - center_) * 5) / 8) + center_;
            max_ref_ = max_sample_;
            min_ref_ = min_sample_;

            jitter_enabled_ = true;
            dmr_filter_enabled_ = true;

            if (match_id == SyncPatternId::DirectTs1Voice) {
                current_burst_start_absolute_ =
                    (symbol_counter_ >= 90) ? symbol_counter_ - 90 : 0;
                active_burst_index_ = 0;
                dibit_index_ = 0;
                parseState_ = Parse_State_Process_Voice;
                sync_search_symbol_count_ = 0;
            } else {
                parseState_ = Parse_State_Process_Data;
                data_sync_hold_symbols_ = 263;
            }

            stats_sync_hits_ts1_++;
        }

        if (sync_search_symbol_count_ >= static_cast<uint32_t>(kCarrierLossSymbolLimit)) {
            handle_carrier_loss();
            return;
        }
    } else if (parseState_ == Parse_State_Process_Voice) {
        const uint16_t offset = static_cast<uint16_t>(symbol_counter_ - current_burst_start_absolute_);

        auto wrap_index = [&](int idx) -> std::size_t {
            const int mod = idx % static_cast<int>(DIBIT_BUF_SIZE);
            return static_cast<std::size_t>((mod < 0) ? mod + static_cast<int>(DIBIT_BUF_SIZE) : mod);
        };

        auto append_dibits = [&](int start, std::size_t count) {
            for (std::size_t i = 0; i < count && dibit_index_ < 108; ++i) {
                const auto buf_index = wrap_index(start + static_cast<int>(i));
                voice_dibits_[dibit_index_++] = dibit_buf_[buf_index];
            }
        };

        const int burst_offset = static_cast<int>(active_burst_index_) * 288;
        if (offset == static_cast<uint16_t>(91 + burst_offset)) {
            append_dibits(static_cast<int>(dibit_buf_index_) -
                              static_cast<int>(DMR_FRAME_SYMBOLS + DMR_FRAME2_HALF_SYMBOLS + DMR_SYNC_SYMBOLS + 1),
                          DMR_FRAME_SYMBOLS);
            append_dibits(static_cast<int>(dibit_buf_index_) -
                              static_cast<int>(DMR_FRAME2_HALF_SYMBOLS + DMR_SYNC_SYMBOLS + 1),
                          DMR_FRAME2_HALF_SYMBOLS);
        } else if (offset == static_cast<uint16_t>(108 + burst_offset)) {
            append_dibits(static_cast<int>(dibit_buf_index_) -
                              static_cast<int>(DMR_FRAME2_HALF_SYMBOLS),
                          DMR_FRAME2_HALF_SYMBOLS);
        } else if (offset == static_cast<uint16_t>(144 + burst_offset)) {
            append_dibits(static_cast<int>(dibit_buf_index_) -
                              static_cast<int>(DMR_FRAME_SYMBOLS),
                          DMR_FRAME_SYMBOLS);

            uint8_t burst_bytes[27]{};
            for (int i = 0; i < 108; ++i) {
                const uint8_t temp = voice_dibits_[i] & 0x03u;
                const int byte_index = i / 4;
                const int slot = i % 4;
                const int shift = 6 - (slot * 2);
                if (!slot) {
                    burst_bytes[byte_index] = 0;
                }
                burst_bytes[byte_index] |= static_cast<uint8_t>(temp << shift);
            }
            handle_external_voice(burst_bytes);

            if (active_burst_index_ == 5) {
                parseState_ = Parse_State_Process_Data;
                data_sync_hold_symbols_ = 209;
                sync_search_symbol_count_ = 0;
            } else {
                active_burst_index_++;
                dibit_index_ = 0;
            }
        }
    } else if (parseState_ == Parse_State_Process_Data) {
        --data_sync_hold_symbols_;
        if (data_sync_hold_symbols_ <= 0) {
            parseState_ = Parse_State_Search_Sync;
            sync_search_symbol_count_ = 0;
        }
    }
}

void DSDRxProcessor::send_live_stats() {
    // Always emit stats so UI reflects live totals even if audio is muted
    DMRRxStatsMessage message{
        live_total_bursts_,
        static_cast<uint32_t>(symbol_counter_),
        static_cast<uint32_t>(parseState_),//stats_drop_midamble_,
        static_cast<int32_t>(min_ref_),//stats_drop_filtered_,
        static_cast<int32_t>(center_),//stats_drop_slot_color_,
        static_cast<int32_t>(max_ref_),//stats_sync_hits_ts1_,
        execute_overrun_count_};
    shared_memory.application_queue.push(message);
}

DSDRxProcessor::SyncPatternId DSDRxProcessor::decode_sync_string(const char* sync_chars) const {
    if (!sync_chars) {
        return SyncPatternId::Unknown;
    }

    auto matches = [&](const char* pattern) {
        if (!pattern) {
            return false;
        }
        for (int i = 0; i < DMR_SYNC_SYMBOLS; ++i) {
            if (sync_chars[i] != pattern[i]) {
                return false;
            }
        }
        return true;
    };

    auto get_pattern = [&](SyncPatternId id) -> const char* {
        for (const auto& desc : kSyncPatterns) {
            if (desc.id == id) {
                return desc.pattern;
            }
        }
        return nullptr;
    };

    const char* pat_ts1_voice = get_pattern(SyncPatternId::DirectTs1Voice);
    if (matches(pat_ts1_voice)) {
        return SyncPatternId::DirectTs1Voice;
    }

    const char* pat_ts1_data = get_pattern(SyncPatternId::DirectTs1Data);
    if (matches(pat_ts1_data)) {
        return SyncPatternId::DirectTs1Data;
    }

    const char* pat_ts2_voice = get_pattern(SyncPatternId::DirectTs2Voice);
    if (matches(pat_ts2_voice)) {
        return SyncPatternId::DirectTs2Voice;
    }

    const char* pat_ts2_data = get_pattern(SyncPatternId::DirectTs2Data);
    if (matches(pat_ts2_data)) {
        return SyncPatternId::DirectTs2Data;
    }

    const char* pat_bs_voice = get_pattern(SyncPatternId::BsVoice);
    if (matches(pat_bs_voice)) {
        return SyncPatternId::BsVoice;
    }

    const char* pat_bs_data = get_pattern(SyncPatternId::BsData);
    if (matches(pat_bs_data)) {
        return SyncPatternId::BsData;
    }

    const char* pat_ms_voice = get_pattern(SyncPatternId::MsVoice);
    if (matches(pat_ms_voice)) {
        return SyncPatternId::MsVoice;
    }

    const char* pat_ms_data = get_pattern(SyncPatternId::MsData);
    if (matches(pat_ms_data)) {
        return SyncPatternId::MsData;
    }

    return SyncPatternId::Unknown;
}

void DSDRxProcessor::handle_external_voice(const uint8_t* voice_bytes) {
    if (!voice_bytes) {
        return;
    }

    live_total_bursts_++;

    AMBEVoiceBurstMessage message{
        voice_bytes,
        AMBEVoiceBurstMessage::kMaxFrames};
    shared_memory.application_queue.push(message);

    send_live_stats();
}

void DSDRxProcessor::process_demod_block(const buffer_s16_t& audio) {
    // Instrument input block size for debugging.
    stats_drop_filtered_ = audio.count;

    std::vector<int16_t> samples;
    samples.reserve(pending_samples_.size() + audio.count);
    samples.insert(samples.end(), pending_samples_.begin(), pending_samples_.end());
    samples.insert(samples.end(), audio.p, audio.p + audio.count);
    pending_samples_.clear();

    const std::size_t samples_per_symbol = symbol_samples_.size();
    constexpr std::size_t kSymbolMargin = 10;
    const std::size_t min_samples = samples_per_symbol + kSymbolMargin;

    std::size_t offset = 0;
    uint32_t symbols_this_block = 0;
    while (samples.size() - offset >= min_samples) {
        int sum = 0;
        int count = 0;
        int32_t symbol = 0;
        if (!getSymbolFromBuffer(samples, offset, sum, count, symbol)) {
            break;
        }

        update_symbol_statistics(symbol);
        const char sign_char = (symbol > center_) ? '1' : '3';
        push_sync_char(sign_char);

        uint8_t dibit;
        if (symbol > center_) {
            dibit = (symbol > umid_) ? 0b01u : 0b00u;
        } else {
            dibit = (symbol < lmid_) ? 0b11u : 0b10u;
        }
        process_decided_symbol(dibit & 0x03u);
        ++symbols_this_block;
    }

    // Expose how many symbols were processed in this block.
    stats_drop_slot_color_ = symbols_this_block;

    pending_samples_.assign(samples.begin() + offset, samples.end());
}

bool DSDRxProcessor::getSymbolFromBuffer(const std::vector<int16_t>& samples,
                                         std::size_t& offset,
                                         int& sum_out,
                                         int& count_out,
                                         int32_t& symbol_out) {
    const int samples_per_symbol = static_cast<int>(symbol_samples_.size());
    if (samples.size() - offset < static_cast<std::size_t>(samples_per_symbol)) {
        return false;
    }

    sum_out = 0;
    count_out = 0;

    for (int loop_i = 0; loop_i < samples_per_symbol; loop_i++) {
        if (loop_i == 0 && parseState_ == Parse_State_Search_Sync) {
            if (jitter_ >= symbol_center_ - 1 && jitter_ <= symbol_center_) {
                loop_i--;
            } else if (jitter_ >= symbol_center_ + 1 && jitter_ <= symbol_center_ + 2) {
                loop_i++;
            }
            jitter_ = -1;
        }

        if (offset >= samples.size()) {
            return false;
        }

        int16_t pre_filter_sample = samples[offset++];
        absolute_sample_index_++;

        int16_t filtered_sample = pre_filter_sample;
        if (dmr_filter_enabled_) {
            filtered_sample = dmr_filter(filtered_sample);
        }
        const int32_t filtered_int = static_cast<int32_t>(filtered_sample);

        if (jitter_ < 0) {
            const int32_t maxref_scaled = (max_ref_ * 5) / 4;
            const int32_t minref_scaled = (min_ref_ * 5) / 4;

            bool should_set = false;
            if (filtered_int > center_) {
                if (filtered_int <= maxref_scaled && last_filtered_sample_ < center_) {
                    should_set = true;
                }
            } else {
                if (filtered_int >= minref_scaled && last_filtered_sample_ > center_) {
                    should_set = true;
                }
            }

            if (should_set) {
                jitter_ = loop_i;
            }
        }

        if ((loop_i == symbol_center_ - 1 ||
             loop_i == symbol_center_ + 1)) {
            sum_out += filtered_int;
            ++count_out;
        }

        last_filtered_sample_ = filtered_int;
    }

    const int divisor = (count_out > 0) ? count_out : 1;
    symbol_out = sum_out / divisor;
    ++symbol_counter_;
    return true;
}

void DSDRxProcessor::build_sync_window(char* dest) const {
    if (!dest) {
        return;
    }
    if (sync_history_count_ < DMR_SYNC_SYMBOLS) {
        std::fill(dest, dest + DMR_SYNC_SYMBOLS, '0');
        return;
    }
    const std::size_t start =
        (sync_history_pos_ + kSyncHistorySize - DMR_SYNC_SYMBOLS) % kSyncHistorySize;
    for (std::size_t i = 0; i < DMR_SYNC_SYMBOLS; ++i) {
        const std::size_t idx = (start + i) % kSyncHistorySize;
        dest[i] = sync_history_[idx];
    }
}

void DSDRxProcessor::push_sync_char(char c) {
    sync_history_[sync_history_pos_] = c;
    sync_history_pos_ = (sync_history_pos_ + 1) % kSyncHistorySize;
    if (sync_history_count_ < kSyncHistorySize) {
        ++sync_history_count_;
    }
}

void DSDRxProcessor::handle_carrier_loss() {
    carrier_present_ = false;
    jitter_ = -1;
    center_ = 0;
    sync_search_symbol_count_ = 0;
    symbol_counter_ = 0;
    parseState_ = Parse_State_Search_Sync;
    active_burst_index_ = 0;
    dibit_index_ = 0;
    current_burst_start_absolute_ = 0;
    umid_ = (((max_sample_ - center_) * 5) / 8) + center_;
    lmid_ = (((min_sample_ - center_) * 5) / 8) + center_;
    max_ref_ = max_sample_;
    min_ref_ = min_sample_;
}

void DSDRxProcessor::update_symbol_statistics(int32_t symbol_value) {
    lbuf1_[lbuf1_pos_] = symbol_value;
    lbuf1_pos_ = (lbuf1_pos_ + 1) % 24;
}

int main() {
    audio::dma::init_audio_out();
    EventDispatcher event_dispatcher{std::make_unique<DSDRxProcessor>()};
    event_dispatcher.run();
    return 0;
}
