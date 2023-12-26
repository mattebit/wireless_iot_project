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

uint8_t sent_beacon_count = 0;
int listen_count = 0;
uint16_t epoch = -1;
int discovered_n_epoch = 0;
int epoch_start = 0;

bool neighbors[MAX_NBR]; // assume ids are from 0 to MAX_NBR

#define TRANSMISSION_WINDOW_COUNT 1
#define RECEPTION_WINDOW_COUNT 5
#define EPOCH_DURATION EPOCH_INTERVAL_RT

#define NUM_EPOCH_EACH_WRAP EPOCH_DURATION / USHRT_MAX // short unsigned dimension

// both transmission and reception have the same window dimension
#define WINDOW_LEN EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT + RECEPTION_WINDOW_COUNT)

#define TRANSMISSION_WINDOW_DURATION WINDOW_LEN
#define RECEPTION_WINDOW_DURATION WINDOW_LEN

#define TICKS_PER_SEC RTIMER_SECOND // number of ticks in one second
#define TICKS_PER_MILLISEC TICKS_PER_SEC / 1000

#define TRANSMISSION_DURATION 400   // [ticks]
#define RECEPTION_DURATION RECEPTION_WINDOW_DURATION / 3 // [ticks]

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
  sent_beacon_count++;

  if (sent_beacon_count != TRANSMISSION_PER_WINDOW)
  {
    static struct rtimer beacon_timer;
    rtimer_set(
        &beacon_timer,
        epoch_start + (sent_beacon_count * TRANSMISSION_DURATION), // set next beacon wrt to epoch start
        NULL,
        nd_send_beacon,
        NULL);
  }
  else
  {
    // start receive
    sent_beacon_count = 0;

    // call nd_listen when recv window starts
    static struct rtimer start_listening;
    rtimer_set(
        &start_listening,
        // wait the remainig time and then listen again
        epoch_start + TRANSMISSION_WINDOW_DURATION,
        NULL,
        nd_listen,
        NULL);
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
        epoch_start + (RECEPTION_WINDOW_DURATION * (listen_count + 1)),
        NULL,
        nd_listen,
        NULL);
  }
  else
  {
    app_cb.nd_epoch_end(epoch, discovered_n_epoch);
    nd_step();
  }
}

void nd_listen(void)
{
  NETSTACK_RADIO.on(); // start listening

  // set callback to stop listening
  static struct rtimer stop_listening;
  rtimer_set(
      &stop_listening,
      epoch_start + (RECEPTION_WINDOW_DURATION * (listen_count + 1)) + (RECEPTION_DURATION),
      NULL,
      nd_stop_listen,
      NULL);
}

void nd_step()
{
  listen_count = 0;
  discovered_n_epoch = 0;
  epoch++;
  epoch_start = rtimer_arch_now();
  nd_send_beacon();
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
    nd_step();
    break;
  }
  case ND_SCATTER:
  {

    break;
  }
  }
}
/*---------------------------------------------------------------------------*/
