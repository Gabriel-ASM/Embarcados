#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#define DELAY_MS 6000

#define LED_VERDE_NODE    DT_ALIAS(led0)
#define LED_AZUL_NODE     DT_ALIAS(led1)
#define LED_VERMELHO_NODE DT_ALIAS(led2)

#if !DT_NODE_HAS_STATUS(LED_VERDE_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_AZUL_NODE, okay) || \
    !DT_NODE_HAS_STATUS(LED_VERMELHO_NODE, okay)
#error "Os aliases led0, led1 e led2 precisam estar definidos no Device Tree"
#endif

static const struct gpio_dt_spec verde =
    GPIO_DT_SPEC_GET(LED_VERDE_NODE, gpios);

static const struct gpio_dt_spec azul =
    GPIO_DT_SPEC_GET(LED_AZUL_NODE, gpios);

static const struct gpio_dt_spec vermelho =
    GPIO_DT_SPEC_GET(LED_VERMELHO_NODE, gpios);

enum estado {
    VERDE,
    AMARELO,
    VERMELHO
};

static void configurar_leds(void)
{
    gpio_pin_configure_dt(&verde, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&azul, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&vermelho, GPIO_OUTPUT_INACTIVE);
}

static void set_semaforo(enum estado e)
{
    gpio_pin_set_dt(&verde,    e == VERDE || e == AMARELO);
    gpio_pin_set_dt(&vermelho, e == VERMELHO || e == AMARELO);
    gpio_pin_set_dt(&azul,     0);
}

void main(void)
{
    configurar_leds();

    while (1) {
        set_semaforo(VERDE);
        k_msleep(DELAY_MS);

        set_semaforo(AMARELO);
        k_msleep(DELAY_MS);

        set_semaforo(VERMELHO);
        k_msleep(DELAY_MS);
    }
}