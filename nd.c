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
bool FIRST_TRANSMIT = false; // basically tells if to use BURST or SCATTER

uint8_t sent_beacon_count = 0;
int listen_count = 0;
uint16_t epoch = 0;
uint8_t discovered_n_epoch = 0; // number of all neighbours discovered each epoch
uint8_t discovered_n_epoch_new = 0; // number of new nodes discovered each epoch
unsigned short epoch_start = 0;
int transmit_window_count = 0;
bool collision = true;
unsigned short collision_offset = 0;

uint32_t nid;

bool neighbors[MAX_NBR]; // assume ids are from 0 to MAX_NBR
bool neighbors_act_epoch[MAX_NBR]; // assume ids are from 0 to MAX_NBR

#define EPOCH_DURATION EPOCH_INTERVAL_RT
#define NUM_EPOCH_EACH_WRAP EPOCH_DURATION / USHRT_MAX // short unsigned dimension
#define TICKS_PER_SEC RTIMER_SECOND                    // number of ticks in one second
#define TICKS_PER_MILLISEC (TICKS_PER_SEC / 1000)      // number of ticks in one millisecond

#define TRANSMISSION_WINDOW_COUNT_BURST 1
#define RECEPTION_WINDOW_COUNT_BURST 10

#define RECEPTION_WINDOW_COUNT_SCATTER 1
// basically finds the number of transmission windows which last 100ms that can fit an epoch.
#define TRANSMISSION_WINDOW_COUNT_SCATTER (((EPOCH_DURATION / TICKS_PER_SEC) * 1000) / 100) - RECEPTION_WINDOW_COUNT_SCATTER

// both transmission and reception have the same window dimension
#define WINDOW_LEN_BURST EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT_BURST + RECEPTION_WINDOW_COUNT_BURST)
#define WINDOW_LEN_SCATTER EPOCH_DURATION / (TRANSMISSION_WINDOW_COUNT_SCATTER + RECEPTION_WINDOW_COUNT_SCATTER)

#define TRANSMISSION_WINDOW_DURATION_BURST WINDOW_LEN_BURST
#define TRANSMISSION_WINDOW_DURATION_SCATTER WINDOW_LEN_SCATTER

#define RECEPTION_WINDOW_DURATION_BURST WINDOW_LEN_BURST
#define RECEPTION_WINDOW_DURATION_SCATTER WINDOW_LEN_SCATTER

#define TRANSMISSION_DURATION_BURST (15 * TICKS_PER_MILLISEC)        // 15ms [ticks]
#define RECEPTION_DURATION_BURST RECEPTION_WINDOW_DURATION_BURST / 4 // [ticks]

#define TRANSMISSION_DURATION_SCATTER 100 * TICKS_PER_MILLISEC            // [ticks]
#define RECEPTION_DURATION_SCATTER RECEPTION_WINDOW_DURATION_SCATTER - 10 // [ticks]

#define TRANSMISSION_PER_WINDOW_BURST TRANSMISSION_WINDOW_DURATION_BURST / TRANSMISSION_DURATION_BURST
#define TRANSMISSION_PER_WINDOW_SCATTER 1

#define TRANSMISSION_COLLISION_OFFSET (((unsigned short)rand()) % (10 * TICKS_PER_MILLISEC) + 0) // offset added to the transmission to avoid collisions

#define EPOCH_COLLISION_OFFSET 0 //(((unsigned short)rand()) % 20 + 10) // offset added to the epoch end to avoid collision

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

  uint32_t nbr_id;

  memcpy(&nbr_id, packetbuf_dataptr(), sizeof(uint32_t));

  /*
  if (packetbuf_datalen() != sizeof(unsigned short))
  {
    printf("invalid payload len. Value read: %u\n", nbr_id);
    return;
  }
  */

  if (nbr_id == 0)
  {
    return; // idk why but sometimes has payload 0
  }

  /*
  int i = 0;
  for (i; i < 11; i++)
  {
    printf("Neighbours: %d", neighbors[i]);
  };
  printf("\n");
  */

  // check if neigh already found, otherwise set to found
  if (nbr_id < MAX_NBR)
  {
    if (!neighbors_act_epoch[nbr_id]) {
      discovered_n_epoch++;
      neighbors_act_epoch[nbr_id] = true;
    }
    
    if (!neighbors[nbr_id])
    {
      neighbors[nbr_id] = true;
      discovered_n_epoch_new++;

      if (app_cb.nd_new_nbr != NULL) // sometimes the first one happens to be NULL
      {
        app_cb.nd_new_nbr(epoch, nbr_id);
      }
      else
      {
        printf("app_cb.nd_new_nbr is NULL\n");
      }
    }
  }
}

/*
 * Sends a beacon
 */
