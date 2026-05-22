#ifndef IFACE_AUDIO_RESAMPLER_H
#define IFACE_AUDIO_RESAMPLER_H

#include <stdint.h>
#include "emulator/audio.h"

#ifdef __cplusplus
extern "C"
{
#endif

    uint8_t *iface_audio_resampler_process_audio_log(const st_AUDIO_SOURCE_LOG *source, uint64_t log_count_samples, size_t output_samples_count);
    float *iface_audio_resampler_output_stream_ctc0(const uint8_t *input_samples, size_t input_samples_count, size_t output_samples_count, size_t parked_samples, size_t parked_fade);
    float *iface_audio_resampler_output_stream_psg(int channel, const uint8_t *input_samples, size_t input_samples_count, size_t output_samples_count, float *SN76489_volume_value, size_t parked_samples, size_t parked_fade);

    float *iface_audio_resampler_process_psg_audio_log(int channel, const st_AUDIO_SOURCE_LOG *source, uint64_t log_count_samples, float *SN76489_volume_value, size_t output_samples_count, size_t parked_samples, size_t parked_fade);
#ifdef __cplusplus
}
#endif
#endif // IFACE_AUDIO_RESAMPLER_H
