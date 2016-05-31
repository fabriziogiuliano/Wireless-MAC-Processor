#include <stdlib.h>
#include <err.h>
#include <stdbool.h>
#include <string.h>

#include "queue.h"
#include "metamac.h"

void queue_init(struct metamac_queue *queue, size_t capacity)
{
	if (capacity <= 0) {
		errx(EXIT_FAILURE, "Invalid capacity value: %zu.\n", capacity);
	}

	queue->data = malloc(sizeof(struct metamac_slot) * capacity);
	if (!queue->data) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	pthread_mutex_init(&queue->pop_mutex, NULL);
	pthread_cond_init(&queue->nonempty_cond, NULL);

	queue->capacity = capacity;
	queue->in = 0;
	queue->out = 0;
}

void queue_destroy(struct metamac_queue *queue)
{
	free(queue->data);
	pthread_mutex_destroy(&queue->pop_mutex);
	pthread_cond_destroy(&queue->nonempty_cond);
}

static void queue_resize(struct metamac_queue *queue, size_t newcap)
{
	if (pthread_mutex_lock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error locking mutex");
	}

	struct metamac_slot *newdata = malloc(sizeof(struct metamac_slot) * newcap);
	if (!newdata) {
		err(EXIT_FAILURE, "Unable to allocate memory");
	}

	if (queue->in >= queue->out) {
		/* Queue is not wrapped. */
		memcpy(newdata, queue->data + queue->out,
			(queue->in - queue->out) * sizeof(struct metamac_slot));
		queue->in -= queue->out;
		queue->out = 0;
	} else {
		/* Queue is wrapped. */
		memcpy(newdata, queue->data + queue->out,
			(queue->capacity - queue->out) * sizeof(struct metamac_slot));
		memcpy(newdata + (queue->capacity - queue->out), queue->data,
			queue->in * sizeof(struct metamac_slot));
		queue->in += queue->capacity - queue->out;
		queue->out = 0;
	}

	struct metamac_slot *olddata = queue->data;
	queue->data = newdata;
	queue->capacity = newcap;
	free(olddata);

	if (pthread_mutex_unlock(&queue->pop_mutex) != 0) {
		err(EXIT_FAILURE, "Error unlocking mutex");
	}
}

void queue_push(struct metamac_queue *queue, struct metamac_slot *slot)
{
	queue_multipush(queue, slot, 1);
}

void queue_multipush(struct metamac_queue *queue, struct metamac_slot *slots, size_t count)
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

size_t queue_multipop(struct metamac_queue *queue, struct metamac_slot *slots, size_t count)
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

void queue_signal(struct metamac_queue *queue)
{
	if (pthread_cond_broadcast(&queue->nonempty_cond) != 0) {
		err(EXIT_FAILURE, "Error signaling condition variable");
	}
}
