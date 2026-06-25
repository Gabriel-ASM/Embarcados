#include <stdint.h>

#include <zephyr/kernel.h>

#define CLOCK_PORTB_MASK    (1u << 10)
#define LED_GREEN_MASK      (1u << 19)
#define PORT_PCR_MUX_GPIO   (1u << 8)
#define BLINK_PERIOD_MS     1000U

#define SIM_SCGC5_ADDRESS   0x40048038u
#define PORTB_PCR19_ADDRESS 0x4004A04Cu
#define GPIOB_PSOR_ADDRESS  0x400FF044u
#define GPIOB_PCOR_ADDRESS  0x400FF048u
#define GPIOB_PDDR_ADDRESS  0x400FF054u

static volatile uint32_t *const sim_scgc5 = (volatile uint32_t *)SIM_SCGC5_ADDRESS;
static volatile uint32_t *const portb_pcr19 = (volatile uint32_t *)PORTB_PCR19_ADDRESS;
static volatile uint32_t *const gpiob_psor = (volatile uint32_t *)GPIOB_PSOR_ADDRESS;
static volatile uint32_t *const gpiob_pcor = (volatile uint32_t *)GPIOB_PCOR_ADDRESS;
static volatile uint32_t *const gpiob_pddr = (volatile uint32_t *)GPIOB_PDDR_ADDRESS;

static void led_green_init(void)
{
	*sim_scgc5 |= CLOCK_PORTB_MASK;
	*portb_pcr19 = PORT_PCR_MUX_GPIO;
	*gpiob_pddr |= LED_GREEN_MASK;
}

static void led_green_on(void)
{
	*gpiob_pcor = LED_GREEN_MASK;
}

static void led_green_off(void)
{
	*gpiob_psor = LED_GREEN_MASK;
}

int main(void)
{
	led_green_init();

	while (1) {
		led_green_on();
		k_msleep(BLINK_PERIOD_MS);

		led_green_off();
		k_msleep(BLINK_PERIOD_MS);
	}

	return 0;
}