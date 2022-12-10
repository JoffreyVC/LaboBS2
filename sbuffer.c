/**
 * \author Mathieu Erbas
 */

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
    #include <bits/pthreadtypes.h>
#endif
#ifndef __USE_UNIX98
    #define __USE_UNIX98
#endif
#ifndef __USE_XOPEN2K
    #define __USE_XOPEN2K
#endif

#include "sbuffer.h"
#include "config.h"

#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

// ^ Element in de Buffer
typedef struct sbuffer_node {
    struct sbuffer_node* prev;   // pointer naar previous?
    sensor_data_t data;          // data in een node van de buffer
} sbuffer_node_t;

// ^ De Buffer
struct sbuffer {                 // buffer gemaakt van nodes
    sbuffer_node_t* head;
    sbuffer_node_t* tail;
    bool closed;
    pthread_rwlock_t rw_lock;
    // // pthread_mutex_t mutex;  
};

static sbuffer_node_t* create_node(const sensor_data_t* data) {     // maak een node voor in de buffer
    sbuffer_node_t* node = malloc(sizeof(*node));
    *node = (sbuffer_node_t){
        .data = *data,
        .prev = NULL,
    };
    return node;
}

sbuffer_t* sbuffer_create() {       // initialiseer attributen en geheugen
    sbuffer_t* buffer = malloc(sizeof(sbuffer_t));
    // should never fail due to optimistic memory allocation
    assert(buffer != NULL);

    buffer->head = NULL;
    buffer->tail = NULL;
    buffer->closed = false;
    ASSERT_ELSE_PERROR(pthread_rwlock_init(&buffer->rw_lock, NULL) == 0);
    // // ASSERT_ELSE_PERROR(pthread_mutex_init(&buffer->mutex, NULL) == 0);

    return buffer;
}

void sbuffer_destroy(sbuffer_t* buffer) {       // vrijgeven van de buffer
    assert(buffer);
    // make sure it's empty
    assert(buffer->head == buffer->tail);
    // // ASSERT_ELSE_PERROR(pthread_mutex_destroy(&buffer->mutex) == 0);
    free(buffer);
}

void sbuffer_lock(sbuffer_t* buffer) {      // lock de buffer
    assert(buffer);
    // // ASSERT_ELSE_PERROR(pthread_mutex_lock(&buffer->mutex) == 0);
}
void sbuffer_unlock(sbuffer_t* buffer) {        // unlock de buffer
    assert(buffer);
    // // ASSERT_ELSE_PERROR(pthread_mutex_unlock(&buffer->mutex) == 0);
}

bool sbuffer_is_empty(sbuffer_t* buffer) {      // is de buffer leeg?
    assert(buffer);
    return buffer->head == NULL;
}

bool sbuffer_is_closed(sbuffer_t* buffer) {     // is de buffer closed?
    assert(buffer);
    return buffer->closed;
}

int sbuffer_insert_first(sbuffer_t* buffer, sensor_data_t const* data) {        // insert aan het begin van de buffer
    assert(buffer && data);
    if (buffer->closed)
        return SBUFFER_FAILURE;  // kan niet als hij toe is

    // create new node
    sbuffer_node_t* node = create_node(data);
    assert(node->prev == NULL);

    // insert it
    if (buffer->head != NULL)
        buffer->head->prev = node;
    buffer->head = node;
    if (buffer->tail == NULL)
        buffer->tail = node;

    return SBUFFER_SUCCESS;
}

sensor_data_t sbuffer_remove_last(sbuffer_t* buffer) {      // geef laatste van buffer terug en verwijder
    assert(buffer);
    assert(buffer->head != NULL);

    sbuffer_node_t* removed_node = buffer->tail;
    assert(removed_node != NULL);
    if (removed_node == buffer->head) {
        buffer->head = NULL;
        assert(removed_node == buffer->tail);
    }
    buffer->tail = removed_node->prev;
    sensor_data_t ret = removed_node->data;
    free(removed_node);

    return ret;
}

void sbuffer_close(sbuffer_t* buffer) {     // close de buffer
    assert(buffer);
    buffer->closed = true;
}
