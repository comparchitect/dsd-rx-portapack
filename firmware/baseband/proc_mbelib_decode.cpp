/*
 * MBELIB (AMBE) offline decoder processor.
 */

#include "proc_mbelib_decode.hpp"

#include "event_m4.hpp"
#include "portapack_shared_memory.hpp"
#include "audio_dma.hpp"
#include "message.hpp"
#include "apps/ambe_processing.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace {


void unpack_frame(const uint8_t* packed, char ambe_fr[4][24]) {
    size_t bit_index = 0;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 24; ++col) {
            const size_t byte_index = bit_index / 8;
            const uint8_t bit_offset = 7 - static_cast<uint8_t>(bit_index % 8);
            ambe_fr[row][col] = static_cast<char>((packed[byte_index] >> bit_offset) & 0x01u);
            ++bit_index;
        }
    }
}

}  // namespace

void MBELIBDecodeProcessor::execute(const buffer_c8_t&) {
    // No streamed data via execute(). All traffic comes through messages.
}

void MBELIBDecodeProcessor::on_message(const Message* const message) {
    switch (message->id) {
        case Message::ID::AMBE2DecodeControl:
            handle_control(*reinterpret_cast<const AMBE2DecodeControlMessage*>(message));
            break;

        case Message::ID::AMBE2DecodeFrame:
            handle_frame(*reinterpret_cast<const AMBE2DecodeFrameMessage*>(message));
            break;

        default:
            break;
    }
}

void MBELIBDecodeProcessor::handle_control(const AMBE2DecodeControlMessage& message) {
    switch (message.command) {
        case AMBE2DecodeControlMessage::Command::Reset:
            decoder_.reset();
            frames_processed_ = 0;
            frame_errors_ = 0;
            pcm_dropped_ = 0;
            // Reset AGC state
            agc_gain_ = 50.0f;
            agc_max_history_.fill(0.0f);
            agc_max_history_index_ = 0;
            send_stats(true);
            break;

        case AMBE2DecodeControlMessage::Command::Flush:
            send_stats(true);
            break;

        case AMBE2DecodeControlMessage::Command::Stop:
            // Send completion acknowledgment to M0
            AMBE2DecodeStatsMessage completion_message{
                frames_processed_,
                frame_errors_,
                pcm_dropped_,
                true};  // completion_ack = true
            shared_memory.application_queue.push(completion_message);
            break;
    }
}

void MBELIBDecodeProcessor::handle_frame(const AMBE2DecodeFrameMessage& message) {
    char ambe_fr[4][24];
    uint8_t errs2 = ambe_processing::unpack_frame(message.data, ambe_fr);

    // Extract AMBE data directly without re-applying error correction
    // (ECC was already applied during capture in AMBE app)
    char ambe_d[49] = {0};
    ambe_processing::extract_ambe_data(ambe_fr, ambe_d);

    std::array<float, 160> float_pcm{};
    // Use the stored errs2 from capture to match dsd.test's error handling
    const int produced = decoder_.processDataFloat(ambe_d, 0, errs2, float_pcm.data(), float_pcm.size());
    if (produced > 0) {
        // Apply sophisticated AGC (dsd.test style)
        apply_auto_gain(float_pcm.data(), produced);

        // Convert to int16_t for M0 processing (upsampling/smoothing will be done there)
        std::array<int16_t, 160> int16_buffer{};
        for (size_t i = 0; i < produced; ++i) {
            float sample = float_pcm[i];
            if (sample > 32767.0f) sample = 32767.0f;
            if (sample < -32768.0f) sample = -32768.0f;
            int16_buffer[i] = static_cast<int16_t>(sample);
        }

        AMBEPCMFrameMessage pcm_message{int16_buffer.data(), static_cast<uint16_t>(produced)};
        if (!shared_memory.application_queue.push(pcm_message)) {
            ++pcm_dropped_;
        }
    } else {
        ++frame_errors_;
    }

    // Don't count errors since we're not applying ECC here
    ++frames_processed_;
    send_stats();
}

void MBELIBDecodeProcessor::send_stats(bool force) {
    if (!force && (frames_processed_ % 25u != 0u)) {
        return;
    }

    AMBE2DecodeStatsMessage stats_message{
        frames_processed_,
        frame_errors_,
        pcm_dropped_,
        false};  // completion_ack
    shared_memory.application_queue.push(stats_message);

    RequestSignalMessage progress_message{RequestSignalMessage::Signal::AmbeDecodeProgress};
    shared_memory.application_queue.push(progress_message);
}

void MBELIBDecodeProcessor::apply_auto_gain(float* samples, size_t count) {
    if (!samples || count == 0) {
        return;
    }

    // Detect max level (dsd.test style)
    float max_val = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        const float abs_val = std::fabs(samples[i]);
        if (abs_val > max_val) {
            max_val = abs_val;
        }
    }

    // Store in history buffer
    agc_max_history_[agc_max_history_index_] = max_val;
    agc_max_history_index_ = (agc_max_history_index_ + 1) % agc_max_history_.size();

    // Find max across history
    float max_history = 0.0f;
    for (float hist_val : agc_max_history_) {
        if (hist_val > max_history) {
            max_history = hist_val;
        }
    }

    // Determine optimal gain level (dsd.test algorithm)
    float gainfactor = 50.0f;  // Default gain
    if (max_history > 0.0f) {
        gainfactor = 30000.0f / max_history;
    }

    float gaindelta = 0.0f;
    if (gainfactor < agc_gain_) {
        // Immediate gain reduction
        agc_gain_ = gainfactor;
    } else {
        // Gradual gain increase
        if (gainfactor > 50.0f) {
            gainfactor = 50.0f;
        }
        gaindelta = gainfactor - agc_gain_;
        if (gaindelta > (0.05f * agc_gain_)) {
            gaindelta = 0.05f * agc_gain_;
        }
    }

    gaindelta /= static_cast<float>(count);

    // Apply gain with smooth transitions
    for (size_t i = 0; i < count; ++i) {
        const float current_gain = agc_gain_ + static_cast<float>(i) * gaindelta;
        samples[i] *= current_gain;
    }

    agc_gain_ += static_cast<float>(count) * gaindelta;
}

int main() {
    audio::dma::init_audio_out();
    EventDispatcher event_dispatcher{std::make_unique<MBELIBDecodeProcessor>()};
    event_dispatcher.run();
    return 0;
}