void nd_send_beacon(void)
{
  int ret = NETSTACK_RADIO.send(&nid, sizeof(uint32_t));
  if (ret == RADIO_TX_COLLISION)
  {
    //printf("there was a collision\n");
    // adds an offset to the epoch to try avoid a collision again
    epoch_start = epoch_start + EPOCH_COLLISION_OFFSET;
    collision = true;
  }

  sent_beacon_count++;

  if (sent_beacon_count != TRANSMISSION_PER_WINDOW)
  {
    unsigned short send_time = 0;
    if (sent_beacon_count == TRANSMISSION_PER_WINDOW - 1)
    {
      // if it is the last beacon avoid putting the collision offset to not go out of the epoch
      send_time = epoch_start + (sent_beacon_count * TRANSMISSION_DURATION) + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? 0 : 1));
    }
    else
    {
      send_time = epoch_start + (sent_beacon_count * TRANSMISSION_DURATION) + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? 0 : 1)) + (collision ? collision_offset : 0);
    }
    static struct rtimer beacon_timer;
    rtimer_set(
        &beacon_timer,
        send_time, // set next beacon wrt to epoch start
        0,
        (rtimer_callback_t)nd_send_beacon,
        NULL);
  }
  else
  {
    transmit_window_count++;
    sent_beacon_count = 0;
    if (transmit_window_count != TRANSMISSION_WINDOW_COUNT)
    {
      unsigned short send_time = 0;
      if (transmit_window_count == TRANSMISSION_WINDOW_COUNT - 1)
      {
        // Avoid collision offset to not go out of epoch
        send_time = epoch_start + (TRANSMISSION_WINDOW_DURATION * transmit_window_count) + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? 0 : 1));
      }
      else
      {
        send_time = epoch_start + (TRANSMISSION_WINDOW_DURATION * transmit_window_count) + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? 0 : 1)) + (collision ? collision_offset : 0);
      }
      // do another transmission window
      static struct rtimer beacon_timer;
      rtimer_set(
          &beacon_timer,
          send_time,
          0,
          (rtimer_callback_t)nd_send_beacon,
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
            epoch_start + TRANSMISSION_WINDOW_DURATION,
            0,
            (rtimer_callback_t)nd_listen,
            NULL);
      }
      else
      {
        static struct rtimer epoch_end;
        rtimer_set(
            &epoch_end,
            // wait the remainig time and then call epoch end
            epoch_start + EPOCH_DURATION,
            0,
            (rtimer_callback_t)nd_step,
            NULL);
      }
    }
  }
}

/**
 * Callback to stop listening
 */
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
        0,
        (rtimer_callback_t)nd_listen,
        NULL);
  }
  else
  {
    if (FIRST_TRANSMIT && listen_count == RECEPTION_WINDOW_COUNT)
    {
      // do last one receive just before end of epoch
      static struct rtimer start_listening;
      rtimer_set(
          &start_listening,
          // wait the remainig time and then listen again
          epoch_start + EPOCH_DURATION - RECEPTION_DURATION - 20, // -10 to give some thresold
          0,
          (rtimer_callback_t)nd_listen_last,
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
            0,
            (rtimer_callback_t)nd_step,
            NULL);
      }
      else
      {
        static struct rtimer start_transmitting;
        rtimer_set(
            &start_transmitting,
            // wait the remainig time and then listen again
            epoch_start + (RECEPTION_WINDOW_DURATION * RECEPTION_WINDOW_COUNT),
            0,
            (rtimer_callback_t)nd_send_beacon,
            NULL);
      }
    }
  }
}

/**
 * Does the last listening step just before transmit in burst
 */
void nd_listen_last(void)
{
  NETSTACK_RADIO.on(); // start listening
  // set callback to stop listening
  static struct rtimer stop_listening;
  rtimer_set(
      &stop_listening,
      epoch_start + EPOCH_DURATION - 20, // -20 is to have some thresold
      0,
      (rtimer_callback_t)nd_stop_listen,
      NULL);
}

/**
 * Callback to start listening
 */
void nd_listen(void)
{
  NETSTACK_RADIO.on(); // start listening
  // set callback to stop listening
  static struct rtimer stop_listening;
  rtimer_set(
      &stop_listening,
      epoch_start + (RECEPTION_WINDOW_DURATION * (FIRST_TRANSMIT ? listen_count + 1 : listen_count)) + RECEPTION_DURATION,
      0,
      (rtimer_callback_t)nd_stop_listen,
      NULL);
}

/**
 * Function called each end of epoch
 */
