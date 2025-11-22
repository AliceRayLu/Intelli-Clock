# ZenLife Assistant

A comprehensive, voice-enabled life rhythm management system designed to harmonize your day. ZenLife Assistant integrates an intelligent alarm clock, sleep aid, meditation guide, and Pomodoro timer into one seamless experience, powered by AI to provide personalized content.

## ✨ Features

### 1. Intelligent Wake-Up Service
- **Smart Alarm**: Wakes you up at the scheduled time with a ringtone.
- **AI-Powered News Briefing**: After dismissing the alarm, automatically plays a voice news briefing (weather, tech, politics, economy) curated by a large language model.
- **Customization**:
  - **Set Time**: Voice-based time setting; defaults to the previous day's setting.
  - **Set Ringtone**: Choose intensity (Gentle/Strong); the AI automatically fetches a suitable ringtone.
- **Dismissal**:
  - Default: Physical press/tap to dismiss.
  - Voice command to "snooze" for a few minutes.
  - News playback starts automatically after the alarm is dismissed.

### 2. Sleep Reminder Service
- **Gentle Reminders**: Notifies you 30 minutes before and at your scheduled bedtime with a soothing ringtone.
- **Sleep Aid Audio**: At bedtime, automatically plays AI-generated sleep-aid audio for 5 minutes.
- **Customization**:
  - **Set Time**: Voice-based bedtime setting.
- **Dismissal**:
  - Default: Physical press/tap to dismiss the reminder.
  - Voice command to "snooze" the reminder.

### 3. Meditation Service
- **Voice Activation**: Start a session simply by speaking.
- **Flexible Timing**: Set session duration via voice or touch (default: 10 minutes).
- **Guided Options**: Choose between guided (with instructions) or unguided (background music only) sessions.
- **Gentle Conclusion**: Session ends with a soft, awakening ringtone.

### 4. Pomodoro Timer Service
- **Voice Activation**: Start a focus session with a voice command.
- **Standard Rhythm**: 25 minutes of focus followed by a 5-minute break. After 4 cycles, enjoy a longer 15-minute break.
- **Background Ambiance**: Optional white noise playback during focus sessions.
- **Progress Tracking**: View your total focused time for the day.
- **Flexibility**: Option to stop the timer mid-session.

## 🚀 Getting Started

### Prerequisites
VScode Extention with ESP-IDF sdk v5.5.1


## 🗣️ Usage

The primary mode of interaction is through **voice commands**. Upon running the application, you can say things like:

*   **"Set wake-up time for 7 AM."**
*   **"Change my alarm ringtone to strong."**
*   **"I want to meditate for 15 minutes with guidance."**
*   **"Start a Pomodoro timer."**
*   **"Snooze for 5 minutes."**

Physical buttons/touchscreen are used for primary actions like dismissing alarms.


## 🤝 Contributing
Contributions are welcome! Please feel free to submit a Pull Request.

1.  Fork the Project
2.  Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3.  Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4.  Push to the Branch (`git push origin feature/AmazingFeature`)
5.  Open a Pull Request

## 📄 License
Distributed under the MIT License. See `LICENSE` file for more information.