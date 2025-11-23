#include "meditation_timer.h"
#include "esp_log.h"

static const char* TAG = "MeditationTimer";

void MeditationTimer::Start(Application* app, Display* display, int duration_minutes, std::function<void(int, int)> on_tick) {
    if (is_running_) return;
    
    is_running_ = true;
    state_ = kMeditationStateRunning;
    app_ = app;
    display_ = display;
    on_tick_ = on_tick;
    
    // 如果提供了时间，使用提供的时间；否则使用默认10分钟
    if (duration_minutes > 0) {
        duration_minutes_ = duration_minutes;
    } else {
        duration_minutes_ = DEFAULT_DURATION;
    }
    
    StartTimer();
}

void MeditationTimer::Stop() {
    if (!is_running_) return;
    
    is_running_ = false;
    state_ = kMeditationStateIdle;
    
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
}

void MeditationTimer::StartTimer() {
    remaining_seconds_ = duration_minutes_ * 60;
    
    esp_timer_create_args_t timer_args = {
        .callback = &TimerCallback,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "meditation_timer",
    };
    
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
    }
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000)); // 1 second
}

void MeditationTimer::TimerCallback(void* arg) {
    MeditationTimer* self = static_cast<MeditationTimer*>(arg);
    self->OnTick();
}

void MeditationTimer::OnTick() {
    remaining_seconds_--;
    
    int minutes = remaining_seconds_ / 60;
    int seconds = remaining_seconds_ % 60;
    
    if (on_tick_) {
        on_tick_(minutes, seconds);
    }
    
    if (display_) {
        app_->Schedule([this, minutes, seconds]() {
            display_->SetStatus("冥想中");
            display_->SetEmotion("moon");  // 使用月亮表情表示冥想
            
            char countdown_msg[64];
            snprintf(countdown_msg, sizeof(countdown_msg), "%02d:%02d", minutes, seconds);
            display_->SetChatMessage("system", countdown_msg);
        });
    }
    
    if (remaining_seconds_ <= 0) {
        OnTimerComplete();
    }
}

void MeditationTimer::OnTimerComplete() {
    // 冥想结束，播放舒缓铃声
    if (app_) {
        app_->Schedule([this]() {
            // 播放舒缓铃声（使用 WELCOME 或 POPUP，都是比较舒缓的声音）
            app_->PlaySound(Lang::Sounds::OGG_WELCOME);
            
            if (display_) {
                display_->SetStatus("冥想结束");
                display_->SetEmotion("neutral");
                display_->SetChatMessage("system", "冥想时间到了");
            }
        });
    }
    
    // 停止定时器
    Stop();
}