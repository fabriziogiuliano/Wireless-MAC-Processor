#ifndef QUEUE_H
#define QUEUE_H

#include <pthread.h>

struct metamac_queue {
	pthread_mutex_t pop_mutex;
	pthread_cond_t nonempty_cond;
	struct metamac_slot *data;
	size_t capacity;
	size_t in;
	size_t out;
};

#include "metamac.h"

void queue_init(struct metamac_queue *queue, size_t capacity);
void queue_destroy(struct metamac_queue *queue);

void queue_push(struct metamac_queue *queue, struct metamac_slot *slot);
void queue_multipush(struct metamac_queue *queue, struct metamac_slot *slots, size_t count);
size_t queue_multipop(struct metamac_queue *queue, struct metamac_slot *slots, size_t count);

#endif