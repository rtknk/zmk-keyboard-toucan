#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/keys.h>
#include <math.h>

#define DT_DRV_COMPAT zmk_input_processor_toucan_circle

// 40mm CirqueのPinnacle最大解像度範囲（通常0〜2047、中心1024）
#define TRACKPAD_CENTER_X 1024
#define TRACKPAD_CENTER_Y 1024
#define TRACKPAD_RADIUS_MAX 1024

// 辺縁2.5mmに相当するカウント閾値（半径約896カウント以上を外周とする）
#define RIM_THRESHOLD_RADIUS 896 

// 角度のラジアンから度への変換
#define RAD_TO_DEG(r) ((r) * 180.0 / M_PI)

struct ip_toucan_circle_config {
    uint8_t fn_layer_index; // Fnキーが配置されているレイヤー番号（例：3）
};

struct ip_toucan_circle_data {
    int16_t last_x;
    int16_t last_y;
    double last_angle;
    double accumulated_angle;
    bool is_first_touch;
};

// 特定のキー入力を擬似的に発生させるヘルパー関数
static void send_key(uint32_t usage_id, bool press) {
    if (press) {
        zmk_hid_register_key(usage_id);
    } else {
        zmk_hid_unregister_key(usage_id);
    }
    // HIDレポートを更新
    // ※実際のトグル動作のためには、zmk_endpoints_send_reportなど適切なHID同期イベントが必要です
}

