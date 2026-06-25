#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#ifndef APP_USA_LOGGING
#define APP_USA_LOGGING 0
#endif

#if APP_USA_LOGGING
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(atividade8, LOG_LEVEL_INF);
#define APP_PRINT(fmt, ...) LOG_INF(fmt, ##__VA_ARGS__)
#else
#define APP_PRINT(fmt, ...) printk(fmt "\n", ##__VA_ARGS__)
#endif

#define NO_ACELEROMETRO DT_ALIAS(accel0)

#if !DT_NODE_HAS_STATUS(NO_ACELEROMETRO, okay)
#error "Alias accel0 nao habilitado para a FRDM-KL25Z"
#endif

#ifndef APP_USA_FIR
#define APP_USA_FIR 0
#endif

#ifndef APP_TEMPO_ETAPA_S
#define APP_TEMPO_ETAPA_S 15
#endif

#ifndef APP_TAM_FILA_AMOSTRAS
#define APP_TAM_FILA_AMOSTRAS 32
#endif

#define TAM_PILHA_AQUISICAO 2048
#define TAM_PILHA_COMUNICACAO 2048
#define PRIORIDADE_AQUISICAO 4
#define PRIORIDADE_COMUNICACAO 5
#define FIR_TAPS 8

struct amostra {
	uint8_t etapa;
	uint32_t seq;
	uint64_t tempo_us;
	int32_t x_mg;
	int32_t y_mg;
	int32_t z_mg;
	int32_t x_filtrado_mg;
	int32_t y_filtrado_mg;
	int32_t z_filtrado_mg;
};

static const uint16_t taxas_teste_hz[] = {50, 100, 200, 400, 800};
static const struct device *const acelerometro = DEVICE_DT_GET(NO_ACELEROMETRO);

K_MSGQ_DEFINE(fila_amostras, sizeof(struct amostra), APP_TAM_FILA_AMOSTRAS, 4);
K_SEM_DEFINE(iniciar_aquisicao, 0, 1);

static volatile int etapa_atual = -1;
static volatile uint32_t taxa_atual_hz;

static atomic_t total_produzidas;
static atomic_t total_enviadas;
static atomic_t total_perdidas;
static atomic_t total_erros;

static uint64_t tempo_us(void)
{
	/*
	 * Usa o uptime do kernel em vez do contador de ciclos.
	 * Em algumas configuracoes da FRDM-KL25Z, k_cycle_get_64() pode nao
	 * avançar como esperado para este uso e todos os pontos aparecem em t=0.
	 */
	return k_ticks_to_us_floor64(k_uptime_ticks());
}

static void imprimir_meta(int etapa, uint32_t taxa_hz)
{
	APP_PRINT("META,stage,%d,rate_hz=%u,fir=%d,logging=%d,queue=%d,taps=%d",
		  etapa,
		  taxa_hz,
		  APP_USA_FIR,
		  APP_USA_LOGGING,
		  APP_TAM_FILA_AMOSTRAS,
		  FIR_TAPS);
}

static void imprimir_estatistica(int etapa)
{
	uint32_t produzidas = (uint32_t)atomic_get(&total_produzidas);
	uint32_t enviadas = (uint32_t)atomic_get(&total_enviadas);
	uint32_t perdidas = (uint32_t)atomic_get(&total_perdidas);
	uint32_t erros = (uint32_t)atomic_get(&total_erros);
	uint32_t hz_x100 = (produzidas * 100U) / APP_TEMPO_ETAPA_S;

	APP_PRINT("STAT,%d,%u,%u,%u,%u,%u",
		  etapa,
		  produzidas,
		  enviadas,
		  perdidas,
		  erros,
		  hz_x100);
}

static void imprimir_amostra(const struct amostra *amostra)
{
	uint32_t perdidas = (uint32_t)atomic_get(&total_perdidas);

	APP_PRINT("DATA,%u,%u,%llu,%d,%d,%d,%d,%d,%d,%u",
		  amostra->etapa,
		  amostra->seq,
		  (unsigned long long)amostra->tempo_us,
		  amostra->x_mg,
		  amostra->y_mg,
		  amostra->z_mg,
		  amostra->x_filtrado_mg,
		  amostra->y_filtrado_mg,
		  amostra->z_filtrado_mg,
		  perdidas);
}

