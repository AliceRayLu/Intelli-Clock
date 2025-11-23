#include "wifi_board.h"
#include "codecs/es8311_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "i2c_device.h"
#include "assets/lang_config.h"
#include "alarm_manager.h"
#include "pomodoro_timer.h"
#include "meditation_timer.h"
#include "mcp_server.h"
#include <cJSON.h>

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <wifi_station.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_gc9a01.h>

#define TAG "AIClock"

#define PI4IOE_ADDR          0x43
#define PI4IOE_REG_CTRL      0x00
#define PI4IOE_REG_IO_PP     0x07
#define PI4IOE_REG_IO_DIR    0x03
#define PI4IOE_REG_IO_OUT    0x05
#define PI4IOE_REG_IO_PULLUP 0x0D

class Pi4ioe : public I2cDevice {
public:
    Pi4ioe(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(PI4IOE_REG_IO_PP, 0x00); // Set to high-impedance
        WriteReg(PI4IOE_REG_IO_PULLUP, 0xFF); // Enable pull-up
        WriteReg(PI4IOE_REG_IO_DIR, 0x6E); // Set input=0, output=1
        WriteReg(PI4IOE_REG_IO_OUT, 0xFF); // Set outputs to 1
    }

    void SetSpeakerMute(bool mute) {
        WriteReg(PI4IOE_REG_IO_OUT, mute ? 0x00 : 0xFF);
    }
};

class Lp5562 : public I2cDevice {
public:
    Lp5562(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x00, 0B01000000); // Set chip_en to 1
        WriteReg(0x08, 0B00000001); // Enable internal clock
        WriteReg(0x70, 0B00000000); // Configure all LED outputs to be controlled from I2C registers

        // PWM clock frequency 558 Hz
        auto data = ReadReg(0x08);
        data = data | 0B01000000;
        WriteReg(0x08, data);
    }

    void SetBrightness(uint8_t brightness) {
        // Map 0~100 to 0~255
        brightness = brightness * 255 / 100;
        WriteReg(0x0E, brightness);
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Lp5562* lp5562) : lp5562_(lp5562) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        if (lp5562_) {
            lp5562_->SetBrightness(brightness);
        } else {
            ESP_LOGE(TAG, "LP5562 not available");
        }
    }

private:
    Lp5562* lp5562_ = nullptr;
};

static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb2, (uint8_t[]){0x2f}, 1, 0},
    {0xb3, (uint8_t[]){0x03}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x01}, 1, 0},
    {0xac, (uint8_t[]){0xcb}, 1, 0},
    {0xab, (uint8_t[]){0x0e}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x19}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xe8, (uint8_t[]){0x24}, 1, 0},
    {0xe9, (uint8_t[]){0x48}, 1, 0},
    {0xea, (uint8_t[]){0x22}, 1, 0},
    {0xc6, (uint8_t[]){0x30}, 1, 0},
    {0xc7, (uint8_t[]){0x18}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1f, 0x28, 0x04, 0x3e, 0x2a, 0x2e, 0x20, 0x00, 0x0c, 0x06,
                0x00, 0x1c, 0x1f, 0x0f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x00, 0x2d, 0x2f, 0x3c, 0x6f, 0x1c, 0x0b, 0x00, 0x00, 0x00,
                0x07, 0x0d, 0x11, 0x0f},
    14, 0},
};

class AIClockBoard : public WifiBoard {
private:
    i2c_master_bus_handle_t i2c_bus_;
    i2c_master_bus_handle_t i2c_bus_internal_;
    Pi4ioe* pi4ioe_ = nullptr;
    Lp5562* lp5562_ = nullptr;
    Display* display_ = nullptr;
    Button boot_button_;
    bool is_echo_base_connected_ = false;

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = I2C_NUM_1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
        
