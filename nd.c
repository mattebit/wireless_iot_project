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

int TRANSMISSION_PER_WINDOW = 0;
int TRANSMISSION_WINDOW_COUNT = 0;
int RECEPTION_WINDOW_COUNT = 0;
int WINDOW_LEN = 0;
int TRANSMISSION_DURATION = 0;
int RECEPTION_DURATION = 0;
int TRANSMISSION_WINDOW_DURATION = 0;
int RECEPTION_WINDOW_DURATION = 0;
bool FIRST_TRANSMIT = false;

uint8_t sent_beacon_count = 0;
int listen_count = 0;
uint16_t epoch = -1;
int discovered_n_epoch = 0;
int epoch_start = 0;
int transmit_window_count = 0;

bool neighbors[MAX_NBR]; // assume ids are from 0 to MAX_NBR

#define EPOCH_DURATION EPOCH_INTERVAL_RT
#define NUM_EPOCH_EACH_WRAP EPOCH_DURATION / USHRT_MAX // short unsigned dimension
#define TICKS_PER_SEC RTIMER_SECOND                    // number of ticks in one second
#define TICKS_PER_MILLISEC TICKS_PER_SEC / 1000

#define TRANSMISSION_WINDOW_COUNT_BURST 1
#define RECEPTION_WINDOW_COUNT_BURST 5
#define TRANSMISSION_WINDOW_COUNT_SCATTER 5
#define RECEPTION_WINDOW_COUNT_SCATTER 1

// both transmission and reception have the same window dimension
#define WINDOW_LEN_BURST EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT_BURST + RECEPTION_WINDOW_COUNT_BURST)
#define WINDOW_LEN_SCATTER EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT_SCATTER + RECEPTION_WINDOW_COUNT_SCATTER)

#define TRANSMISSION_WINDOW_DURATION_BURST WINDOW_LEN_BURST
#define TRANSMISSION_WINDOW_DURATION_SCATTER WINDOW_LEN_SCATTER

#define RECEPTION_WINDOW_DURATION_BURST WINDOW_LEN_BURST
#define RECEPTION_WINDOW_DURATION_SCATTER WINDOW_LEN_SCATTER

#define TRANSMISSION_DURATION_BURST 400                                   // [ticks]
#define RECEPTION_DURATION_BURST RECEPTION_WINDOW_DURATION_BURST / 3      // [ticks]
#define TRANSMISSION_DURATION_SCATTER 200                                 // [ticks]
#define RECEPTION_DURATION_SCATTER RECEPTION_WINDOW_DURATION_SCATTER - 10 // [ticks]

#define TRANSMISSION_PER_WINDOW_BURST TRANSMISSION_WINDOW_DURATION_BURST / TRANSMISSION_DURATION_BURST
#define TRANSMISSION_PER_WINDOW_SCATTER 1

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
        printf("app_cb.nd_new_nbr is NULL\n"); // TODO: fix
      }
    }
  }

  // printf("received id: %d\n", nbr_id);
}

/*
 * Sends a beacon
 */
