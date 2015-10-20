#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>
#include <signal.h>
#include <pthread.h>
#include <unistd.h>

#include <time.h>

#include "libb43.h"
#include "dataParser.h"

typedef unsigned int uint;

struct slot {
	uint64_t host_start;
	uint64_t host_finish;
	uint64_t tsf_counter;
	uint slot_count;
	uint packet_queued;
	uint transmitted;
	uint transmit_success;
	uint transmit_other;
};

struct queue {
	pthread_mutex_t pop_mutex;
	pthread_cond_t nonempty_cond;
	struct slot *data;
	size_t capacity;
	size_t in;
	size_t out;
};

static void queue_init(struct queue *queue, size_t capacity)
{
	if (capacity <= 0) {
		errx(EXIT_FAILURE, "Invalid capacity value: %lu.\n", capacity);
	}

	queue->data = malloc(sizeof(struct slot) * capacity);
	if (!queue->data) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	pthread_mutex_init(&queue->pop_mutex, NULL);
	pthread_cond_init(&queue->nonempty_cond, NULL);

	queue->capacity = capacity;
	queue->in = 0;
	queue->out = 0;
}

static void queue_destroy(struct queue *queue)
{
	free(queue->data);
	pthread_mutex_destroy(&queue->pop_mutex);
	pthread_cond_destroy(&queue->nonempty_cond);
}

static void queue_resize(struct queue *queue, size_t newcap)
{
	if (pthread_mutex_lock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error locking mutex");
	}

	struct slot *newdata = malloc(sizeof(struct slot) * newcap);
	if (!newdata) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	if (queue->in >= queue->out) {
		/* Queue is not wrapped. */
		memcpy(newdata, queue->data + queue->out,
			(queue->in - queue->out) * sizeof(struct slot));
		queue->in -= queue->out;
		queue->out = 0;
	} else {
		/* Queue is wrapped. */
		memcpy(newdata, queue->data + queue->out,
			(queue->capacity - queue->out) * sizeof(struct slot));
		memcpy(newdata + (queue->capacity - queue->out), queue->data,
			queue->in * sizeof(struct slot));
		queue->in += queue->capacity - queue->out;
		queue->out = 0;
	}

	struct slot *olddata = queue->data;
	queue->data = newdata;
	queue->capacity = newcap;
	free(olddata);

	if (pthread_mutex_unlock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error unlocking mutex");
	}
}

static void queue_multipush(struct queue *queue, struct slot *slots, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (queue->in == (queue->out + queue->capacity - 1) % queue->capacity) {
			queue_resize(queue, queue->capacity * 2);
		}

		queue->data[queue->in] = slots[i];
		queue->in = (queue->in + 1) % queue->capacity;
	}

	if (pthread_cond_broadcast(&queue->nonempty_cond) != 0) {
		err(EXIT_FAILURE, "Error signaling condition variable");
	}
}

static void queue_push(struct queue *queue, struct slot *slot)
{
	queue_multipush(queue, slot, 1);
}

static size_t queue_multipop(struct queue *queue, struct slot *slots, size_t count)
{
	if (pthread_mutex_lock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error locking mutex");
	}

	if (queue->out == queue->in) {
		pthread_cond_wait(&queue->nonempty_cond, &queue->pop_mutex);
	}

	size_t i = 0;
	while (i < count && queue->out != queue->in) {
		slots[i++] = queue->data[queue->out];
		queue->out = (queue->out + 1) % queue->capacity;
	}

	if (pthread_mutex_unlock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error unlocking mutex");
	}

	return i;
}

static void queue_signal(struct queue *queue)
{
	if (pthread_cond_broadcast(&queue->nonempty_cond) != 0) {
		err(EXIT_FAILURE, "Error signaling condition variable");
	}
}

static volatile int break_loop = 0;

static void read_loop(struct queue *queue, struct debugfs_file *df)
{
	struct timespec starttime;
	clock_gettime(CLOCK_MONOTONIC_RAW, &starttime);

	while (break_loop == 0) {
		struct timespec currenttime;
		clock_gettime(CLOCK_MONOTONIC_RAW, &currenttime);
		uint64_t host_start = (currenttime.tv_sec - starttime.tv_sec) * 1000000 +
			(currenttime.tv_nsec - starttime.tv_nsec) / 1000;
		
		uint64_t tsf_counter;
		getTSFRegs(df, &tsf_counter);
		int slot_count = shmRead16(df, B43_SHM_REGS, COUNT_SLOT);
		int packet_queued = shmRead16(df, B43_SHM_SHARED, PACKET_TO_TRANSMIT);
		int transmitted = shmRead16(df, B43_SHM_SHARED, MY_TRANSMISSION);
		int transmit_success = shmRead16(df, B43_SHM_SHARED, SUCCES_TRANSMISSION);
		int transmit_other = shmRead16(df, B43_SHM_SHARED, OTHER_TRANSMISSION);

		clock_gettime(CLOCK_MONOTONIC_RAW, &currenttime);
		uint64_t host_finish = (currenttime.tv_sec - starttime.tv_sec) * 1000000 +
			(currenttime.tv_nsec - starttime.tv_nsec) / 1000;

		struct slot current_slot = {
			.host_start = host_start,
			.host_finish = host_finish,
			.tsf_counter = tsf_counter,
			.slot_count = slot_count,
			.packet_queued = packet_queued,
			.transmitted = transmitted,
			.transmit_success = transmit_success,
			.transmit_other = transmit_other
		};

		queue_push(queue, &current_slot);
	}

	fprintf(stderr, "Exiting read loop.");
	usleep(10000);
	queue_signal(queue);
}

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))

static void process_loop(struct queue *queue, struct debugfs_file *df)
{
	printf("host_start,host_finish,tsf_counter,slot_count,packet_queued,transmitted,transmit_success,transmit_other\n");

	while (break_loop == 0) {
		struct slot slots[10];
		size_t count = queue_multipop(queue, slots, ARRAY_SIZE(slots));

		for (size_t i = 0; i < count; i++) {
			printf("%llu,%llu,%llu,%d,%x,%x,%x,%x\n",
				(long long)slots[i].host_start,
				(long long)slots[i].host_finish,
				(long long)slots[i].tsf_counter,
				slots[i].slot_count,
				slots[i].packet_queued,
				slots[i].transmitted,
				slots[i].transmit_success,
				slots[i].transmit_other);
		}
	}

	fprintf(stderr, "Exiting process loop.");
}

static void sigint_handler(int signum, siginfo_t *info, void *ptr)
{
	if (signum == SIGINT) {
		fprintf(stderr, "Received SIGINT, exiting...");
		break_loop = SIGINT;
	}
}

static struct sigaction sigact;
static struct queue queue;
static struct debugfs_file df;

static void *run_read_loop(void *unused)
{
	struct sched_param param = { .sched_priority = 98 };
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
	read_loop(&queue, &df);
	return NULL;
}

int main(int argc, char *argv[])
{
	/* Set signal handler for interrupt signal. */
	memset(&sigact, 0, sizeof(sigact));
	sigact.sa_sigaction = sigint_handler;
	sigact.sa_flags = SA_SIGINFO;
	sigaction(SIGINT, &sigact, NULL);

	queue_init(&queue, 1024);
	init_file(&df);

	pthread_t reader;
	pthread_create(&reader, NULL, run_read_loop, NULL);
	process_loop(&queue, &df);
	pthread_join(reader, NULL);

	queue_destroy(&queue);
	close_file(&df);

	return 0;
}