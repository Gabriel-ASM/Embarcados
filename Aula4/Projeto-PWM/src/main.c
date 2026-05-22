#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include <pwm_z42.h>

#define TPM_MODULE 1000U
#define INPUT_MAX_DIGITS 3U
#define INPUT_IDLE_MS 300
#define ORANGE_GREEN_RATIO 45U

static const struct device *const console =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static char input[INPUT_MAX_DIGITS + 1U];
static size_t input_len;
static int64_t last_char_ms;
static bool input_invalid;

static void apply_orange(uint8_t percent)
{
	uint8_t green_percent = (uint8_t)((percent * ORANGE_GREEN_RATIO) / 100U);
	uint16_t red_cnv = (uint16_t)(TPM_MODULE - ((TPM_MODULE * percent) / 100U));
	uint16_t green_cnv =
		(uint16_t)(TPM_MODULE - ((TPM_MODULE * green_percent) / 100U));

	pwm_tpm_CnV(TPM2, 0, red_cnv);
	pwm_tpm_CnV(TPM2, 1, green_cnv);
}

int main(void)
{
	if (!device_is_ready(console)) {
		return 0;
	}

	pwm_tpm_Init(TPM2, TPM_PLLFLL, TPM_MODULE, TPM_CLK, PS_128, EDGE_PWM);
	pwm_tpm_Ch_Init(TPM2, 0, TPM_PWM_H, GPIOB, 18);
	pwm_tpm_Ch_Init(TPM2, 1, TPM_PWM_H, GPIOB, 19);
	apply_orange(0U);

	printk("*** Controle PWM do tom laranja ***\r\n");
	printk("Digite valores inteiros de 0 a 100.\r\n");

	while (true) {
		unsigned char c;
		bool should_process = false;

		if (uart_poll_in(console, &c) == 0) {
			last_char_ms = k_uptime_get();

			if ((c >= '0') && (c <= '9')) {
				if (!input_invalid) {
					if (input_len < INPUT_MAX_DIGITS) {
						input[input_len++] = (char)c;
						input[input_len] = '\0';
					} else {
						input_invalid = true;
					}
				}
			} else if ((c == '\r') || (c == '\n') || (c == ' ') || (c == '\t')) {
				if ((input_len > 0U) || input_invalid) {
					should_process = true;
				}
			} else if ((c == '\b') || (c == 0x7f)) {
				if (input_invalid) {
					input_len = 0U;
					input[0] = '\0';
					input_invalid = false;
				} else if (input_len > 0U) {
					input[--input_len] = '\0';
				}
			} else {
				input_invalid = true;
			}
		} else if (((input_len > 0U) || input_invalid) &&
			   ((k_uptime_get() - last_char_ms) >= INPUT_IDLE_MS)) {
			should_process = true;
		}

		if (should_process) {
			int value = 0;

			for (size_t i = 0U; i < input_len; i++) {
				value = (value * 10) + (input[i] - '0');
			}

			if (input_invalid || (input_len == 0U) || (value > 100)) {
				printk("Entrada invalida. Use um inteiro de 0 a 100.\r\n");
			} else {
				apply_orange((uint8_t)value);
				printk(
					"Entrada %s -> laranja %d%% (R=%d%%, G=%d%%)\r\n",
					input, value, value,
					(value * ORANGE_GREEN_RATIO) / 100);
			}

			input_len = 0U;
			input[0] = '\0';
			input_invalid = false;
		}

		k_sleep(K_MSEC(1));
	}

	return 0;
}
