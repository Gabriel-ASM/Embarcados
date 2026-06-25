#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <stdint.h>
#include <stdbool.h>

/* THREADS */
#define ADC_THREAD_STACK_SIZE     1024
#define ACCEL_THREAD_STACK_SIZE   1024
#define ADC_THREAD_PRIORITY       5
#define ACCEL_THREAD_PRIORITY     4

K_THREAD_STACK_DEFINE(adc_stack, ADC_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(accel_stack, ACCEL_THREAD_STACK_SIZE);

static struct k_thread adc_thread_data;
static struct k_thread accel_thread_data;

/* ADC */
#define ADC_RESOLUTION            12
#define ADC_GAIN_CFG              ADC_GAIN_1

/*
 * Para a FRDM-KL25Z, usar referência externa com VREF ~= 3,3 V
 * costuma ser mais coerente quando a conversão para mV assume 3300 mV.
 *
 * Se a sua versão/placa exigir ADC_REF_INTERNAL, troque aqui.
 */
#define ADC_REFERENCE_CFG         ADC_REF_EXTERNAL0
#define ADC_ACQUISITION_TIME_CFG  ADC_ACQ_TIME_DEFAULT
#define ADC_CHANNEL_ID            0
#define ADC_VREF_MV               3300

#define ADC_NODE                  DT_NODELABEL(adc0)

#if !DT_NODE_HAS_STATUS(ADC_NODE, okay)
#error "ADC0 nao esta habilitado no DeviceTree."
#endif

static const struct device *adc_dev = DEVICE_DT_GET(ADC_NODE);
static int16_t sample_buffer;

static struct adc_channel_cfg adc_channel_cfg = {
    .gain = ADC_GAIN_CFG,
    .reference = ADC_REFERENCE_CFG,
    .acquisition_time = ADC_ACQUISITION_TIME_CFG,
    .channel_id = ADC_CHANNEL_ID,
    .differential = 0,
};

static struct adc_sequence adc_sequence_cfg = {
    .channels = BIT(ADC_CHANNEL_ID),
    .buffer = &sample_buffer,
    .buffer_size = sizeof(sample_buffer),
    .resolution = ADC_RESOLUTION,
};

/* ACELERÔMETRO */
#if DT_NODE_HAS_STATUS(DT_ALIAS(accel0), okay)
#define ACCEL_NODE DT_ALIAS(accel0)
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(mma8451q), okay)
#define ACCEL_NODE DT_NODELABEL(mma8451q)
#else
#error "Nenhum acelerometro valido encontrado: tente alias accel0 ou nodelabel mma8451q."
#endif

static const struct device *accel = DEVICE_DT_GET(ACCEL_NODE);

/* BOTÃO */
#if DT_NODE_HAS_STATUS(DT_ALIAS(sw0), okay)
#define BUTTON_NODE DT_ALIAS(sw0)
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(user_button_0), okay)
#define BUTTON_NODE DT_NODELABEL(user_button_0)
#else
#error "Nenhum botao valido encontrado: tente alias sw0 ou nodelabel user_button_0."
#endif

#define BUTTON_DEBOUNCE_MS 150

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);
static struct gpio_callback button_cb_data;

/* ESTADO GLOBAL */
static atomic_t complete_mode;
static int64_t last_button_ms = -BUTTON_DEBOUNCE_MS;

K_SEM_DEFINE(button_sem, 0, 1);
K_MUTEX_DEFINE(print_mutex);

static bool is_complete_mode(void)
{
    return atomic_get(&complete_mode) != 0;
}

static void print_sensor_value(const struct sensor_value *value)
{
    int64_t micro = ((int64_t)value->val1 * 1000000LL) + value->val2;

    if (micro < 0) {
        printk("-");
        micro = -micro;
    }

    printk("%d.%06d",
           (int)(micro / 1000000LL),
           (int)(micro % 1000000LL));
}

/* ISR DO BOTÃO */
static void button_isr(const struct device *dev,
                       struct gpio_callback *cb,
                       uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);

    int64_t now = k_uptime_get();

    if ((now - last_button_ms) < BUTTON_DEBOUNCE_MS) {
        return;
    }

    last_button_ms = now;

    atomic_set(&complete_mode, !is_complete_mode());
    k_sem_give(&button_sem);
}

