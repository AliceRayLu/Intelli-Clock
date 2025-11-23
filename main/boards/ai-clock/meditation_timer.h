#pragma once

#include "application.h"
#include "display/display.h"
#include "assets/lang_config.h"
#include <esp_timer.h>
#include <functional>
#include <ctime>

enum MeditationState {
    kMeditationStateIdle,
    kMeditationStateRunning
};

class MeditationTimer {
public:
    static MeditationTimer& GetInstance() {
        static MeditationTimer instance;
        return instance;
    }

    MeditationTimer(const MeditationTimer&) = delete;
    MeditationTimer& operator=(const MeditationTimer&) = delete;

    ~MeditationTimer() {
        Stop();
    }

    // 启动冥想定时器，duration_minutes 为分钟数，如果为0则使用默认值10分钟
    void Start(Application* app, Display* display, int duration_minutes = 0, std::function<void(int, int)> on_tick = nullptr);
    void Stop();

    bool IsRunning() const {
        return is_running_;
    }

    MeditationState GetState() const {
        return state_;
    }

    int GetRemainingMinutes() const {
        return remaining_seconds_ / 60;
    }

    int GetRemainingSeconds() const {
        return remaining_seconds_ % 60;
    }

private:
    MeditationTimer() = default;

    bool is_running_ = false;
    MeditationState state_ = kMeditationStateIdle;
    int remaining_seconds_ = 0;
    int duration_minutes_ = 10;  // 默认10分钟
    esp_timer_handle_t timer_handle_ = nullptr;
    Application* app_ = nullptr;
    Display* display_ = nullptr;
    std::function<void(int, int)> on_tick_;

    static constexpr int DEFAULT_DURATION = 10;  // 默认10分钟

    void StartTimer();
    static void TimerCallback(void* arg);
    void OnTick();
    void OnTimerComplete();
};