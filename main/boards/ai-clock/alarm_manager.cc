#include "alarm_manager.h"
#include "settings.h"
#include <esp_log.h>
#include <cstring>

#define TAG "AlarmManager"

AlarmManager::AlarmManager() {
    LoadConfig();
    StartCheckTimer();
}

AlarmManager::~AlarmManager() {
    StopCheckTimer();
}

void AlarmManager::StartCheckTimer() {
    if (check_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = CheckTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "alarm_check_timer",
            .skip_unhandled_events = true
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &check_timer_));
    }
    ESP_ERROR_CHECK(esp_timer_start_periodic(check_timer_, 60000000)); // 每分钟检查一次
}

void AlarmManager::StopCheckTimer() {
    if (check_timer_ != nullptr) {
        esp_timer_stop(check_timer_);
        esp_timer_delete(check_timer_);
        check_timer_ = nullptr;
    }
}

void AlarmManager::CheckTimerCallback(void* arg) {
    AlarmManager* manager = static_cast<AlarmManager*>(arg);
    manager->CheckAlarms();
}

bool AlarmManager::SetWakeUpAlarm(int hour, int minute, AlarmRingIntensity intensity) {
    if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60) {
        ESP_LOGE(TAG, "Invalid wake up alarm time: %02d:%02d", hour, minute);
        return false;
    }
    
    wake_up_hour_ = hour;
    wake_up_minute_ = minute;
    wake_up_intensity_ = intensity;
    wake_up_state_ = kAlarmStateEnabled;
    wake_up_snooze_minutes_ = 0;
    wake_up_snooze_until_ = 0;
    
    SaveConfig();
    ESP_LOGI(TAG, "Wake up alarm set to %02d:%02d, intensity: %s", 
             hour, minute, intensity == kAlarmRingIntensityGentle ? "gentle" : "strong");
    return true;
}

bool AlarmManager::SetSleepAlarm(int hour, int minute) {
    if (hour < 0 || hour >= 24 || minute < 0 || minute >= 60) {
        ESP_LOGE(TAG, "Invalid sleep alarm time: %02d:%02d", hour, minute);
        return false;
    }
    
    sleep_hour_ = hour;
    sleep_minute_ = minute;
    sleep_state_ = kAlarmStateEnabled;
    sleep_snooze_minutes_ = 0;
    sleep_snooze_until_ = 0;
    sleep_reminder_sent_ = false;
    sleep_audio_playing_ = false;
    
    SaveConfig();
    ESP_LOGI(TAG, "Sleep alarm set to %02d:%02d", hour, minute);
    return true;
}

bool AlarmManager::GetWakeUpAlarm(int& hour, int& minute, AlarmRingIntensity& intensity) {
    if (wake_up_hour_ < 0 || wake_up_minute_ < 0) {
        return false;
    }
    hour = wake_up_hour_;
    minute = wake_up_minute_;
    intensity = wake_up_intensity_;
    return true;
}

bool AlarmManager::GetSleepAlarm(int& hour, int& minute) {
    if (sleep_hour_ < 0 || sleep_minute_ < 0) {
        return false;
    }
    hour = sleep_hour_;
    minute = sleep_minute_;
    return true;
}

void AlarmManager::EnableWakeUpAlarm(bool enable) {
    if (enable) {
        if (wake_up_hour_ >= 0 && wake_up_minute_ >= 0) {
            wake_up_state_ = kAlarmStateEnabled;
        } else {
            ESP_LOGW(TAG, "Cannot enable wake up alarm: time not set");
            return;
        }
    } else {
        wake_up_state_ = kAlarmStateDisabled;
    }
    SaveConfig();
    ESP_LOGI(TAG, "Wake up alarm %s", enable ? "enabled" : "disabled");
}

void AlarmManager::EnableSleepAlarm(bool enable) {
    if (enable) {
        if (sleep_hour_ >= 0 && sleep_minute_ >= 0) {
            sleep_state_ = kAlarmStateEnabled;
            sleep_reminder_sent_ = false;
        } else {
            ESP_LOGW(TAG, "Cannot enable sleep alarm: time not set");
            return;
        }
    } else {
        sleep_state_ = kAlarmStateDisabled;
        sleep_reminder_sent_ = false;
        sleep_audio_playing_ = false;
    }
    SaveConfig();
    ESP_LOGI(TAG, "Sleep alarm %s", enable ? "enabled" : "disabled");
}

