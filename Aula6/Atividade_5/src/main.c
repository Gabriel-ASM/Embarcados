#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/irq.h>
#include <stdint.h>
#include <stdbool.h>
#include <pwm_z42.h>

#define TPM_IRQ_LINE        TPM1_IRQn
#define TPM_IRQ_PRIORITY    1

#define TPM_CLOCK_HZ        48000000ULL
#define TPM_DIVISOR         128ULL

#define TRIGGER_PERIOD_US   60000u
#define TRIGGER_HIGH_US     10u

#define TRIGGER_MOD_TICKS   (((TPM_CLOCK_HZ / TPM_DIVISOR) * TRIGGER_PERIOD_US) / 1000000u - 1u)
#define TRIGGER_HIGH_TICKS  ((((TPM_CLOCK_HZ / TPM_DIVISOR) * TRIGGER_HIGH_US) + 999999u) / 1000000u)

#define PWM_HIGH_TRUE_MODE  (TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK)

volatile uint16_t captura_atual = 0;
volatile uint16_t tempo_subida = 0;
volatile uint16_t tempo_descida = 0;
volatile uint16_t largura_pulso_ticks = 0;
volatile bool medida_disponivel = false;

static uint32_t converter_ticks_para_us(uint16_t qtd_ticks)
{
    return (uint32_t)(((uint64_t)qtd_ticks * TPM_DIVISOR * 1000000ULL) / TPM_CLOCK_HZ);
}

void tpm1_isr(void *arg)
{
    (void)arg;

    static bool subida_detectada = false;

    if (TPM1->STATUS & TPM_STATUS_CH0F_MASK) {
        captura_atual = TPM1->CONTROLS[0].CnV;

        TPM1->STATUS = TPM_STATUS_CH0F_MASK;

        if (GPIOA->PDIR & (1u << 12)) {
            tempo_subida = captura_atual;
            subida_detectada = true;
        } else {
            if (subida_detectada) {
                tempo_descida = captura_atual;

                largura_pulso_ticks = (uint16_t)(tempo_descida - tempo_subida);

                medida_disponivel = true;
                subida_detectada = false;
            }
        }
    }
}

void main(void)
{
    uint16_t ticks_medidos;
    uint32_t tempo_pulso_us;
    uint32_t distancia_cm_x10;

    printk("Iniciando PWM e Input Capture\n");

    IRQ_CONNECT(TPM_IRQ_LINE, TPM_IRQ_PRIORITY, tpm1_isr, NULL, 0);
    irq_enable(TPM_IRQ_LINE);

    pwm_tpm_Init(TPM0, TPM_PLLFLL, TRIGGER_MOD_TICKS, TPM_CLK, PS_128, EDGE_PWM);
    pwm_tpm_Ch_Init(TPM0, 0, PWM_HIGH_TRUE_MODE, GPIOD, 0);
    TPM0->CONTROLS[0].CnV = TRIGGER_HIGH_TICKS;

    pwm_tpm_Init(TPM1, TPM_PLLFLL, 65535, TPM_CLK, PS_128, EDGE_PWM);
    pwm_tpm_Ch_Init(
        TPM1,
        0,
        TPM_INPUT_CAPTURE_RISING | TPM_INPUT_CAPTURE_FALLING | TPM_CHANNEL_INTERRUPT,
        GPIOA,
        12
    );

    printk("PWM gerada em PTD0\n");

    while (1) {
        if (medida_disponivel) {
            unsigned int chave_irq = irq_lock();

            ticks_medidos = largura_pulso_ticks;
            medida_disponivel = false;

            irq_unlock(chave_irq);

            tempo_pulso_us = converter_ticks_para_us(ticks_medidos);
            distancia_cm_x10 = (tempo_pulso_us * 10u) / 58u;

            printk("Pulso alto: %u ticks | %u us | Distancia: %u.%u cm\n",
                   ticks_medidos,
                   tempo_pulso_us,
                   distancia_cm_x10 / 10u,
                   distancia_cm_x10 % 10u);
        }

        k_msleep(100);
    }
}