        i2c_bus_cfg.i2c_port = I2C_NUM_0;
        i2c_bus_cfg.sda_io_num = GPIO_NUM_45;
        i2c_bus_cfg.scl_io_num = GPIO_NUM_0;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_internal_));
    }

    void I2cDetect() {
        is_echo_base_connected_ = false;
        uint8_t echo_base_connected_flag = 0x00;
        uint8_t address;
        printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        for (int i = 0; i < 128; i += 16) {
            printf("%02x: ", i);
            for (int j = 0; j < 16; j++) {
                fflush(stdout);
                address = i + j;
                esp_err_t ret = i2c_master_probe(i2c_bus_, address, pdMS_TO_TICKS(200));
                if (ret == ESP_OK) {
                    printf("%02x ", address);
                    if (address == 0x18) {
                        echo_base_connected_flag |= 0xF0;
                    } else if (address == 0x43) {
                        echo_base_connected_flag |= 0x0F;
                    }
                } else if (ret == ESP_ERR_TIMEOUT) {
                    printf("UU ");
                } else {
                    printf("-- ");
                }
            }
            printf("\r\n");
        }
        is_echo_base_connected_ = (echo_base_connected_flag == 0xFF);
    }

    void CheckEchoBaseConnection() {
        if (is_echo_base_connected_) {
            return;
        }
        
        // Pop error page
        InitializeLp5562();
        InitializeSpi();
        InitializeGc9107Display();
        InitializeButtons();
        GetBacklight()->SetBrightness(100);
        display_->SetStatus(Lang::Strings::ERROR);
        display_->SetEmotion("triangle_exclamation");
        display_->SetChatMessage("system", "Echo Base\nnot connected");
        
        while (1) {
            ESP_LOGE(TAG, "Atomic Echo Base is disconnected");
            vTaskDelay(pdMS_TO_TICKS(1000));

            // Rerun detection
            I2cDetect();
            if (is_echo_base_connected_) {
                vTaskDelay(pdMS_TO_TICKS(500));
                I2cDetect();
                if (is_echo_base_connected_) {
                    ESP_LOGI(TAG, "Atomic Echo Base is reconnected");
                    vTaskDelay(pdMS_TO_TICKS(200));
                    esp_restart();
                }
            }
        }
    }

    void InitializePi4ioe() {
        ESP_LOGI(TAG, "Init PI4IOE");
        pi4ioe_ = new Pi4ioe(i2c_bus_, PI4IOE_ADDR);
        pi4ioe_->SetSpeakerMute(false);
    }

    void InitializeLp5562() {
        ESP_LOGI(TAG, "Init LP5562");
        lp5562_ = new Lp5562(i2c_bus_internal_, 0x30);
    }

    void InitializeSpi() {
        ESP_LOGI(TAG, "Initialize SPI bus");
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_21;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_15;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeGc9107Display() {
        ESP_LOGI(TAG, "Init GC9107 display");

        ESP_LOGI(TAG, "Install panel IO");
        esp_lcd_panel_io_handle_t io_handle = NULL;
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_14;
        io_config.dc_gpio_num = GPIO_NUM_42;
        io_config.spi_mode = 0;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io_handle));
    
        ESP_LOGI(TAG, "Install GC9A01 panel driver");
        esp_lcd_panel_handle_t panel_handle = NULL;
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_48; // Set to -1 if not use
        panel_config.rgb_endian = LCD_RGB_ENDIAN_BGR;
        panel_config.bits_per_pixel = 16; // Implemented by LCD command `3Ah` (16/18)
        panel_config.vendor_config = &gc9107_vendor_config;

        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(io_handle, &panel_config, &panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
        ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true)); 

        display_ = new SpiLcdDisplay(io_handle, panel_handle,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            auto& alarm_mgr = AlarmManager::GetInstance();
            
            // 如果闹钟正在响，则关闭闹钟
            if (alarm_mgr.GetWakeUpAlarmState() == kAlarmStateRinging ||
                alarm_mgr.GetSleepAlarmState() == kAlarmStateRinging) {
                alarm_mgr.DismissAlarm();
                return;
            }
            
            if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
                ResetWifiConfiguration();
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        auto& mcp_server = McpServer::GetInstance();
        auto& alarm_mgr = AlarmManager::GetInstance();
        auto& app = Application::GetInstance();
        
        // 设置闹钟管理器的回调
        alarm_mgr.OnWakeUpAlarmTriggered([&app, &alarm_mgr](AlarmRingIntensity intensity) {
            app.Schedule([&app, &alarm_mgr, intensity]() {
                // 播放铃声（根据强度，大模型会自动选择铃声）
                // 这里我们通过 MCP 消息通知服务器播放铃声
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "alarm");
                cJSON_AddStringToObject(json, "alarm_type", "wake_up");
                cJSON_AddStringToObject(json, "intensity", intensity == kAlarmRingIntensityGentle ? "gentle" : "strong");
                char* json_str = cJSON_PrintUnformatted(json);
                app.SendMcpMessage(std::string(json_str));
                cJSON_free(json_str);
                cJSON_Delete(json);
                
                auto display = Board::GetInstance().GetDisplay();
                display->SetStatus("闹钟");
                display->SetEmotion("bell");
                display->SetChatMessage("system", "起床时间到了！");
            });
        });
        
        alarm_mgr.OnSleepAlarmReminder([&app]() {
            app.Schedule([&app]() {
                auto display = Board::GetInstance().GetDisplay();
                display->SetStatus("睡眠提醒");
                display->SetEmotion("moon");
                display->SetChatMessage("system", "还有30分钟就该睡觉了");
                app.PlaySound(Lang::Sounds::OGG_POPUP);
            });
        });
        
        alarm_mgr.OnSleepAlarmStart([&app]() {
            app.Schedule([&app]() {
                // 开始播放助眠音频（舒缓铃声）
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "alarm");
                cJSON_AddStringToObject(json, "alarm_type", "sleep_start");
                char* json_str = cJSON_PrintUnformatted(json);
                app.SendMcpMessage(std::string(json_str));
                cJSON_free(json_str);
                cJSON_Delete(json);
                
                auto display = Board::GetInstance().GetDisplay();
                display->SetStatus("助眠");
                display->SetEmotion("moon");
                display->SetChatMessage("system", "开始播放助眠音频");
            });
        });
        
        alarm_mgr.OnSleepAlarmStop([&app]() {
            app.Schedule([&app]() {
                auto display = Board::GetInstance().GetDisplay();
                display->SetStatus(Lang::Strings::STANDBY);
                display->SetEmotion("neutral");
                display->SetChatMessage("system", "");
            });
        });
        
        alarm_mgr.OnAlarmDismissed([&app, &alarm_mgr](AlarmType type) {
            app.Schedule([&app, &alarm_mgr, type]() {
                auto display = Board::GetInstance().GetDisplay();
                if (type == kAlarmTypeWakeUp) {
                    // 关闭闹钟后，开始播放新闻
                    alarm_mgr.StartNewsBroadcast();
                    display->SetStatus("新闻播报");
                    display->SetEmotion("newspaper");
                    display->SetChatMessage("system", "开始播放新闻");
                    
                    // 通知服务器开始播放新闻
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddStringToObject(json, "type", "news");
                    cJSON_AddStringToObject(json, "action", "start");
                    char* json_str = cJSON_PrintUnformatted(json);
                    app.SendMcpMessage(std::string(json_str));
                    cJSON_free(json_str);
                    cJSON_Delete(json);
                } else {
                    display->SetStatus(Lang::Strings::STANDBY);
                    display->SetEmotion("neutral");
                    display->SetChatMessage("system", "");
                }
            });
        });
        
        // 起床唤醒 - 设置时间
        mcp_server.AddTool("self.alarm.set_wake_up_time",
            "设置起床唤醒时间。时间格式为24小时制。如果不提供时间参数，则使用前一天设置的时间。",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 24, 0, 24),  // 使用24作为未提供的标记
                Property("minute", kPropertyTypeInteger, 60, 0, 60)  // 使用60作为未提供的标记
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int hour = -1, minute = -1;
                
                // 尝试获取参数，如果不存在则使用默认值
                try {
                    hour = properties["hour"].value<int>();
                } catch (...) {
                    hour = -1;
                }
                try {
                    minute = properties["minute"].value<int>();
                } catch (...) {
                    minute = -1;
                }
                
                // 如果没有提供时间，尝试从设置中获取前一天的时间
                if (hour < 0 || minute < 0) {
                    int prev_hour, prev_minute;
                    AlarmRingIntensity prev_intensity;
                    if (alarm_mgr.GetWakeUpAlarm(prev_hour, prev_minute, prev_intensity)) {
                        if (hour >= 24) hour = prev_hour;
                        if (minute >= 60) minute = prev_minute;
                    } else {
                        return std::string("请提供起床时间");
                    }
                }
                
                if (alarm_mgr.SetWakeUpAlarm(hour, minute)) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "起床时间已设置为 %02d:%02d", hour, minute);
                    return std::string(msg);
                } else {
                    return std::string("设置起床时间失败");
                }
            });
        
        // 起床唤醒 - 设置铃声强度
        mcp_server.AddTool("self.alarm.set_wake_up_ring_intensity",
            "设置起床唤醒铃声强度。intensity: 'gentle' 表示舒缓，'strong' 表示强烈。根据强度，大模型会自动选择合适的铃声。",
            PropertyList({
                Property("intensity", kPropertyTypeString)
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                std::string intensity_str = properties["intensity"].value<std::string>();
                AlarmRingIntensity intensity;
                if (intensity_str == "gentle" || intensity_str == "舒缓") {
                    intensity = kAlarmRingIntensityGentle;
                } else if (intensity_str == "strong" || intensity_str == "强烈") {
                    intensity = kAlarmRingIntensityStrong;
                } else {
                    return std::string("无效的强度值，请使用 'gentle' 或 'strong'");
                }
                
                int hour, minute;
                AlarmRingIntensity old_intensity;
                if (alarm_mgr.GetWakeUpAlarm(hour, minute, old_intensity)) {
                    if (alarm_mgr.SetWakeUpAlarm(hour, minute, intensity)) {
                        return std::string("铃声强度已设置为: " + intensity_str);
                    }
                } else {
                    return std::string("请先设置起床时间");
                }
                return std::string("设置铃声强度失败");
            });
        
        // 起床唤醒 - 获取设置
        mcp_server.AddTool("self.alarm.get_wake_up_alarm",
            "获取起床唤醒的设置信息，包括时间和铃声强度。",
            PropertyList(),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int hour, minute;
                AlarmRingIntensity intensity;
                if (alarm_mgr.GetWakeUpAlarm(hour, minute, intensity)) {
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddNumberToObject(json, "hour", hour);
                    cJSON_AddNumberToObject(json, "minute", minute);
                    cJSON_AddStringToObject(json, "intensity", 
                        intensity == kAlarmRingIntensityGentle ? "gentle" : "strong");
                    cJSON_AddBoolToObject(json, "enabled", 
                        alarm_mgr.GetWakeUpAlarmState() == kAlarmStateEnabled);
                    return json;
                } else {
                    return std::string("未设置起床唤醒");
                }
            });
        
        // 起床唤醒 - 启用/禁用
        mcp_server.AddTool("self.alarm.enable_wake_up_alarm",
            "启用或禁用起床唤醒。",
            PropertyList({
                Property("enable", kPropertyTypeBoolean)
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                bool enable = properties["enable"].value<bool>();
                alarm_mgr.EnableWakeUpAlarm(enable);
                return std::string(enable ? "起床唤醒已启用" : "起床唤醒已禁用");
            });
        
        // 起床唤醒 - 延迟提醒
        mcp_server.AddTool("self.alarm.snooze_wake_up_alarm",
            "延迟起床唤醒几分钟后再次提醒。",
            PropertyList({
                Property("minutes", kPropertyTypeInteger, 5, 1, 60)
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int minutes = properties["minutes"].value<int>();
                alarm_mgr.SnoozeAlarm(minutes);
                char msg[64];
                snprintf(msg, sizeof(msg), "已延迟 %d 分钟后再次提醒", minutes);
                return std::string(msg);
            });
        
        // 睡眠提醒 - 设置时间
        mcp_server.AddTool("self.alarm.set_sleep_time",
            "设置睡眠提醒时间。时间格式为24小时制。如果不提供时间参数，则使用前一天设置的时间。",
            PropertyList({
                Property("hour", kPropertyTypeInteger, 24, 0, 24),  // 使用24作为未提供的标记
                Property("minute", kPropertyTypeInteger, 60, 0, 60)  // 使用60作为未提供的标记
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int hour = properties["hour"].value<int>();
                int minute = properties["minute"].value<int>();
                
                // 如果没有提供时间（使用24和60作为未提供的标记），尝试从设置中获取前一天的时间
                if (hour >= 24 || minute >= 60) {
                    int prev_hour, prev_minute;
                    if (alarm_mgr.GetSleepAlarm(prev_hour, prev_minute)) {
                        if (hour >= 24) hour = prev_hour;
                        if (minute >= 60) minute = prev_minute;
                    } else {
                        return std::string("请提供睡眠时间");
                    }
                }
                
                if (alarm_mgr.SetSleepAlarm(hour, minute)) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "睡眠时间已设置为 %02d:%02d", hour, minute);
                    return std::string(msg);
                } else {
                    return std::string("设置睡眠时间失败");
                }
            });
        
        // 睡眠提醒 - 获取设置
        mcp_server.AddTool("self.alarm.get_sleep_alarm",
            "获取睡眠提醒的设置信息。",
            PropertyList(),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int hour, minute;
                if (alarm_mgr.GetSleepAlarm(hour, minute)) {
                    cJSON* json = cJSON_CreateObject();
                    cJSON_AddNumberToObject(json, "hour", hour);
                    cJSON_AddNumberToObject(json, "minute", minute);
                    cJSON_AddBoolToObject(json, "enabled", 
                        alarm_mgr.GetSleepAlarmState() == kAlarmStateEnabled);
                    return json;
                } else {
                    return std::string("未设置睡眠提醒");
                }
            });
        
        // 睡眠提醒 - 启用/禁用
        mcp_server.AddTool("self.alarm.enable_sleep_alarm",
            "启用或禁用睡眠提醒。",
            PropertyList({
                Property("enable", kPropertyTypeBoolean)
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                bool enable = properties["enable"].value<bool>();
                alarm_mgr.EnableSleepAlarm(enable);
                return std::string(enable ? "睡眠提醒已启用" : "睡眠提醒已禁用");
            });
        
        // 睡眠提醒 - 延迟提醒
        mcp_server.AddTool("self.alarm.snooze_sleep_alarm",
            "延迟睡眠提醒几分钟后再次提醒。",
            PropertyList({
                Property("minutes", kPropertyTypeInteger, 5, 1, 60)
            }),
            [&alarm_mgr](const PropertyList& properties) -> ReturnValue {
                int minutes = properties["minutes"].value<int>();
                alarm_mgr.SnoozeAlarm(minutes);
                char msg[64];
                snprintf(msg, sizeof(msg), "已延迟 %d 分钟后再次提醒", minutes);
                return std::string(msg);
            });
        
        // 新闻播报 - 开始播放
        mcp_server.AddTool("self.news.start_broadcast",
            "开始播放新闻播报。大模型会自动抓取不同类型的新闻（天气、科技、政治、经济）并播放。",
            PropertyList(),
            [&alarm_mgr, &app](const PropertyList& properties) -> ReturnValue {
                alarm_mgr.StartNewsBroadcast();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "news");
                cJSON_AddStringToObject(json, "action", "start");
                char* json_str = cJSON_PrintUnformatted(json);
                app.SendMcpMessage(std::string(json_str));
                cJSON_free(json_str);
                cJSON_Delete(json);
                return std::string("开始播放新闻");
            });
        
        // 新闻播报 - 停止播放
        mcp_server.AddTool("self.news.stop_broadcast",
            "停止播放新闻播报。",
            PropertyList(),
            [&alarm_mgr, &app](const PropertyList& properties) -> ReturnValue {
                alarm_mgr.StopNewsBroadcast();
                cJSON* json = cJSON_CreateObject();
                cJSON_AddStringToObject(json, "type", "news");
                cJSON_AddStringToObject(json, "action", "stop");
                char* json_str = cJSON_PrintUnformatted(json);
                app.SendMcpMessage(std::string(json_str));
                cJSON_free(json_str);
                cJSON_Delete(json);
                return std::string("停止播放新闻");
            });
        
        // 番茄钟 - 启动
        mcp_server.AddTool("self.pomodoro.start",
            "启动番茄工作法计时器。将循环执行25分钟工作 + 5分钟休息，每4个循环后进行15分钟长休息。屏幕上会显示倒计时。",
            PropertyList(),
            [this, &app](const PropertyList& properties) -> ReturnValue {
                auto& timer = PomodoroTimer::GetInstance();
                
                if (timer.IsRunning()) {
                    return std::string("番茄钟已在运行中");
                }
                
                timer.Start(&app, display_, [](int minutes, int seconds) {
                    // Callback for each tick (optional)
                });
                
                return std::string("番茄钟已启动，开始25分钟工作");
            });
        
        // 番茄钟 - 暂停/停止
        mcp_server.AddTool("self.pomodoro.stop",
            "停止番茄工作法计时器。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& timer = PomodoroTimer::GetInstance();
                
                if (!timer.IsRunning()) {
                    return std::string("番茄钟未运行");
                }
                
                timer.Stop();
                
                if (display_) {
                    display_->SetStatus(Lang::Strings::STANDBY);
                    display_->SetEmotion("neutral");
                    display_->SetChatMessage("system", "");
                }
                
                return std::string("番茄钟已停止");
            });
        
        // 番茄钟 - 获取状态
        mcp_server.AddTool("self.pomodoro.get_status",
            "获取番茄工作法计时器的当前状态。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& timer = PomodoroTimer::GetInstance();
                
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "is_running", timer.IsRunning());
                
                const char* state_name;
                switch (timer.GetState()) {
                    case kPomodoroStateWorking:
                        state_name = "working";
                        break;
                    case kPomodoroStateBreak:
                        state_name = "short_break";
                        break;
                    case kPomodoroStateLongBreak:
                        state_name = "long_break";
                        break;
                    default:
                        state_name = "idle";
                }
                
                cJSON_AddStringToObject(json, "state", state_name);
                cJSON_AddNumberToObject(json, "loop_count", timer.GetLoopCount());
                
                return json;
            });
        
        // 番茄钟 - 获取今日聚焦时间统计
        mcp_server.AddTool("self.pomodoro.get_daily_focus_time",
            "获取今天的总聚焦（工作）时间统计。显示总工作时间、小时数、分钟数、秒数以及完成的番茄钟数量。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& timer = PomodoroTimer::GetInstance();
                return timer.GetDailyFocusInfo();
            });

        // 冥想 - 启动
        mcp_server.AddTool("self.meditation.start",
            "启动冥想定时器。根据用户是否提及时间设置总时间，如果没有提到时间，默认10分钟。时间结束时播放舒缓铃声唤醒。",
            PropertyList({
                Property("duration_minutes", kPropertyTypeInteger, 0, 0, 120)  // 0表示使用默认值，最大120分钟
            }),
            [this, &app](const PropertyList& properties) -> ReturnValue {
                auto& timer = MeditationTimer::GetInstance();
                
                if (timer.IsRunning()) {
                    return std::string("冥想定时器已在运行中");
                }
                
                int duration_minutes = 0;
                try {
                    duration_minutes = properties["duration_minutes"].value<int>();
                } catch (...) {
                    duration_minutes = 0;  // 使用默认值
                }
                
                timer.Start(&app, display_, duration_minutes, [](int minutes, int seconds) {
                    // Callback for each tick (optional)
                });
                
                if (duration_minutes > 0) {
                    char msg[64];
                    snprintf(msg, sizeof(msg), "冥想定时器已启动，时长 %d 分钟", duration_minutes);
                    return std::string(msg);
                } else {
                    return std::string("冥想定时器已启动，默认时长 10 分钟");
                }
            });
        
        // 冥想 - 停止
        mcp_server.AddTool("self.meditation.stop",
            "停止冥想定时器。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& timer = MeditationTimer::GetInstance();
                
                if (!timer.IsRunning()) {
                    return std::string("冥想定时器未运行");
                }
                
                timer.Stop();
                
                if (display_) {
                    display_->SetStatus(Lang::Strings::STANDBY);
                    display_->SetEmotion("neutral");
                    display_->SetChatMessage("system", "");
                }
                
                return std::string("冥想定时器已停止");
            });
        
        // 冥想 - 获取状态
        mcp_server.AddTool("self.meditation.get_status",
            "获取冥想定时器的当前状态。",
            PropertyList(),
            [this](const PropertyList& properties) -> ReturnValue {
                auto& timer = MeditationTimer::GetInstance();
                
                cJSON* json = cJSON_CreateObject();
                cJSON_AddBoolToObject(json, "is_running", timer.IsRunning());
                
                const char* state_name;
                switch (timer.GetState()) {
                    case kMeditationStateRunning:
                        state_name = "running";
                        break;
                    default:
                        state_name = "idle";
                }
                
                cJSON_AddStringToObject(json, "state", state_name);
                
                if (timer.IsRunning()) {
                    cJSON_AddNumberToObject(json, "remaining_minutes", timer.GetRemainingMinutes());
                    cJSON_AddNumberToObject(json, "remaining_seconds", timer.GetRemainingSeconds());
                }
                
                return json;
            });

        
        ESP_LOGI(TAG, "AI Clock MCP tools initialized");
    }

public:
    AIClockBoard() : boot_button_(BOOT_BUTTON_GPIO) {
        InitializeI2c();
        I2cDetect();
        CheckEchoBaseConnection();
        InitializePi4ioe();
        InitializeLp5562();
        InitializeSpi();
        InitializeGc9107Display();
        InitializeButtons();
        InitializeTools();
        GetBacklight()->RestoreBrightness();
    }

    virtual AudioCodec* GetAudioCodec() override {
        static Es8311AudioCodec audio_codec(
            i2c_bus_, 
            I2C_NUM_1, 
            AUDIO_INPUT_SAMPLE_RATE, 
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK, 
            AUDIO_I2S_GPIO_BCLK, 
            AUDIO_I2S_GPIO_WS, 
            AUDIO_I2S_GPIO_DOUT, 
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_GPIO_PA, 
            AUDIO_CODEC_ES8311_ADDR, 
            false);
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight *GetBacklight() override {
        static CustomBacklight backlight(lp5562_);
        return &backlight;
    }
};

DECLARE_BOARD(AIClockBoard);
