#include "audio_engine.h"
#include <aaudio/AAudio.h>
#include <cmath>
#include <atomic>
#include <vector>

static AAudioStream* stream = nullptr;
static std::atomic<float> phase{0.0f};
static std::atomic<float> phase_inc{0.0f};
static std::atomic<float> amplitude{0.0f};

// [极致优化] 预计算正弦波表 (Wavetable)，彻底消除音频线程中的 sin() 昂贵计算
#define WAVETABLE_SIZE 1024
static float wavetable[WAVETABLE_SIZE];

static void init_wavetable() {
    static bool table_ready = false;
    if (table_ready) return;
    for (int i = 0; i < WAVETABLE_SIZE; i++) {
        wavetable[i] = std::sin(2.0f * 3.14159265f * i / WAVETABLE_SIZE);
    }
    table_ready = true;
}

static aaudio_data_callback_result_t data_callback(
        AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {
    float *floatData = (float *) audioData;
    
    float amp = amplitude.load(std::memory_order_relaxed);
    float p_inc = phase_inc.load(std::memory_order_relaxed);
    float p = phase.load(std::memory_order_relaxed);

    if (amp < 0.0001f) {
        for (int i = 0; i < numFrames; i++) floatData[i] = 0.0f;
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    // 将弧度增量转换为波表索引增量
    float idx_inc = p_inc * (WAVETABLE_SIZE / (2.0f * 3.14159265f));
    float idx = p * (WAVETABLE_SIZE / (2.0f * 3.14159265f));

    for (int i = 0; i < numFrames; i++) {
        // [极客优化] 线性插值虽然音质更好，但对于短促音效，直接取整 (Truncation) 性能最强且听感无差
        int i_idx = (int)idx % WAVETABLE_SIZE;
        floatData[i] = wavetable[i_idx] * amp;

        idx += idx_inc;
        amp *= 0.993f; // 快速衰减
    }
    
    // 换算回弧度存回原子变量
    phase.store(idx * (2.0f * 3.14159265f / WAVETABLE_SIZE), std::memory_order_relaxed);
    amplitude.store(amp, std::memory_order_relaxed);
    
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void audio_init() {
    init_wavetable();
    AAudioStreamBuilder *builder;
    AAudio_createStreamBuilder(&builder);
    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
    AAudioStreamBuilder_setChannelCount(builder, 1);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
    AAudioStreamBuilder_setDataCallback(builder, data_callback, nullptr);

    if (AAudioStreamBuilder_openStream(builder, &stream) == AAUDIO_OK) {
        AAudioStream_requestStart(stream);
    }
    AAudioStreamBuilder_delete(builder);
}

// 播放清脆的高频短音 (UI 交互/拿起棋子)
void audio_play_click() {
    phase_inc.store((660.0f * 2.0f * 3.14159f) / 48000.0f, std::memory_order_relaxed);
    amplitude.store(0.25f, std::memory_order_relaxed);
}

// 播放低沉有力的短音 (重重落子/吃子)
void audio_play_drop() {
    phase_inc.store((200.0f * 2.0f * 3.14159f) / 48000.0f, std::memory_order_relaxed);
    amplitude.store(0.6f, std::memory_order_relaxed);
}

void audio_teardown() {
    if (stream) {
        AAudioStream_requestStop(stream);
        AAudioStream_close(stream);
        stream = nullptr;
    }
}