/* THREAD ADC */
static void adc_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (1) {
        int err = adc_read(adc_dev, &adc_sequence_cfg);

        if (err != 0) {
            k_mutex_lock(&print_mutex, K_FOREVER);
            printk("[ADC] Falha na leitura: %d\n", err);
            k_mutex_unlock(&print_mutex);
        } else {
            int32_t mv = sample_buffer;

            err = adc_raw_to_millivolts(ADC_VREF_MV,
                                        ADC_GAIN_CFG,
                                        ADC_RESOLUTION,
                                        &mv);

            k_mutex_lock(&print_mutex, K_FOREVER);

            if (err != 0) {
                printk("[ADC] Leitura raw: %d | erro na conversao para mV: %d\n",
                       sample_buffer, err);
            } else {
                printk("ADC: %d mV (raw: %d)\n", mv, sample_buffer);
            }

            k_mutex_unlock(&print_mutex);
        }

        k_sleep(K_MSEC(500));
    }
}

/* THREAD ACELERÔMETRO */
static void accel_thread(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    struct sensor_value accel_x;
    struct sensor_value accel_y;
    struct sensor_value accel_z;

    while (1) {
        if (is_complete_mode()) {
            int ret = sensor_sample_fetch(accel);

            if (ret != 0) {
                k_mutex_lock(&print_mutex, K_FOREVER);
                printk("[ACCEL] Falha no sensor_sample_fetch: %d\n", ret);
                k_mutex_unlock(&print_mutex);
            } else {
                ret = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &accel_x);
                if (ret == 0) {
                    ret = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &accel_y);
                }
                if (ret == 0) {
                    ret = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &accel_z);
                }

                k_mutex_lock(&print_mutex, K_FOREVER);

                if (ret != 0) {
                    printk("[ACCEL] Falha ao obter canais: %d\n", ret);
                } else {
                    printk("ACCEL: X=");
                    print_sensor_value(&accel_x);

                    printk(" | Y=");
                    print_sensor_value(&accel_y);

                    printk(" | Z=");
                    print_sensor_value(&accel_z);

                    printk("\n");
                }

                k_mutex_unlock(&print_mutex);
            }
        }

        k_sleep(K_MSEC(1000));
    }
}

/* MAIN */
int main(void)
{
    int err;

    printk("---> MODO ADC <---\n\n");

    /* Botão */
    if (!gpio_is_ready_dt(&button)) {
        printk("ERRO: Botao nao esta disponivel\n");
        return 0;
    }

    err = gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_UP);
    if (err != 0) {
        printk("ERRO: Falha ao configurar botao: %d\n", err);
        return 0;
    }

    gpio_init_callback(&button_cb_data, button_isr, BIT(button.pin));

    err = gpio_add_callback(button.port, &button_cb_data);
    if (err != 0) {
        printk("ERRO: Falha ao adicionar callback do botao: %d\n", err);
        return 0;
    }

    err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_TO_ACTIVE);
    if (err != 0) {
        printk("ERRO: Falha ao configurar interrupcao do botao: %d\n", err);
        return 0;
    }

    /* ADC */
    if (!device_is_ready(adc_dev)) {
        printk("ERRO: ADC nao esta pronto\n");
        return 0;
    }

    err = adc_channel_setup(adc_dev, &adc_channel_cfg);
    if (err != 0) {
        printk("ERRO: Falha no adc_channel_setup: %d\n", err);
        return 0;
    }

    /* Acelerômetro */
    if (!device_is_ready(accel)) {
        printk("ERRO: Acelerometro nao esta pronto\n");
        return 0;
    }

    /* Threads */
    k_thread_create(&adc_thread_data,
                    adc_stack,
                    K_THREAD_STACK_SIZEOF(adc_stack),
                    adc_thread,
                    NULL,
                    NULL,
                    NULL,
                    ADC_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    k_thread_create(&accel_thread_data,
                    accel_stack,
                    K_THREAD_STACK_SIZEOF(accel_stack),
                    accel_thread,
                    NULL,
                    NULL,
                    NULL,
                    ACCEL_THREAD_PRIORITY,
                    0,
                    K_NO_WAIT);

    while (1) {
        k_sem_take(&button_sem, K_FOREVER);

        k_mutex_lock(&print_mutex, K_FOREVER);

        if (is_complete_mode()) {
            printk("\n---> MODO COMPLETO: ADC + ACELEROMETRO <---\n\n");
        } else {
            printk("\n---> MODO ADC <---\n\n");
        }

        k_mutex_unlock(&print_mutex);
    }

    return 0;
}