void nd_send_beacon(void)
{
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
    transmit_window_count += 1;
    sent_beacon_count = 0;
    if (transmit_window_count < TRANSMISSION_WINDOW_COUNT)
    {
      // do another transmission window
      static struct rtimer beacon_timer;
      rtimer_set(
          &beacon_timer,
          epoch_start + (TRANSMISSION_WINDOW_DURATION * (transmit_window_count + 1)),
          NULL,
          nd_send_beacon,
          NULL);
    }
    else
    {
      transmit_window_count = 0;
      // if it transmit before receiving in an epoch, go to listen window
      // otherwise end epoch

      if (FIRST_TRANSMIT)
      {
        // do a reception window
        // call nd_listen when recv window starts
        static struct rtimer start_listening;
        rtimer_set(
            &start_listening,
            // wait the remainig time and then listen again
            epoch_start + (TRANSMISSION_WINDOW_DURATION * TRANSMISSION_WINDOW_COUNT),
            NULL,
            nd_listen,
            NULL);
      }
      else
      {
        static struct rtimer epoch_end;
        rtimer_set(
            &epoch_end,
            // wait the remainig time and then call epoch end
            epoch_start + EPOCH_DURATION,
            NULL,
            nd_step,
            NULL);
      }
    }
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
        epoch_start + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? listen_count + 1 : listen_count)),
        NULL,
        nd_listen,
        NULL);
  }
  else
  {
    listen_count = 0;
    // if first transmit in epoch, then after listen end epoch, otherwise
    // go to transmit
    if (FIRST_TRANSMIT)
    {
      static struct rtimer epoch_end;
      rtimer_set(
          &epoch_end,
          // wait the remainig time and then call epoch end
          epoch_start + EPOCH_DURATION,
          NULL,
          nd_step,
          NULL);
    }
    else
    {
      static struct rtimer start_transmitting;
      rtimer_set(
          &start_transmitting,
          // wait the remainig time and then listen again
          epoch_start + (RECEPTION_WINDOW_DURATION * (RECEPTION_WINDOW_COUNT)),
          NULL,
          nd_send_beacon,
          NULL);
    }
  }
}

void nd_listen(void)
{
  NETSTACK_RADIO.on(); // start listening

  // set callback to stop listening
  static struct rtimer stop_listening;
  rtimer_set(
      &stop_listening,
      epoch_start + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? listen_count + 1 : listen_count)) + (RECEPTION_DURATION),
      NULL,
      nd_stop_listen,
      NULL);
}

void nd_step()
{
  if (epoch != -1)
  {
    app_cb.nd_epoch_end(epoch, discovered_n_epoch);
  }
  listen_count = 0;
  discovered_n_epoch = 0;
  epoch++;
  if (epoch_start != 0)
  {
    epoch_start = epoch_start + EPOCH_DURATION;
  }
  else
  {
    epoch_start = rtimer_arch_now();
  }
  if (FIRST_TRANSMIT)
  {
    nd_send_beacon();
  }
  else
  {
    nd_listen();
  }
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
    TRANSMISSION_PER_WINDOW = TRANSMISSION_PER_WINDOW_BURST;
    TRANSMISSION_WINDOW_COUNT = TRANSMISSION_WINDOW_COUNT_BURST;
    RECEPTION_WINDOW_COUNT = RECEPTION_WINDOW_COUNT_BURST;
    WINDOW_LEN = WINDOW_LEN_BURST;
    TRANSMISSION_DURATION = TRANSMISSION_DURATION_BURST;
    RECEPTION_DURATION = RECEPTION_DURATION_BURST;
    RECEPTION_WINDOW_DURATION = RECEPTION_WINDOW_DURATION_BURST;
    TRANSMISSION_WINDOW_DURATION = TRANSMISSION_WINDOW_DURATION_BURST;
    FIRST_TRANSMIT = true;
    nd_step();
    break;
  }
  case ND_SCATTER:
  {
    TRANSMISSION_PER_WINDOW = TRANSMISSION_PER_WINDOW_SCATTER;
    TRANSMISSION_WINDOW_COUNT = TRANSMISSION_WINDOW_COUNT_SCATTER;
    RECEPTION_WINDOW_COUNT = RECEPTION_WINDOW_COUNT_SCATTER;
    WINDOW_LEN = WINDOW_LEN_SCATTER;
    TRANSMISSION_DURATION = TRANSMISSION_DURATION_SCATTER;
    RECEPTION_DURATION = RECEPTION_DURATION_SCATTER;
    RECEPTION_WINDOW_DURATION = RECEPTION_WINDOW_DURATION_SCATTER;
    TRANSMISSION_WINDOW_DURATION = TRANSMISSION_WINDOW_DURATION_BURST;
    FIRST_TRANSMIT = false;
    nd_step();
    break;
  }
  }
}
/*---------------------------------------------------------------------------*/
