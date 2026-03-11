/*
 * ping_thread.c - Background ICMP ping for RTT measurement
 *
 * Spawns `ping -c1 -W1 <target>` every second in a dedicated thread.
 * The last measured RTT is available via ping_get_rtt().
 */
#include "wavemon.h"
#include <pthread.h>

static pthread_t ping_tid;
static pthread_mutex_t ping_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool ping_running;
static char ping_target[64];

static struct {
	float	rtt_ms;
	bool	valid;
} ping_result;

static void *ping_loop(void *arg)
{
	(void)arg;
	sigset_t blockmask;

	sigemptyset(&blockmask);
	sigaddset(&blockmask, SIGWINCH);
	pthread_sigmask(SIG_BLOCK, &blockmask, NULL);

	while (ping_running) {
		char cmd[128];
		FILE *fp;
		float rtt = -1;

		snprintf(cmd, sizeof(cmd),
			 "ping -c1 -W1 %s 2>/dev/null", ping_target);
		fp = popen(cmd, "r");
		if (fp) {
			char line[256];

			while (fgets(line, sizeof(line), fp)) {
				char *p = strstr(line, "time=");

				if (p && sscanf(p + 5, "%f", &rtt) == 1)
					break;
			}
			pclose(fp);
		}

		pthread_mutex_lock(&ping_mutex);
		if (rtt >= 0) {
			ping_result.rtt_ms = rtt;
			ping_result.valid = true;
		}
		pthread_mutex_unlock(&ping_mutex);

		usleep(1000000);
	}
	return NULL;
}

void ping_start(const char *target)
{
	snprintf(ping_target, sizeof(ping_target), "%s", target);
	ping_running = true;
	ping_result.valid = false;
	pthread_create(&ping_tid, NULL, ping_loop, NULL);
}

void ping_stop(void)
{
	ping_running = false;
	pthread_join(ping_tid, NULL);
}

bool ping_get_rtt(float *rtt_ms)
{
	bool valid;

	pthread_mutex_lock(&ping_mutex);
	valid = ping_result.valid;
	if (valid)
		*rtt_ms = ping_result.rtt_ms;
	pthread_mutex_unlock(&ping_mutex);
	return valid;
}
