#include "meditation_timer.h"
#include "esp_log.h"

static const char* TAG = "MeditationTimer";

void MeditationTimer::Start(Application* app, Display* display, int duration_minutes, std::function<void(int, int)> on_tick) {
    if (is_running_) {
        ESP_LOGW(TAG, "Meditation timer is already running");
        return;
    }
    
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
    
    ESP_LOGI(TAG, "Starting meditation timer for %d minutes", duration_minutes_);
    StartTimer();
}

void MeditationTimer::Stop() {
    if (!is_running_) {
        return;
    }
    
    ESP_LOGI(TAG, "Stopping meditation timer");
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
        timer_handle_ = nullptr;
    }
    
    esp_err_t ret = esp_timer_create(&timer_args, &timer_handle_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create meditation timer: %s", esp_err_to_name(ret));
        is_running_ = false;
        state_ = kMeditationStateIdle;
        return;
    }
    
    ret = esp_timer_start_periodic(timer_handle_, 1000000); // 1 second
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start meditation timer: %s", esp_err_to_name(ret));
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
        is_running_ = false;
        state_ = kMeditationStateIdle;
        return;
    }
}

void MeditationTimer::TimerCallback(void* arg) {
    MeditationTimer* self = static_cast<MeditationTimer*>(arg);
    if (self) {
        self->OnTick();
    }
}

void MeditationTimer::OnTick() {
    if (!is_running_) {
        return;
    }
    
    remaining_seconds_--;
    
    int minutes = remaining_seconds_ / 60;
    int seconds = remaining_seconds_ % 60;
    
    if (on_tick_) {
        on_tick_(minutes, seconds);
    }
    
    // 需要同时检查 app_ 和 display_，因为 Schedule 需要 app_
    if (display_ && app_) {
        app_->Schedule([this, minutes, seconds]() {
            if (display_) {  // 再次检查，因为可能在 Schedule 执行时已经被停止
                display_->SetStatus("冥想中");
                display_->SetEmotion("moon");  // 使用月亮表情表示冥想
                
                char countdown_msg[64];
                snprintf(countdown_msg, sizeof(countdown_msg), "%02d:%02d", minutes, seconds);
                display_->SetChatMessage("system", countdown_msg);
            }
        });
    }
    
    if (remaining_seconds_ <= 0) {
        OnTimerComplete();
    }
}

void MeditationTimer::OnTimerComplete() {
    ESP_LOGI(TAG, "Meditation timer completed");
    
    // 先标记为停止，避免在回调中继续执行
    is_running_ = false;
    state_ = kMeditationStateIdle;
    
    // 停止并删除定时器
    if (timer_handle_ != nullptr) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
        timer_handle_ = nullptr;
    }
    
    // 冥想结束，播放舒缓铃声
    if (app_) {
        app_->Schedule([this]() {
            // 播放舒缓铃声（使用 WELCOME 或 POPUP，都是比较舒缓的声音）
            if (app_) {
                app_->PlaySound(Lang::Sounds::OGG_VIBRATION);
            }
            
            if (display_) {
                display_->SetStatus("冥想结束");
                display_->SetEmotion("neutral");
                display_->SetChatMessage("system", "冥想时间到了");
            }
        });
    }
}