void nd_step()
{
  if (epoch != 0)
  {
    app_cb.nd_epoch_end(epoch, discovered_n_epoch, discovered_n_epoch_new);
  }

  if (!FIRST_TRANSMIT)
  {
    // if scatter: transmit at the end of the epoch before starting to listen
    NETSTACK_RADIO.send(&nid, sizeof(uint32_t));
  }
  epoch++;
  listen_count = 0;
  discovered_n_epoch = 0;
  discovered_n_epoch_new = 0;
  sent_beacon_count = 0;

  // reset neighbours bitset
  int i = 0;
  for (; i < MAX_NBR; i++)
  {
    neighbors_act_epoch[i] = false;
  }

  printf("NCO: %u\n", collision_offset);
  if (epoch_start != 0)
  {
    epoch_start = epoch_start + EPOCH_DURATION + (collision ? EPOCH_COLLISION_OFFSET : 0);
  }
  else
  {
    epoch_start = rtimer_arch_now();
  }

  // collision = false; //uncomment to add slack only after a collision
  collision_offset = TRANSMISSION_COLLISION_OFFSET;

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

  // set reference of callbacks
  app_cb.nd_new_nbr = cb->nd_new_nbr;
  app_cb.nd_epoch_end = cb->nd_epoch_end;

  nid = (uint32_t) node_id;

  // init neighbours bitset
  int i = 0;
  for (; i < MAX_NBR; i++)
  {
    neighbors[i] = false;
    neighbors_act_epoch[i] = false;
  }

  switch (mode)
  {
  case ND_BURST:
  {
    // set settings for BURST primitive
    TRANSMISSION_PER_WINDOW = TRANSMISSION_PER_WINDOW_BURST;
    TRANSMISSION_WINDOW_COUNT = TRANSMISSION_WINDOW_COUNT_BURST;
    RECEPTION_WINDOW_COUNT = RECEPTION_WINDOW_COUNT_BURST;
    WINDOW_LEN = WINDOW_LEN_BURST;
    TRANSMISSION_DURATION = TRANSMISSION_DURATION_BURST; // WHY THIS IS 0
    RECEPTION_DURATION = RECEPTION_DURATION_BURST;
    RECEPTION_WINDOW_DURATION = RECEPTION_WINDOW_DURATION_BURST;
    TRANSMISSION_WINDOW_DURATION = TRANSMISSION_WINDOW_DURATION_BURST;
    FIRST_TRANSMIT = true;
    printf(
        "START: BURST, %d, %d, %d, %d, %d, %d, %d\n",
        TRANSMISSION_WINDOW_COUNT,
        RECEPTION_WINDOW_COUNT,
        TRANSMISSION_WINDOW_DURATION,
        RECEPTION_WINDOW_DURATION,
        TRANSMISSION_PER_WINDOW,
        TRANSMISSION_DURATION,
        RECEPTION_DURATION);
    printf(
        "START: TYPE, \
        TRANSMISSION_WINDOW_COUNT, \
        RECEPTION_WINDOW_COUNT, \
        TRANSMISSION_WINDOW_DURATION, \
        RECEPTION_WINDOW_DURATION, \
        TRANSMISSION_PER_WINDOW, \
        TRANSMISSION_DURATION, \
        RECEPTION_DURATION\n");
    nd_step(); // does the first step
    break;
  }
  case ND_SCATTER:
  {
    // set settings for SCATTER primitive
    TRANSMISSION_PER_WINDOW = TRANSMISSION_PER_WINDOW_SCATTER;
    TRANSMISSION_WINDOW_COUNT = TRANSMISSION_WINDOW_COUNT_SCATTER;
    RECEPTION_WINDOW_COUNT = RECEPTION_WINDOW_COUNT_SCATTER;
    WINDOW_LEN = WINDOW_LEN_SCATTER;
    TRANSMISSION_DURATION = TRANSMISSION_DURATION_SCATTER;
    RECEPTION_DURATION = RECEPTION_DURATION_SCATTER;
    RECEPTION_WINDOW_DURATION = RECEPTION_WINDOW_DURATION_SCATTER;
    TRANSMISSION_WINDOW_DURATION = TRANSMISSION_WINDOW_DURATION_SCATTER;
    FIRST_TRANSMIT = false;
    printf(
        "START: SCATTER, %d, %d, %d, %d, %d, %d, %d\n",
        TRANSMISSION_WINDOW_COUNT,
        RECEPTION_WINDOW_COUNT,
        TRANSMISSION_WINDOW_DURATION,
        RECEPTION_WINDOW_DURATION,
        TRANSMISSION_PER_WINDOW,
        TRANSMISSION_DURATION,
        RECEPTION_DURATION);
    printf(
        "START: TYPE, \
        TRANSMISSION_WINDOW_COUNT, \
        RECEPTION_WINDOW_COUNT, \
        TRANSMISSION_WINDOW_DURATION, \
        RECEPTION_WINDOW_DURATION, \
        TRANSMISSION_PER_WINDOW, \
        TRANSMISSION_DURATION, \
        RECEPTION_DURATION\n");
    nd_step(); // does the first step
    break;
  }
  }
}
/*---------------------------------------------------------------------------*/
