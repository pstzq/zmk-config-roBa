/*
 * ノッチ同期型ロータリーエンコーダドライバ (ZMK 純正 alps,ec11 の置き換え)。
 *
 * 純正 ec11 との違い:
 *  1. 割り込みを止めない。ec11 はエッジ割り込みの冒頭で A/B 両方の割り込みを
 *     切り、ワークキューで処理し終えてから戻す。その間のエッジは失われ、
 *     2遷移まとめて見えると 0 として捨てられていた。ここでは ISR 内で
 *     その場でピンを読んでデコードする (数µs)。
 *  2. 角度の積算をやめ、ノッチ単位で報告する (detent_decoder.h 参照)。
 *     1ノッチ = 360/detents-per-rotation 度をちょうど1回報告するので、
 *     sensor-rotate 側の端数が常に 0 のまま保たれ、位相ずれが起きない。
 */

#define DT_DRV_COMPAT roba_detent_encoder

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/logging/log.h>

#include "detent_decoder.h"

LOG_MODULE_REGISTER(detent_encoder, CONFIG_SENSOR_LOG_LEVEL);

/* 最後のエッジからこの時間が経ったらピンを読み直す (最後のエッジを
 * 取りこぼしていてもここでノッチが確定する) */
#define IDLE_POLL_MS 20
/* 静止位置以外で止まったままこの回数 (x IDLE_POLL_MS) 経ったら、そこを
 * 静止位置として学び直す。起動直後のプルアップ未安定などで誤った状態を
 * 静止位置として覚えた場合の保険。ノッチの途中で指を止めていても
 * 学び直さないよう長めにとる */
#define RELEARN_POLLS 50

struct detent_encoder_config {
    struct gpio_dt_spec a;
    struct gpio_dt_spec b;
    int32_t degrees_per_detent;
};

struct detent_encoder_data {
    const struct device *dev;
    struct gpio_callback a_cb;
    struct gpio_callback b_cb;
    struct k_spinlock lock;
    struct detent_decoder dec;
    int32_t pending; /* ISR が確定させ、まだ報告していないノッチ数 */
    int32_t fetched; /* sample_fetch で取り出したノッチ数 */
    uint8_t idle_polls;
    struct k_work report_work;
    struct k_work_delayable idle_work;
    const struct sensor_trigger *trigger;
    sensor_trigger_handler_t handler;
};

static uint8_t read_ab(const struct device *dev) {
    const struct detent_encoder_config *cfg = dev->config;
    return (gpio_pin_get_dt(&cfg->a) << 1) | gpio_pin_get_dt(&cfg->b);
}

/* lock を持った状態で呼ぶ。確定ノッチがあれば true */
static bool feed_locked(struct detent_encoder_data *data, uint8_t ab) {
    int n = detent_decoder_feed(&data->dec, ab);
    data->pending += n;
    return n != 0;
}

static void edge_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins,
                    struct detent_encoder_data *data) {
    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool notch = feed_locked(data, read_ab(data->dev));
    data->idle_polls = 0;
    k_spin_unlock(&data->lock, key);

    k_work_reschedule(&data->idle_work, K_MSEC(IDLE_POLL_MS));
    if (notch) {
        k_work_submit(&data->report_work);
    }
}

static void a_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    edge_cb(port, cb, pins, CONTAINER_OF(cb, struct detent_encoder_data, a_cb));
}

static void b_cb(const struct device *port, struct gpio_callback *cb, uint32_t pins) {
    edge_cb(port, cb, pins, CONTAINER_OF(cb, struct detent_encoder_data, b_cb));
}

static void idle_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct detent_encoder_data *data = CONTAINER_OF(dwork, struct detent_encoder_data, idle_work);

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    uint8_t ab = read_ab(data->dev);
    uint8_t old_rest = data->dec.rest;
    bool notch = feed_locked(data, ab);
    bool at_rest = data->dec.state == data->dec.rest;
    bool relearned = false;
    if (!at_rest && ++data->idle_polls >= RELEARN_POLLS) {
        detent_decoder_relearn(&data->dec, ab);
        data->idle_polls = 0;
        at_rest = true;
        relearned = true;
    }
    k_spin_unlock(&data->lock, key);

    if (relearned) {
        LOG_INF("relearned rest state %d -> %d", old_rest, ab);
    }
    if (!at_rest) {
        k_work_reschedule(dwork, K_MSEC(IDLE_POLL_MS));
    }
    if (notch) {
        k_work_submit(&data->report_work);
    }
}

