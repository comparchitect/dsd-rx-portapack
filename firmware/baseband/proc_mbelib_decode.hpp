/*
 * MBELIB (AMBE) offline decoder processor.
 */

#ifndef __PROC_MBELIB_DECODE_HPP__
#define __PROC_MBELIB_DECODE_HPP__

#include "baseband_processor.hpp"
#include "message.hpp"

#include "external/dsd/mbe_decoder.hpp"

#include <array>
#include <cstdint>
#include <cstddef>

class MBELIBDecodeProcessor : public BasebandProcessor {
   public:
    void execute(const buffer_c8_t& buffer) override;
    void on_message(const Message* const message) override;

   private:
    void handle_control(const AMBE2DecodeControlMessage& message);
    void handle_frame(const AMBE2DecodeFrameMessage& message);
    void send_stats(bool force = false);
    void apply_auto_gain(float* samples, size_t count);

    mbe::MBEDecoder decoder_{};
    uint32_t frames_processed_{0};
    uint32_t frame_errors_{0};
    uint32_t pcm_dropped_{0};

    // Sophisticated AGC state (dsd.test style)
    float agc_gain_{50.0f};  // Start with gain = 50
    std::array<float, 25> agc_max_history_{};
    size_t agc_max_history_index_{0};
};

#endif /* __PROC_MBELIB_DECODE_HPP__ */
