/*---------------------------------------------------------------------------*/
#include "contiki.h"
#include "lib/random.h"
#include "sys/rtimer.h"
#include "dev/radio.h"
#include "net/netstack.h"
#include "net/packetbuf.h"
#include "node-id.h"
/*---------------------------------------------------------------------------*/
#include <stdbool.h>
#include <stdio.h>
/*---------------------------------------------------------------------------*/
#include "nd.h"
/*---------------------------------------------------------------------------*/
#define DEBUG 0
#if DEBUG
#define PRINTF(...) printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif
/*---------------------------------------------------------------------------*/
struct nd_callbacks app_cb = {
    .nd_new_nbr = NULL,
    .nd_epoch_end = NULL};

struct beacon_msg
{
    uint16_t epoch;
    uint8_t id;
} __attribute__((packed));

uint8_t beacons_count = 0;
int listen_count = 0;
uint16_t epoch = 0;
int discovered_n_epoch = 0;

bool neighbors[MAX_NBR]; // assume ids are from 0 to MAX_NBR

#define TRANSMISSION_WINDOW_COUNT 1
#define RECEPTION_WINDOW_COUNT 2
#define EPOCH_DURATION EPOCH_INTERVAL_RT

#define WINDOW_LEN EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT + RECEPTION_WINDOW_COUNT)

#define TRANSMISSION_WINDOW_DURATION WINDOW_LEN
#define RECEPTION_WINDOW_DURATION WINDOW_LEN

#define TICKS_PER_SEC RTIMER_SECOND // number of ticks in one second

#define TRANSMISSION_DURATION 500                        // TODO define better
#define RECEPTION_DURATION RECEPTION_WINDOW_DURATION / 3 // TODO define better

#define TRANSMISSION_PER_WINDOW TRANSMISSION_WINDOW_DURATION / TRANSMISSION_DURATION

/*---------------------------------------------------------------------------*/

void nd_recv(void)
{
    /* New packet received
     * 1. Read packet from packetbuf---packetbuf_dataptr()
     * 2. If a new neighbor is discovered within the epoch, notify the application
     * NOTE: The testbed's firefly nodes can receive packets only if they are at
     * least 3 bytes long (5 considering the CRC).
     * If while you are testing you receive nothing make sure your packet is long enough
     */

    unsigned short nbr_id;
    memcpy(&nbr_id, packetbuf_dataptr(), sizeof(unsigned short));

    if (nbr_id < MAX_NBR)
    {
        if (!neighbors[nbr_id])
        {
            neighbors[nbr_id] = true;
            discovered_n_epoch++;

            if (app_cb.nd_new_nbr != NULL)
            {
                app_cb.nd_new_nbr(epoch, nbr_id);
            }
            else
            {
                printf("app_cb.nd_new_nbr is NULL\n");
            }
        }
    }

    // printf("received id: %d\n", nbr_id);
}

void nd_send_beacon(void)
{
    // sends a beacon

    NETSTACK_RADIO.on();
    if (!NETSTACK_RADIO.receiving_packet())
    {
        NETSTACK_RADIO.send(&node_id, sizeof(unsigned short));
    }
    NETSTACK_RADIO.off();
    beacons_count++;

    if (beacons_count != TRANSMISSION_PER_WINDOW)
    {
        static struct rtimer beacon_timer;
        rtimer_set(
            &beacon_timer,
            rtimer_arch_now() + TRANSMISSION_DURATION,
            NULL,
            nd_send_beacon,
            NULL);
    }
    else
    {
        beacons_count = 0;
        nd_listen();
        // start receive
    }
}

void nd_stop_listen(void)
{
    NETSTACK_RADIO.off();
    listen_count++;

    if (listen_count < RECEPTION_WINDOW_COUNT)
    {
        static struct rtimer start_listening;
        rtimer_set(
            &start_listening,
            // wait the remainig time and then listen again
            rtimer_arch_now() + (RECEPTION_WINDOW_DURATION - RECEPTION_DURATION),
            NULL,
            nd_listen,
            NULL);
    }
    else
    {
        app_cb.nd_epoch_end(epoch, discovered_n_epoch);
        static struct rtimer beacon_timer;
        rtimer_set(
            &beacon_timer,
            // wait the remainig time and then send beacon
            rtimer_arch_now() + (RECEPTION_WINDOW_DURATION - RECEPTION_DURATION),
            NULL,
            nd_send_beacon,
            NULL);
        listen_count = 0;
        discovered_n_epoch = 0;
        epoch++;
    }
}

void nd_listen(void)
{
    NETSTACK_RADIO.on(); // start listening

    // set callback to stop listening
    static struct rtimer stop_listening;
    rtimer_set(
        &stop_listening,
        rtimer_arch_now() + RECEPTION_DURATION,
        NULL,
        nd_stop_listen,
        NULL);
}

/*---------------------------------------------------------------------------*/
void nd_start(uint8_t mode, const struct nd_callbacks *cb)
{
    /* Start seleced ND primitive and set nd_callbacks */
    // memcpy(&app_cb, cb, sizeof(struct nd_callbacks));

    app_cb.nd_new_nbr = cb->nd_new_nbr;
    app_cb.nd_epoch_end = cb->nd_epoch_end;

    switch (mode)
    {
    case ND_BURST:
    {
        nd_send_beacon();
        break;
    }
    case ND_SCATTER:
    {

        break;
    }
    }
}
/*---------------------------------------------------------------------------*/