static void report_work_cb(struct k_work *work) {
    struct detent_encoder_data *data = CONTAINER_OF(work, struct detent_encoder_data, report_work);

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    bool has_pending = data->pending != 0;
    k_spin_unlock(&data->lock, key);

    if (has_pending && data->handler) {
        data->handler(data->dev, data->trigger);
    }
}

static int detent_encoder_sample_fetch(const struct device *dev, enum sensor_channel chan) {
    struct detent_encoder_data *data = dev->data;

    if (chan != SENSOR_CHAN_ALL && chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    k_spinlock_key_t key = k_spin_lock(&data->lock);
    data->fetched = data->pending;
    data->pending = 0;
    k_spin_unlock(&data->lock, key);
    return 0;
}

static int detent_encoder_channel_get(const struct device *dev, enum sensor_channel chan,
                                      struct sensor_value *val) {
    const struct detent_encoder_config *cfg = dev->config;
    struct detent_encoder_data *data = dev->data;

    if (chan != SENSOR_CHAN_ROTATION) {
        return -ENOTSUP;
    }

    /* ノッチ数 x ちょうどの角度。val2 (小数部) は常に 0 */
    val->val1 = data->fetched * cfg->degrees_per_detent;
    val->val2 = 0;
    data->fetched = 0;
    return 0;
}

static int detent_encoder_trigger_set(const struct device *dev, const struct sensor_trigger *trig,
                                      sensor_trigger_handler_t handler) {
    struct detent_encoder_data *data = dev->data;

    data->trigger = trig;
    data->handler = handler;
    return 0;
}

static const struct sensor_driver_api detent_encoder_api = {
    .trigger_set = detent_encoder_trigger_set,
    .sample_fetch = detent_encoder_sample_fetch,
    .channel_get = detent_encoder_channel_get,
};

static int detent_encoder_init(const struct device *dev) {
    const struct detent_encoder_config *cfg = dev->config;
    struct detent_encoder_data *data = dev->data;
    int err;

    data->dev = dev;

    if (!gpio_is_ready_dt(&cfg->a) || !gpio_is_ready_dt(&cfg->b)) {
        LOG_ERR("GPIO not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&cfg->a, GPIO_INPUT);
    if (!err) {
        err = gpio_pin_configure_dt(&cfg->b, GPIO_INPUT);
    }
    if (err) {
        LOG_ERR("Failed to configure pins (%d)", err);
        return err;
    }

    /* 内蔵プルアップが立ち上がるのを待ってから静止位置を読む */
    k_busy_wait(1000);
    detent_decoder_init(&data->dec, read_ab(dev));

    k_work_init(&data->report_work, report_work_cb);
    k_work_init_delayable(&data->idle_work, idle_work_cb);

    gpio_init_callback(&data->a_cb, a_cb, BIT(cfg->a.pin));
    gpio_init_callback(&data->b_cb, b_cb, BIT(cfg->b.pin));
    err = gpio_add_callback_dt(&cfg->a, &data->a_cb);
    if (!err) {
        err = gpio_add_callback_dt(&cfg->b, &data->b_cb);
    }
    if (!err) {
        err = gpio_pin_interrupt_configure_dt(&cfg->a, GPIO_INT_EDGE_BOTH);
    }
    if (!err) {
        err = gpio_pin_interrupt_configure_dt(&cfg->b, GPIO_INT_EDGE_BOTH);
    }
    if (err) {
        LOG_ERR("Failed to set up interrupts (%d)", err);
        return err;
    }

    return 0;
}

#define DETENT_ENCODER_INST(n)                                                                     \
    BUILD_ASSERT(360 % DT_INST_PROP(n, detents_per_rotation) == 0,                                 \
                 "detents-per-rotation must divide 360");                                          \
    static struct detent_encoder_data detent_encoder_data_##n;                                     \
    static const struct detent_encoder_config detent_encoder_cfg_##n = {                           \
        .a = GPIO_DT_SPEC_INST_GET(n, a_gpios),                                                    \
        .b = GPIO_DT_SPEC_INST_GET(n, b_gpios),                                                    \
        .degrees_per_detent = 360 / DT_INST_PROP(n, detents_per_rotation),                         \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, detent_encoder_init, NULL, &detent_encoder_data_##n,                  \
                          &detent_encoder_cfg_##n, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,       \
                          &detent_encoder_api);

DT_INST_FOREACH_STATUS_OKAY(DETENT_ENCODER_INST)