static int configurar_taxa(uint32_t taxa_hz)
{
	struct sensor_value taxa = {
		.val1 = (int32_t)taxa_hz,
		.val2 = 0,
	};

	return sensor_attr_set(acelerometro,
				       SENSOR_CHAN_ALL,
				       SENSOR_ATTR_SAMPLING_FREQUENCY,
				       &taxa);
}

static void zerar_contadores(void)
{
	atomic_set(&total_produzidas, 0);
	atomic_set(&total_enviadas, 0);
	atomic_set(&total_perdidas, 0);
	atomic_set(&total_erros, 0);
}

static void desativar_etapa(void)
{
	etapa_atual = -1;
	taxa_atual_hz = 0;
}

static void preparar_etapa(void)
{
	desativar_etapa();
	k_msgq_purge(&fila_amostras);
	zerar_contadores();
}

static void ativar_etapa(int etapa, uint32_t taxa_hz)
{
	taxa_atual_hz = taxa_hz;
	etapa_atual = etapa;
	imprimir_meta(etapa, taxa_hz);
}

#if APP_USA_FIR
struct filtro_fir {
	int32_t hist_x[FIR_TAPS];
	int32_t hist_y[FIR_TAPS];
	int32_t hist_z[FIR_TAPS];
	int64_t soma_x;
	int64_t soma_y;
	int64_t soma_z;
	uint8_t pos;
	uint8_t qtd;
};

static void zerar_filtro(struct filtro_fir *filtro)
{
	memset(filtro, 0, sizeof(*filtro));
}

static void filtrar_amostra(struct filtro_fir *filtro, struct amostra *amostra)
{
	filtro->soma_x -= filtro->hist_x[filtro->pos];
	filtro->soma_y -= filtro->hist_y[filtro->pos];
	filtro->soma_z -= filtro->hist_z[filtro->pos];

	filtro->hist_x[filtro->pos] = amostra->x_mg;
	filtro->hist_y[filtro->pos] = amostra->y_mg;
	filtro->hist_z[filtro->pos] = amostra->z_mg;

	filtro->soma_x += amostra->x_mg;
	filtro->soma_y += amostra->y_mg;
	filtro->soma_z += amostra->z_mg;

	if (filtro->qtd < FIR_TAPS) {
		filtro->qtd++;
	}

	filtro->pos = (filtro->pos + 1U) % FIR_TAPS;

	amostra->x_filtrado_mg = (int32_t)(filtro->soma_x / filtro->qtd);
	amostra->y_filtrado_mg = (int32_t)(filtro->soma_y / filtro->qtd);
	amostra->z_filtrado_mg = (int32_t)(filtro->soma_z / filtro->qtd);
}
#endif

static void ler_acelerometro(struct amostra *amostra, struct sensor_value eixos[3])
{
	amostra->x_mg = sensor_ms2_to_mg(&eixos[0]);
	amostra->y_mg = sensor_ms2_to_mg(&eixos[1]);
	amostra->z_mg = sensor_ms2_to_mg(&eixos[2]);

#if !APP_USA_FIR
	amostra->x_filtrado_mg = amostra->x_mg;
	amostra->y_filtrado_mg = amostra->y_mg;
	amostra->z_filtrado_mg = amostra->z_mg;
#endif
}

