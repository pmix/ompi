/*
 * Copyright (c) 2012-2020 Cisco Systems, Inc.  All rights reserved
 * Copyright (c) 2012      Los Alamos National Security, LLC. All rights reserved
 * Copyright (c) 2015-2020 Intel, Inc.  All rights reserved.
 * Copyright (c) 2021      Nanook Consulting.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "prte_config.h"

#include <stddef.h>
#include <stdio.h>

#include "constants.h"
#include "src/class/prte_hotel.h"
#include "src/event/event-internal.h"

static void local_eviction_callback(int fd, short flags, void *arg)
{
    prte_hotel_room_eviction_callback_arg_t *eargs;
    void *occupant;

    eargs = (prte_hotel_room_eviction_callback_arg_t *) arg;
    occupant = eargs->hotel->rooms[eargs->room_num].occupant;

    /* Remove the occupant from the room.

       Do not change this logic without also changing the same logic
       in prte_hotel_checkout() and
       prte_hotel_checkout_and_return_occupant(). */
    prte_hotel_t *hotel = eargs->hotel;
    prte_hotel_room_t *room = &(hotel->rooms[eargs->room_num]);
    room->occupant = NULL;

    /* Invoke the user callback to tell them that they were evicted */
    hotel->evict_callback_fn(hotel, eargs->room_num, occupant);
}

int prte_hotel_init(prte_hotel_t *h, int num_rooms, prte_event_base_t *evbase,
                    uint32_t eviction_timeout, int eviction_event_priority,
                    prte_hotel_eviction_callback_fn_t evict_callback_fn)
{
    int i;

    /* Bozo check */
    if (num_rooms <= 0 || NULL == evict_callback_fn) {
        return PRTE_ERR_BAD_PARAM;
    }

    h->num_rooms = num_rooms;
    h->evbase = evbase;
    h->eviction_timeout.tv_usec = 0;
    h->eviction_timeout.tv_sec = eviction_timeout;
    h->evict_callback_fn = evict_callback_fn;
    h->rooms = (prte_hotel_room_t *) malloc(num_rooms * sizeof(prte_hotel_room_t));
    if (NULL != evict_callback_fn) {
        h->eviction_args = (prte_hotel_room_eviction_callback_arg_t *) malloc(
            num_rooms * sizeof(prte_hotel_room_eviction_callback_arg_t));
    }
    h->last_unoccupied_room = num_rooms - 1;

    for (i = 0; i < num_rooms; ++i) {
        /* Mark this room as unoccupied */
        h->rooms[i].occupant = NULL;

        /* Setup the eviction callback args */
        h->eviction_args[i].hotel = h;
        h->eviction_args[i].room_num = i;

        /* Create this room's event (but don't add it) */
        if (NULL != h->evbase) {
            prte_event_set(h->evbase, &(h->rooms[i].eviction_timer_event), -1, 0,
                           local_eviction_callback, &(h->eviction_args[i]));

            /* Set the priority so it gets serviced properly */
            prte_event_set_priority(&(h->rooms[i].eviction_timer_event), eviction_event_priority);
        }
    }

    return PRTE_SUCCESS;
}

static void constructor(prte_hotel_t *h)
{
    h->num_rooms = 0;
    h->evbase = NULL;
    h->eviction_timeout.tv_sec = 0;
    h->eviction_timeout.tv_usec = 0;
    h->evict_callback_fn = NULL;
    h->rooms = NULL;
    h->eviction_args = NULL;
    h->last_unoccupied_room = -1;
}

static void destructor(prte_hotel_t *h)
{
    int i;

    /* Go through all occupied rooms and destroy their events */
    if (NULL != h->evbase) {
        for (i = 0; i < h->num_rooms; ++i) {
            if (NULL != h->rooms[i].occupant) {
                prte_event_del(&(h->rooms[i].eviction_timer_event));
            }
        }
    }

    if (NULL != h->rooms) {
        free(h->rooms);
    }
    if (NULL != h->eviction_args) {
        free(h->eviction_args);
    }
}

PRTE_CLASS_INSTANCE(prte_hotel_t, prte_object_t, constructor, destructor);
