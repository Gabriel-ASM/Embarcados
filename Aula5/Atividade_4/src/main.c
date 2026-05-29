#include "MKL25Z4.h"
#include <stdint.h>

#define LED_VERDE_PIN   19u   // PTB19
#define LED_AZUL_PIN     1u   // PTD1

#define ADC_PIN          0u   // PTB0 = A0
#define ADC_CHANNEL      8u   // ADC0_SE8

#define LIMIAR_BAIXO     410u    // ~0.33 V
#define LIMIAR_ALTO      3685u   // ~2.97 V

#define LED_VERDE_MASK   (1u << LED_VERDE_PIN)
#define LED_AZUL_MASK    (1u << LED_AZUL_PIN)

void leds_init(void)
{
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK |
                  SIM_SCGC5_PORTD_MASK;

    PORTB->PCR[LED_VERDE_PIN] = PORT_PCR_MUX(1);
    PORTD->PCR[LED_AZUL_PIN]  = PORT_PCR_MUX(1);

    GPIOB->PDDR |= LED_VERDE_MASK;
    GPIOD->PDDR |= LED_AZUL_MASK;

    // LEDs da FRDM-KL25Z são ativos em nível baixo
    GPIOB->PSOR = LED_VERDE_MASK;  // verde OFF
    GPIOD->PSOR = LED_AZUL_MASK;   // azul OFF
}

void adc_init(void)
{
    // A0 = PTB0 = ADC0_SE8
    SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
    PORTB->PCR[ADC_PIN] = 0x00000000;   // modo analógico, MUX = 0

    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;

    ADC0->SC1[0] = ADC_SC1_ADCH(31);    // desabilita canal durante config
    ADC0->SC2 = 0x00000000;             // trigger por software
    ADC0->SC3 = 0x00000000;             // sem média/calibração

    ADC0->CFG1 = ADC_CFG1_ADICLK(0) |   // bus clock
                 ADC_CFG1_MODE(1)   |   // 12 bits single-ended
                 ADC_CFG1_ADIV(2);      // clock / 4

    ADC0->CFG2 = 0x00000000;
}

uint16_t adc_read(void)
{
    ADC0->SC1[0] = ADC_SC1_ADCH(ADC_CHANNEL);   // inicia conversão em ADC0_SE8

    while (!(ADC0->SC1[0] & ADC_SC1_COCO_MASK)) {
        // espera fim da conversão
    }

    return (uint16_t)ADC0->R[0];
}

void leds_update(uint16_t adc)
{
    if (adc <= LIMIAR_BAIXO) {
        GPIOB->PCOR = LED_VERDE_MASK;   // verde ON
        GPIOD->PSOR = LED_AZUL_MASK;    // azul OFF
    }
    else if (adc >= LIMIAR_ALTO) {
        GPIOB->PSOR = LED_VERDE_MASK;   // verde OFF
        GPIOD->PCOR = LED_AZUL_MASK;    // azul ON
    }
    else {
        GPIOB->PSOR = LED_VERDE_MASK;   // verde OFF
        GPIOD->PSOR = LED_AZUL_MASK;    // azul OFF
    }
}

int main(void)
{
    uint16_t adc;

    leds_init();
    adc_init();

    while (1) {
        adc = adc_read();
        leds_update(adc);
    }
}