void AlarmManager::DismissAlarm() {
    if (wake_up_state_ == kAlarmStateRinging) {
        wake_up_state_ = kAlarmStateDisabled;
        wake_up_snooze_minutes_ = 0;
        wake_up_snooze_until_ = 0;
        if (on_alarm_dismissed_) {
            on_alarm_dismissed_(kAlarmTypeWakeUp);
        }
        SaveConfig();
        ESP_LOGI(TAG, "Wake up alarm dismissed");
    }
    
    if (sleep_state_ == kAlarmStateRinging) {
        sleep_state_ = kAlarmStateDisabled;
        sleep_snooze_minutes_ = 0;
        sleep_snooze_until_ = 0;
        sleep_audio_playing_ = false;
        if (on_alarm_dismissed_) {
            on_alarm_dismissed_(kAlarmTypeSleep);
        }
        SaveConfig();
        ESP_LOGI(TAG, "Sleep alarm dismissed");
    }
}

void AlarmManager::SnoozeAlarm(int minutes) {
    if (wake_up_state_ == kAlarmStateRinging) {
        wake_up_snooze_minutes_ = minutes;
        time_t now = time(NULL);
        wake_up_snooze_until_ = now + minutes * 60;
        wake_up_state_ = kAlarmStateSnoozed;
        SaveConfig();
        ESP_LOGI(TAG, "Wake up alarm snoozed for %d minutes", minutes);
    }
    
    if (sleep_state_ == kAlarmStateRinging) {
        sleep_snooze_minutes_ = minutes;
        time_t now = time(NULL);
        sleep_snooze_until_ = now + minutes * 60;
        sleep_state_ = kAlarmStateSnoozed;
        SaveConfig();
        ESP_LOGI(TAG, "Sleep alarm snoozed for %d minutes", minutes);
    }
}

int64_t AlarmManager::CalculateSecondsToTime(int hour, int minute) {
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    
    struct tm tm_target = *tm_now;
    tm_target.tm_hour = hour;
    tm_target.tm_min = minute;
    tm_target.tm_sec = 0;
    
    time_t target = mktime(&tm_target);
    
    // 如果目标时间已过，则设置为明天
    if (target <= now) {
        target += 24 * 3600;
    }
    
    return target - now;
}

void AlarmManager::CheckAlarms() {
    time_t now = time(NULL);
    struct tm* tm_now = localtime(&now);
    
    // 检查起床闹钟
    if (wake_up_state_ == kAlarmStateEnabled && wake_up_hour_ >= 0 && wake_up_minute_ >= 0) {
        if (tm_now->tm_hour == wake_up_hour_ && tm_now->tm_min == wake_up_minute_) {
            if (wake_up_state_ != kAlarmStateRinging) {
                wake_up_state_ = kAlarmStateRinging;
                if (on_wake_up_triggered_) {
                    on_wake_up_triggered_(wake_up_intensity_);
                }
                ESP_LOGI(TAG, "Wake up alarm triggered at %02d:%02d", wake_up_hour_, wake_up_minute_);
            }
        }
    } else if (wake_up_state_ == kAlarmStateSnoozed) {
        if (now >= wake_up_snooze_until_) {
            wake_up_state_ = kAlarmStateRinging;
            if (on_wake_up_triggered_) {
                on_wake_up_triggered_(wake_up_intensity_);
            }
            ESP_LOGI(TAG, "Wake up alarm snooze expired, ringing again");
        }
    }
    
    // 检查睡眠提醒
    if (sleep_state_ == kAlarmStateEnabled && sleep_hour_ >= 0 && sleep_minute_ >= 0) {
        // 计算30分钟前的时间
        struct tm tm_reminder = *tm_now;
        tm_reminder.tm_min -= 30;
        if (tm_reminder.tm_min < 0) {
            tm_reminder.tm_min += 60;
            tm_reminder.tm_hour -= 1;
            if (tm_reminder.tm_hour < 0) {
                tm_reminder.tm_hour += 24;
            }
        }
        
        // 检查是否到了30分钟前提醒时间
        if (!sleep_reminder_sent_ && 
            tm_now->tm_hour == tm_reminder.tm_hour && 
            tm_now->tm_min == tm_reminder.tm_min) {
            sleep_reminder_sent_ = true;
            if (on_sleep_reminder_) {
                on_sleep_reminder_();
            }
            ESP_LOGI(TAG, "Sleep reminder sent (30 minutes before)");
        }
        
        // 检查是否到了入睡时间
        if (tm_now->tm_hour == sleep_hour_ && tm_now->tm_min == sleep_minute_) {
            if (!sleep_audio_playing_) {
                sleep_audio_playing_ = true;
                sleep_audio_start_time_ = now;
                if (on_sleep_start_) {
                    on_sleep_start_();
                }
                ESP_LOGI(TAG, "Sleep audio started at %02d:%02d", sleep_hour_, sleep_minute_);
            }
        }
        
        // 检查助眠音频是否已播放5分钟
        if (sleep_audio_playing_ && (now - sleep_audio_start_time_) >= 300) {
            sleep_audio_playing_ = false;
            if (on_sleep_stop_) {
                on_sleep_stop_();
            }
            ESP_LOGI(TAG, "Sleep audio stopped after 5 minutes");
        }
    } else if (sleep_state_ == kAlarmStateSnoozed) {
        if (now >= sleep_snooze_until_) {
            sleep_state_ = kAlarmStateRinging;
            if (on_sleep_reminder_) {
                on_sleep_reminder_();
            }
            ESP_LOGI(TAG, "Sleep alarm snooze expired, reminding again");
        }
    }
}

