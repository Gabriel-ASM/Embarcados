#include "MKL25Z4.h"
#include <stdbool.h>
#include <stdint.h>

#define LED_VERDE_PIN   19u
#define LED_AZUL_PIN     1u
#define ADC_PIN          0u
#define ADC_CHANNEL      8u
#define LIMIAR_BAIXO     410u
#define LIMIAR_ALTO      3685u

#define LED_VERDE_MASK   (1u << LED_VERDE_PIN)
#define LED_AZUL_MASK    (1u << LED_AZUL_PIN)

static void leds_apply(bool verde_on, bool azul_on)
{
    if (verde_on) {
        GPIOB->PCOR = LED_VERDE_MASK;
    }
    else {
        GPIOB->PSOR = LED_VERDE_MASK;
    }

    if (azul_on) {
        GPIOD->PCOR = LED_AZUL_MASK;
    }
    else {
        GPIOD->PSOR = LED_AZUL_MASK;
    }
}

static void leds_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK |
                  SIM_SCGC5_PORTD_MASK;

    PORTB->PCR[LED_VERDE_PIN] = PORT_PCR_MUX(1);
    PORTD->PCR[LED_AZUL_PIN]  = PORT_PCR_MUX(1);

    GPIOB->PDDR |= LED_VERDE_MASK;
    GPIOD->PDDR |= LED_AZUL_MASK;

    leds_apply(false, false);
}

static void adc_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    PORTB->PCR[ADC_PIN] = PORT_PCR_MUX(0);

    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;

    ADC0->SC1[0] = ADC_SC1_ADCH(31);
    ADC0->SC2 = 0u;
    ADC0->SC3 = 0u;

    ADC0->CFG1 = ADC_CFG1_ADICLK(0) |
                 ADC_CFG1_MODE(1)   |
                ADC_CFG1_ADIV(2);

    ADC0->CFG2 = 0u;
}

static uint16_t adc_read(void)
{
    ADC0->SC1[0] = ADC_SC1_ADCH(ADC_CHANNEL);

    while (!(ADC0->SC1[0] & ADC_SC1_COCO_MASK)) {
    }

    return (uint16_t)ADC0->R[0];
}

static void leds_update(uint16_t adc_value)
{
    if (adc_value <= LIMIAR_BAIXO) {
        leds_apply(true, false);
    }
    else if (adc_value >= LIMIAR_ALTO) {
        leds_apply(false, true);
    }
    else {
        leds_apply(false, false);
    }
}

int main(void)
{
    uint16_t adc_value;

    leds_init();
    adc_init();

    while (1) {
        adc_value = adc_read();
        leds_update(adc_value);
    }
}