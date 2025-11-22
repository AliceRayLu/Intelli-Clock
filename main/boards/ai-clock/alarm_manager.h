#ifndef ALARM_MANAGER_H
#define ALARM_MANAGER_H

#include <esp_timer.h>
#include <time.h>
#include <string>
#include <functional>
#include <cstdint>

enum AlarmType {
    kAlarmTypeWakeUp,
    kAlarmTypeSleep
};

enum AlarmRingIntensity {
    kAlarmRingIntensityGentle,  // 舒缓
    kAlarmRingIntensityStrong   // 强烈
};

enum AlarmState {
    kAlarmStateDisabled,
    kAlarmStateEnabled,
    kAlarmStateRinging,
    kAlarmStateSnoozed
};

class AlarmManager {
public:
    static AlarmManager& GetInstance() {
        static AlarmManager instance;
        return instance;
    }

    // 设置起床闹钟
    bool SetWakeUpAlarm(int hour, int minute, AlarmRingIntensity intensity = kAlarmRingIntensityGentle);
    
    // 设置睡眠提醒
    bool SetSleepAlarm(int hour, int minute);
    
    // 获取起床闹钟时间
    bool GetWakeUpAlarm(int& hour, int& minute, AlarmRingIntensity& intensity);
    
    // 获取睡眠提醒时间
    bool GetSleepAlarm(int& hour, int& minute);
    
    // 启用/禁用起床闹钟
    void EnableWakeUpAlarm(bool enable);
    
    // 启用/禁用睡眠提醒
    void EnableSleepAlarm(bool enable);
    
    // 关闭闹钟（按压关闭）
    void DismissAlarm();
    
    // 延迟提醒（语音延迟）
    void SnoozeAlarm(int minutes);
    
    // 检查并触发闹钟
    void CheckAlarms();
    
    // 设置回调函数
    void OnWakeUpAlarmTriggered(std::function<void(AlarmRingIntensity)> callback);
    void OnSleepAlarmReminder(std::function<void()> callback);
    void OnSleepAlarmStart(std::function<void()> callback);
    void OnSleepAlarmStop(std::function<void()> callback);
    void OnAlarmDismissed(std::function<void(AlarmType)> callback);
    
    // 获取闹钟状态
    AlarmState GetWakeUpAlarmState() const { return wake_up_state_; }
    AlarmState GetSleepAlarmState() const { return sleep_state_; }
    
    // 开始播放新闻
    void StartNewsBroadcast();
    
    // 停止播放新闻
    void StopNewsBroadcast();
    
    // 是否正在播放新闻
    bool IsNewsBroadcasting() const { return news_broadcasting_; }

private:
    AlarmManager();
    ~AlarmManager();
    
    void StartCheckTimer();
    void StopCheckTimer();
    static void CheckTimerCallback(void* arg);
    
    // 起床闹钟相关
    int wake_up_hour_ = -1;
    int wake_up_minute_ = -1;
    AlarmRingIntensity wake_up_intensity_ = kAlarmRingIntensityGentle;
    AlarmState wake_up_state_ = kAlarmStateDisabled;
    int wake_up_snooze_minutes_ = 0;
    int64_t wake_up_snooze_until_ = 0;
    
    // 睡眠提醒相关
    int sleep_hour_ = -1;
    int sleep_minute_ = -1;
    AlarmState sleep_state_ = kAlarmStateDisabled;
    int sleep_snooze_minutes_ = 0;
    int64_t sleep_snooze_until_ = 0;
    bool sleep_reminder_sent_ = false;  // 是否已发送30分钟前提醒
    bool sleep_audio_playing_ = false;   // 是否正在播放助眠音频
    int64_t sleep_audio_start_time_ = 0; // 助眠音频开始时间
    
    // 新闻播报
    bool news_broadcasting_ = false;
    
    // 定时器
    esp_timer_handle_t check_timer_ = nullptr;
    
    // 回调函数
    std::function<void(AlarmRingIntensity)> on_wake_up_triggered_;
    std::function<void()> on_sleep_reminder_;
    std::function<void()> on_sleep_start_;
    std::function<void()> on_sleep_stop_;
    std::function<void(AlarmType)> on_alarm_dismissed_;
    
    // 计算到指定时间的秒数
    int64_t CalculateSecondsToTime(int hour, int minute);
    
    // 保存和加载配置
    void SaveConfig();
    void LoadConfig();
};

#endif // ALARM_MANAGER_H

