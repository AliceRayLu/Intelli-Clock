#include "pomodoro_timer.h"
#include "esp_log.h"

static const char* TAG = "PomodoroTimer";

void PomodoroTimer::Start(Application* app, Display* display, std::function<void(int, int)> on_tick) {
    if (is_running_) return;
    
    is_running_ = true;
    state_ = kPomodoroStateWorking;
    loop_count_ = 0;
    app_ = app;
    display_ = display;
    on_tick_ = on_tick;
    
    StartTimer();
}

void PomodoroTimer::Stop() {
    if (!is_running_) return;
    
    // Add focus time if stopping during work session
    if (state_ == kPomodoroStateWorking) {
        int elapsed_seconds = (GetDurationForState() * 60) - remaining_seconds_;
        AddFocusTime(elapsed_seconds);
    }
    
    is_running_ = false;
    state_ = kPomodoroStateIdle;
    
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
}

void PomodoroTimer::StartTimer() {
    remaining_seconds_ = GetDurationForState() * 60;
    
    esp_timer_create_args_t timer_args = {
        .callback = &TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "pomodoro_timer",
    };
    
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
    }
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000)); // 1 second
}

void PomodoroTimer::TimerCallback(void* arg) {
    PomodoroTimer* self = static_cast<PomodoroTimer*>(arg);
    self->OnTick();
}

void PomodoroTimer::OnTick() {
    remaining_seconds_--;
    
    int minutes = remaining_seconds_ / 60;
    int seconds = remaining_seconds_ % 60;
    
    if (on_tick_) {
        on_tick_(minutes, seconds);
    }
    
    if (display_) {
        app_->Schedule([this, minutes, seconds]() {
            char status_msg[64];
            const char* state_name;
            
            switch (state_) {
                case kPomodoroStateWorking:
                    state_name = "工作中";
                    break;
                case kPomodoroStateBreak:
                    state_name = "休息中";
                    break;
                case kPomodoroStateLongBreak:
                    state_name = "长休息中";
                    break;
                default:
                    state_name = "未知";
            }
            
            snprintf(status_msg, sizeof(status_msg), "%s [%d/%d]", state_name, loop_count_, LOOPS_BEFORE_LONG_BREAK);
            display_->SetStatus(status_msg);
            
            char countdown_msg[32];
            snprintf(countdown_msg, sizeof(countdown_msg), "%02d:%02d", minutes, seconds);
            display_->SetChatMessage("system", countdown_msg);
        });
    }
    
    if (remaining_seconds_ <= 0) {
        OnTimerComplete();
    }
}

void PomodoroTimer::OnTimerComplete() {
    switch (state_) {
        case kPomodoroStateWorking: {
            // Add completed work session to daily focus time
            AddFocusTime(WORK_DURATION * 60);
            
            loop_count_++;
            if (loop_count_ >= LOOPS_BEFORE_LONG_BREAK) {
                // Long break after 4 loops
                state_ = kPomodoroStateLongBreak;
                loop_count_ = 0;
            } else {
                // Short break
                state_ = kPomodoroStateBreak;
            }
            
            // Play reminder sound and send message
            if (app_) {
                app_->Schedule([this]() {
                    app_->PlaySound(Lang::Sounds::OGG_SUCCESS);
                    if (state_ == kPomodoroStateLongBreak) {
                        app_->PlaySound(Lang::Sounds::OGG_POPUP);
                    }
                });
            }
            
            StartTimer();
            break;
        }
        
        case kPomodoroStateBreak:
        case kPomodoroStateLongBreak: {
            // After break, go back to working
            state_ = kPomodoroStateWorking;
            
            // Play reminder sound and send message
            if (app_) {
                app_->Schedule([this]() {
                    app_->PlaySound(Lang::Sounds::OGG_WELCOME);
                });
            }
            
            StartTimer();
            break;
        }
        
        default:
            break;
    }
}

int PomodoroTimer::GetDurationForState() const {
    switch (state_) {
        case kPomodoroStateWorking:
            return WORK_DURATION;
        case kPomodoroStateBreak:
            return SHORT_BREAK;
        case kPomodoroStateLongBreak:
            return LONG_BREAK;
        default:
            return WORK_DURATION;
    }
}

void PomodoroTimer::ResetDailyStatsIfNeeded() const {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    time_t today_midnight = mktime(timeinfo);
    
    if (last_session_date_ != today_midnight) {
        const_cast<PomodoroTimer*>(this)->total_focus_seconds_ = 0;
        const_cast<PomodoroTimer*>(this)->last_session_date_ = today_midnight;
    }
}

void PomodoroTimer::AddFocusTime(int seconds) {
    ResetDailyStatsIfNeeded();
    total_focus_seconds_ += seconds;
}

int PomodoroTimer::GetTotalFocusTimeSeconds() const {
    const_cast<PomodoroTimer*>(this)->ResetDailyStatsIfNeeded();
    return total_focus_seconds_;
}

std::string PomodoroTimer::GetTotalFocusTimeFormatted() const {
    ResetDailyStatsIfNeeded();
    int hours = total_focus_seconds_ / 3600;
    int minutes = (total_focus_seconds_ % 3600) / 60;
    int seconds = total_focus_seconds_ % 60;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, seconds);
    return std::string(buffer);
}

cJSON* PomodoroTimer::GetDailyFocusInfo() const {
    ResetDailyStatsIfNeeded();
    
    int total_seconds = total_focus_seconds_;
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;
    
    cJSON* json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "total_focus_seconds", total_seconds);
    cJSON_AddNumberToObject(json, "hours", hours);
    cJSON_AddNumberToObject(json, "minutes", minutes);
    cJSON_AddNumberToObject(json, "seconds", seconds);
    
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d", hours, minutes, seconds);
    cJSON_AddStringToObject(json, "formatted_time", time_str);
    
    // Calculate estimated completed pomodoros
    int completed_pomodoros = total_seconds / (WORK_DURATION * 60);
    cJSON_AddNumberToObject(json, "completed_pomodoros", completed_pomodoros);
    
    return json;
}