void AlarmManager::OnWakeUpAlarmTriggered(std::function<void(AlarmRingIntensity)> callback) {
    on_wake_up_triggered_ = callback;
}

void AlarmManager::OnSleepAlarmReminder(std::function<void()> callback) {
    on_sleep_reminder_ = callback;
}

void AlarmManager::OnSleepAlarmStart(std::function<void()> callback) {
    on_sleep_start_ = callback;
}

void AlarmManager::OnSleepAlarmStop(std::function<void()> callback) {
    on_sleep_stop_ = callback;
}

void AlarmManager::OnAlarmDismissed(std::function<void(AlarmType)> callback) {
    on_alarm_dismissed_ = callback;
}

void AlarmManager::StartNewsBroadcast() {
    news_broadcasting_ = true;
    ESP_LOGI(TAG, "News broadcast started");
}

void AlarmManager::StopNewsBroadcast() {
    news_broadcasting_ = false;
    ESP_LOGI(TAG, "News broadcast stopped");
}

void AlarmManager::SaveConfig() {
    Settings settings("alarm", true);
    settings.SetInt("wake_hour", wake_up_hour_);
    settings.SetInt("wake_min", wake_up_minute_);
    settings.SetInt("wake_intensity", wake_up_intensity_);
    settings.SetInt("wake_state", wake_up_state_);
    settings.SetInt("sleep_hour", sleep_hour_);
    settings.SetInt("sleep_min", sleep_minute_);
    settings.SetInt("sleep_state", sleep_state_);
}

void AlarmManager::LoadConfig() {
    Settings settings("alarm", false);
    // 先尝试使用新键名，如果不存在则尝试旧键名（用于兼容）
    wake_up_hour_ = settings.GetInt("wake_hour", -1);
    if (wake_up_hour_ < 0) {
        wake_up_hour_ = settings.GetInt("wake_up_hour", -1);
    }
    wake_up_minute_ = settings.GetInt("wake_min", -1);
    if (wake_up_minute_ < 0) {
        wake_up_minute_ = settings.GetInt("wake_up_minute", -1);
    }
    int intensity = settings.GetInt("wake_intensity", -1);
    if (intensity < 0) {
        intensity = settings.GetInt("wake_up_intensity", kAlarmRingIntensityGentle);
    }
    wake_up_intensity_ = static_cast<AlarmRingIntensity>(intensity);
    int state = settings.GetInt("wake_state", -1);
    if (state < 0) {
        state = settings.GetInt("wake_up_state", kAlarmStateDisabled);
    }
    wake_up_state_ = static_cast<AlarmState>(state);
    sleep_hour_ = settings.GetInt("sleep_hour", -1);
    sleep_minute_ = settings.GetInt("sleep_min", -1);
    if (sleep_minute_ < 0) {
        sleep_minute_ = settings.GetInt("sleep_minute", -1);
    }
    state = settings.GetInt("sleep_state", kAlarmStateDisabled);
    sleep_state_ = static_cast<AlarmState>(state);
    
    ESP_LOGI(TAG, "Loaded alarm config: wake_up=%02d:%02d (state=%d), sleep=%02d:%02d (state=%d)",
             wake_up_hour_, wake_up_minute_, wake_up_state_,
             sleep_hour_, sleep_minute_, sleep_state_);
}