static void thread_aquisicao(void *a, void *b, void *c)
{
	struct sensor_value eixos[3];
	int ultima_etapa = -1;
	uint32_t seq = 0;
	uint64_t proxima_amostra_us = 0;

#if APP_USA_FIR
	struct filtro_fir filtro;
#endif

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	k_sem_take(&iniciar_aquisicao, K_FOREVER);

	while (1) {
		struct amostra amostra;
		uint32_t periodo_us;
		uint64_t agora_us;
		const int etapa = etapa_atual;
		const uint32_t taxa_hz = taxa_atual_hz;

		if (etapa < 0 || taxa_hz == 0) {
			k_msleep(10);
			continue;
		}

		if (etapa != ultima_etapa) {
			ultima_etapa = etapa;
			seq = 0;
			proxima_amostra_us = tempo_us();

#if APP_USA_FIR
			zerar_filtro(&filtro);
#endif
		}

		periodo_us = 1000000U / taxa_hz;
		agora_us = tempo_us();

		if (agora_us < proxima_amostra_us) {
			uint64_t espera_us = proxima_amostra_us - agora_us;

			if (espera_us > 2000U) {
				k_usleep((uint32_t)espera_us);
			} else {
				k_busy_wait((uint32_t)espera_us);
			}
		}

		agora_us = tempo_us();

		if (sensor_sample_fetch(acelerometro) < 0 ||
		    sensor_channel_get(acelerometro, SENSOR_CHAN_ACCEL_XYZ, eixos) < 0) {
			atomic_inc(&total_erros);
			proxima_amostra_us = agora_us + periodo_us;
			continue;
		}

		amostra.etapa = (uint8_t)etapa;
		amostra.seq = seq++;
		amostra.tempo_us = agora_us;

		ler_acelerometro(&amostra, eixos);

#if APP_USA_FIR
		filtrar_amostra(&filtro, &amostra);
#endif

		if (k_msgq_put(&fila_amostras, &amostra, K_NO_WAIT) != 0) {
			atomic_inc(&total_perdidas);
		}

		atomic_inc(&total_produzidas);

		if (agora_us > proxima_amostra_us + periodo_us) {
			proxima_amostra_us = agora_us + periodo_us;
		} else {
			proxima_amostra_us += periodo_us;
		}
	}
}

static void thread_comunicacao(void *a, void *b, void *c)
{
	struct amostra amostra;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (1) {
		k_msgq_get(&fila_amostras, &amostra, K_FOREVER);
		imprimir_amostra(&amostra);
		atomic_inc(&total_enviadas);
	}
}

K_THREAD_DEFINE(tid_aquisicao,
		TAM_PILHA_AQUISICAO,
		thread_aquisicao,
		NULL, NULL, NULL,
		PRIORIDADE_AQUISICAO,
		0,
		0);

K_THREAD_DEFINE(tid_comunicacao,
		TAM_PILHA_COMUNICACAO,
		thread_comunicacao,
		NULL, NULL, NULL,
		PRIORIDADE_COMUNICACAO,
		0,
		0);

int main(void)
{
	if (!device_is_ready(acelerometro)) {
		APP_PRINT("ERROR,accelerometer_not_ready");
		return 0;
	}

	APP_PRINT("META,start,board=frdm_kl25z,sensor=mma8451q,fir=%d,logging=%d",
		  APP_USA_FIR,
		  APP_USA_LOGGING);
	APP_PRINT("META,csv_header,stage,seq,t_us,x_mg,y_mg,z_mg,xf_mg,yf_mg,zf_mg,dropped");
	APP_PRINT("META,stat_header,stage,produced,sent,dropped,errors,produced_rate_hz_x100");

	k_sem_give(&iniciar_aquisicao);

	for (size_t i = 0; i < ARRAY_SIZE(taxas_teste_hz); i++) {
		const int etapa = (int)i;
		const uint32_t taxa_hz = taxas_teste_hz[i];
		int ret;

		preparar_etapa();

		ret = configurar_taxa(taxa_hz);
		if (ret != 0) {
			APP_PRINT("ERROR,stage,%u,rate_hz=%u,configure_ret=%d",
				  (unsigned int)i,
				  taxa_hz,
				  ret);
			continue;
		}

		k_msleep(100);
		ativar_etapa(etapa, taxa_hz);
		k_sleep(K_SECONDS(APP_TEMPO_ETAPA_S));
		desativar_etapa();

		/* Tempo para a thread de comunicacao esvaziar a fila antes do STAT. */
		k_sleep(K_MSEC(1000));
		imprimir_estatistica(etapa);
		k_sleep(K_MSEC(250));
	}

	APP_PRINT("META,done");

	while (1) {
		k_sleep(K_SECONDS(1));
	}
}