static void process_toucan_circle(const struct device *dev, struct input_event *evt) {
    const struct ip_toucan_circle_config *config = dev->config;
    struct ip_toucan_circle_data *data = dev->data;

    // タッチが離れた（Touch Up）イベントの検知
    if (evt->type == INPUT_EV_KEY && evt->code == INPUT_BTN_TOUCH && evt->value == 0) {
        data->is_first_touch = true;
        data->accumulated_angle = 0.0;
        return;
    }

    // 座標イベントの追跡（Absolute Modeの生データを想定）
    if (evt->type == INPUT_EV_ABS) {
        if (evt->code == INPUT_ABS_X) data->last_x = evt->value;
        if (evt->code == INPUT_ABS_Y) data->last_y = evt->value;

        // 中心からの相対座標を算出
        int32_t dx = data->last_x - TRACKPAD_CENTER_X;
        int32_t dy = data->last_y - TRACKPAD_CENTER_Y;

        // 三平方の定理で半径の距離を確認
        uint32_t current_r = (uint32_t)sqrt((dx * dx) + (dy * dy));

        // 辺縁2.5mmエリアに指がいない場合は、通常の処理にバイパス（スワイプ/移動用）
        if (current_r < RIM_THRESHOLD_RADIUS) {
            data->is_first_touch = true;
            data->accumulated_angle = 0.0;
            return; // 通常のマウス移動やスクロールレイヤー（DTS側）に処理を任せる
        }

        // 三角関数 atan2 で角度θを算出（結果は -π 〜 +π）
        double current_angle = atan2((double)dy, (double)dx);
        if (current_angle < 0) current_angle += 2 * M_PI; // 0 〜 2π (0°〜360°)に補正

        if (data->is_first_touch) {
            data->last_angle = current_angle;
            data->is_first_touch = false;
            return;
        }

        // 角度の変化量（dθ）を計算
        double d_angle = current_angle - data->last_angle;
        if (d_angle > M_PI) d_angle -= 2 * M_PI;
        else if (d_angle < -M_PI) d_angle += 2 * M_PI;

        data->accumulated_angle += RAD_TO_DEG(d_angle);
        data->last_angle = current_angle;

        // 現在Fnレイヤー（レイヤー3）がアクティブかどうかを取得
        bool fn_active = zmk_keymap_layer_active(config->fn_layer_index);

        // 指が現在の「上半周」か「下半周」かを判定
        bool is_upper_half = (current_angle >= 0 && current_angle < M_PI); // 0°〜180°

        // ----------------------------------------
        // ジェスチャー判定ロジック
        // ----------------------------------------
        if (fn_active) {
            // 【Fnホールド状態】1周（360度）回転の監視
            if (data->accumulated_angle >= 360.0) {
                // 時計回り：ピンチアウト
                send_key(HID_USAGE_KEY_KEYBOARD_LCTRL, true);
                // マウスホイール上イベントをシミュレートする処理が理想（簡易的にキー等で代用）
                send_key(HID_USAGE_KEY_KEYBOARD_LCTRL, false);
                data->accumulated_angle = 0.0;
            } else if (data->accumulated_angle <= -360.0) {
                // 反時計回り：ピンチイン
                send_key(HID_USAGE_KEY_KEYBOARD_LCTRL, true);
                // マウスホイール下イベントをシミュレート
                send_key(HID_USAGE_KEY_KEYBOARD_LCTRL, false);
                data->accumulated_angle = 0.0;
            }
        } else {
            // 【通常状態】
            if (is_upper_half) {
                // 上半周：音量調整（閾値30度）
                if (data->accumulated_angle >= 30.0) {
                    // 時計回り：音量1段階UP
                    send_key(HID_USAGE_CONSUMER_VOLUME_INCREMENT, true);
                    send_key(HID_USAGE_CONSUMER_VOLUME_INCREMENT, false);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -30.0) {
                    // 反時計回り：音量1段階DOWN
                    send_key(HID_USAGE_CONSUMER_VOLUME_DECREMENT, true);
                    send_key(HID_USAGE_CONSUMER_VOLUME_DECREMENT, false);
                    data->accumulated_angle = 0.0;
                }
            } else {
                // 下半周：ページ進み・戻り（閾値60度）
                if (data->accumulated_angle >= 60.0) {
                    // 時計回り：1ページ戻り（Alt + Left）
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ALT, true);
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ARROW, true);
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ARROW, false);
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ALT, false);
                    data->accumulated_angle = 0.0;
                } else if (data->accumulated_angle <= -60.0) {
                    // 反時計回り：1ページ送り（Alt + Right）
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ALT, true);
                    send_key(HID_USAGE_KEY_KEYBOARD_RIGHT_ARROW, true);
                    send_key(HID_USAGE_KEY_KEYBOARD_RIGHT_ARROW, false);
                    send_key(HID_USAGE_KEY_KEYBOARD_LEFT_ALT, false);
                    data->accumulated_angle = 0.0;
                }
            }
        }
        
        // このプロセッサでイベントを完全に消費し、通常のマウスカーソル移動を殺す場合はイベントを消去
        // 外周操作時はカーソルが動かない方が誤操作を防げます
        evt->type = INPUT_EV_DUMMY; 
    }
}

// Zephyrのインプットプロセッサ APIの初期化マクロ群
static int ip_toucan_circle_init(const struct device *dev) { return 0; }

#define INST_IP_TOUCAN_CIRCLE(n)                                              \
    static struct ip_toucan_circle_data ip_toucan_circle_data_##n = {         \
        .is_first_touch = true,                                               \
        .accumulated_angle = 0.0,                                             \
    };                                                                        \
    static const struct ip_toucan_circle_config ip_toucan_circle_config_##n = { \
        .fn_layer_index = DT_INST_PROP(n, fn_layer_index),                    \
    };                                                                        \
    INPUT_PROCESSOR_DEFINE(DT_DRV_INST(n), process_toucan_circle,             \
                           &ip_toucan_circle_data_##n,                        \
                           &ip_toucan_circle_config_##n,                      \
                           ip_toucan_circle_init, POST_KERNEL,                \
                           CONFIG_APPLICATION_INIT_PRIORITY);

DT_INST_FOREACH_STATUS_OKAY(INST_IP_TOUCAN_CIRCLE)
