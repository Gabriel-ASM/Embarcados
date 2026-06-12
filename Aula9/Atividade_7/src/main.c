#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE 2048
#define PRIORIDADE 5
#define CAPACIDADE 10

static int saldo_vitrine;

K_MUTEX_DEFINE(mutex_vitrine);
K_SEM_DEFINE(paes, 0, CAPACIDADE);
K_SEM_DEFINE(espacos, CAPACIDADE, CAPACIDADE);

static void padeiro(void *arg1, void *arg2, void *arg3)
{
	int ret;
	int saldo_atual;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msleep(1000);

		ret = k_sem_take(&espacos, K_FOREVER);
		if (ret != 0) {
			printk("[ERRO] k_sem_take(espacos): %d\n", ret);
			continue;
		}

		ret = k_mutex_lock(&mutex_vitrine, K_FOREVER);
		if (ret != 0) {
			printk("[ERRO] k_mutex_lock: %d\n", ret);
			k_sem_give(&espacos);
			continue;
		}

		saldo_vitrine++;
		saldo_atual = saldo_vitrine;

		k_mutex_unlock(&mutex_vitrine);
		k_sem_give(&paes);

		printk("[t=%u ms] Padeiro produziu | Saldo: %d\n",
		       k_uptime_get_32(), saldo_atual);
	}
}

static void cliente(void *arg1, void *arg2, void *arg3)
{
	int ret;
	int saldo_atual;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msleep(1500);

		ret = k_sem_take(&paes, K_FOREVER);
		if (ret != 0) {
			printk("[ERRO] k_sem_take(paes): %d\n", ret);
			continue;
		}

		ret = k_mutex_lock(&mutex_vitrine, K_FOREVER);
		if (ret != 0) {
			printk("[ERRO] k_mutex_lock: %d\n", ret);
			k_sem_give(&paes);
			continue;
		}

		saldo_vitrine--;
		saldo_atual = saldo_vitrine;

		k_mutex_unlock(&mutex_vitrine);
		k_sem_give(&espacos);

		printk("[t=%u ms] Cliente retirou  | Saldo: %d\n",
		       k_uptime_get_32(), saldo_atual);
	}
}

K_THREAD_DEFINE(padeiro_id, STACK_SIZE, padeiro,
		0, 0, 0, PRIORIDADE, 0, 0);

K_THREAD_DEFINE(cliente_id, STACK_SIZE, cliente,
		0, 0, 0, PRIORIDADE, 0, 0);

int main(void)
{
	printk("[t=%u ms] Simulacao com semaforos iniciada | Saldo: %d\n",
	       k_uptime_get_32(), saldo_vitrine);
	return 0;
}
