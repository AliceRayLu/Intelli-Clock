#pragma once

#include "application.h"
#include "display/display.h"
#include "assets/lang_config.h"
#include <esp_timer.h>
#include <functional>
#include <ctime>
#include <cJSON.h>

enum PomodoroState {
    kPomodoroStateIdle,
    kPomodoroStateWorking,
    kPomodoroStateBreak,
    kPomodoroStateLongBreak
};

class PomodoroTimer {
public:
    static PomodoroTimer& GetInstance() {
        static PomodoroTimer instance;
        return instance;
    }

    PomodoroTimer(const PomodoroTimer&) = delete;
    PomodoroTimer& operator=(const PomodoroTimer&) = delete;

    ~PomodoroTimer() {
        Stop();
    }

    void Start(Application* app, Display* display, std::function<void(int, int)> on_tick = nullptr);
    void Stop();

    bool IsRunning() const {
        return is_running_;
    }

    PomodoroState GetState() const {
        return state_;
    }

    int GetLoopCount() const {
        return loop_count_;
    }

    // Get total focus time (working time) in seconds for today
    int GetTotalFocusTimeSeconds() const;
    
    // Get total focus time formatted as string (HH:MM:SS)
    std::string GetTotalFocusTimeFormatted() const;
    
    // Display daily focus time info
    cJSON* GetDailyFocusInfo() const;

private:
    PomodoroTimer() = default;

    bool is_running_ = false;
    PomodoroState state_ = kPomodoroStateIdle;
    int loop_count_ = 0;
    int remaining_seconds_ = WORK_DURATION * 60;
    esp_timer_handle_t timer_handle_ = nullptr;
    Application* app_ = nullptr;
    Display* display_ = nullptr;
    std::function<void(int, int)> on_tick_;
    
    // Track daily focus time
    int total_focus_seconds_ = 0;  // Total working time in seconds for today
    time_t last_session_date_ = 0; // Track the date to reset daily stats

    static constexpr int WORK_DURATION = 25;      // 25 minutes
    static constexpr int SHORT_BREAK = 5;         // 5 minutes
    static constexpr int LONG_BREAK = 15;         // 15 minutes
    static constexpr int LOOPS_BEFORE_LONG_BREAK = 4;

    void StartTimer();
    static void TimerCallback(void* arg);
    void OnTick();
    void OnTimerComplete();
    int GetDurationForState() const;
    void ResetDailyStatsIfNeeded() const;
    void AddFocusTime(int seconds);
};

