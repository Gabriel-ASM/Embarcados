#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#define SEMAFORO_DELAY_MS 600

// LEDs via Device Tree aliases (led0/led1/led2)
#define LED0_NODE DT_ALIAS(led0)
#define LED1_NODE DT_ALIAS(led1)
#define LED2_NODE DT_ALIAS(led2)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay) && \
    DT_NODE_HAS_STATUS(LED1_NODE, okay) && \
    DT_NODE_HAS_STATUS(LED2_NODE, okay)
static const struct gpio_dt_spec led_vermelho = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec led_amarelo = GPIO_DT_SPEC_GET(LED1_NODE, gpios);
static const struct gpio_dt_spec led_verde = GPIO_DT_SPEC_GET(LED2_NODE, gpios);
#else
#error "Unsupported board: led0/led1/led2 devicetree aliases are not defined"
#endif

enum semaforo_estado {
    SEM_VERDE = 0,
    SEM_AMARELO,
    SEM_VERMELHO,
    SEM_ESTADOS
};

struct semaforo_step {
    enum semaforo_estado estado;
    uint32_t tempo_ms;
};

static const struct semaforo_step sequencia[] = {
    { SEM_VERDE, SEMAFORO_DELAY_MS },
    { SEM_AMARELO, SEMAFORO_DELAY_MS },
    { SEM_VERMELHO, SEMAFORO_DELAY_MS },
};

static int configurar_led(const struct gpio_dt_spec *led)
{
    if (!gpio_is_ready_dt(led)) {
        printk("Error: LED device %s is not ready\n", led->port->name);
        return -ENODEV;
    }

    return gpio_pin_configure_dt(led, GPIO_OUTPUT_INACTIVE);
}

static void aplicar_estado(enum semaforo_estado estado)
{
    gpio_pin_set_dt(&led_verde, estado == SEM_VERDE);
    gpio_pin_set_dt(&led_amarelo, estado == SEM_AMARELO);
    gpio_pin_set_dt(&led_vermelho, estado == SEM_VERMELHO);
}

void main(void)
{
    int ret;

    ret = configurar_led(&led_vermelho);
    if (ret < 0) {
        printk("Error %d: failed to configure red LED\n", ret);
        return;
    }

    ret = configurar_led(&led_amarelo);
    if (ret < 0) {
        printk("Error %d: failed to configure yellow LED\n", ret);
        return;
    }

    ret = configurar_led(&led_verde);
    if (ret < 0) {
        printk("Error %d: failed to configure green LED\n", ret);
        return;
    }

    while (1) {
        for (size_t i = 0; i < ARRAY_SIZE(sequencia); i++) {
            aplicar_estado(sequencia[i].estado);
            k_msleep(sequencia[i].tempo_ms);
        }
    }
}
