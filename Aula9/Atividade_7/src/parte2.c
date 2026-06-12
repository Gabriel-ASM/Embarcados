#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define STACK_SIZE 1024
#define PRIORIDADE 5

static int saldo_vitrine;

K_MUTEX_DEFINE(mutex_vitrine);

static void padeiro(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msleep(1000);

		k_mutex_lock(&mutex_vitrine, K_FOREVER);

		saldo_vitrine++;
		printk("[t=%u ms] Padeiro produziu | Saldo: %d\n",
		       k_uptime_get_32(), saldo_vitrine);

		k_mutex_unlock(&mutex_vitrine);
	}
}

static void cliente(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (1) {
		k_msleep(1500);

		k_mutex_lock(&mutex_vitrine, K_FOREVER);

		if (saldo_vitrine > 0) {
			saldo_vitrine--;
			printk("[t=%u ms] Cliente retirou  | Saldo: %d\n",
			       k_uptime_get_32(), saldo_vitrine);
		} else {
			printk("[t=%u ms] Vitrine vazia    | Saldo: %d\n",
			       k_uptime_get_32(), saldo_vitrine);
		}

		k_mutex_unlock(&mutex_vitrine);
	}
}

K_THREAD_DEFINE(padeiro_id, STACK_SIZE, padeiro,
		0, 0, 0, PRIORIDADE, 0, 0);

K_THREAD_DEFINE(cliente_id, STACK_SIZE, cliente,
		0, 0, 0, PRIORIDADE, 0, 0);

int main(void)
{
	printk("[t=%u ms] Simulacao com mutex iniciada\n",
	       k_uptime_get_32());
	return 0;
}
