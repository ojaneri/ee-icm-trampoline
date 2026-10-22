/*
 * ICM — CAN database: frame layout, encode/decode and end-to-end checks.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * Pure C, no RTOS calls, so tests/unit can exercise it on the host. The same
 * layout is described in dbc/icm.dbc and re-implemented in sim/can_sim.py;
 * tests/integration/test_dbc_consistency.py checks the three agree.
 *
 * Layout of every ICM frame (DLC 8, Intel byte order):
 *
 *   byte 0..5  signals (per message, see below)
 *   byte 6     bits 0..3 alive counter, bits 4..7 reserved (0)
 *   byte 7     CRC-8 SAE J1850 over [id_lo, id_hi, byte0..byte6]
 *
 * The CRC covers the CAN identifier as well as the payload, the way AUTOSAR
 * E2E Profile 1 folds in a Data ID: a frame that arrives under the wrong
 * identifier fails the check instead of being decoded as something else.
 */
#ifndef CAN_DB_H
#define CAN_DB_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
  ICM_MSG_M1 = 0, /* 0x100 EngineSpeed,   500 ms (professor) */
  ICM_MSG_M2,     /* 0x200 VehicleSpeed, 1000 ms (professor) */
  ICM_MSG_M3,     /* 0x300 EngineStatus, 1000 ms (fuel, coolant) */
  ICM_MSG_M4,     /* 0x400 BodyStatus,    100 ms (turn signals, lamps) */
  ICM_MSG_COUNT
} icm_msg_t;

/* Our own frame type, so this header does not drag in <linux/can.h>. */
typedef struct {
  uint32_t id;
  uint8_t dlc;
  uint8_t data[8];
} icm_can_frame_t;

/* M4 byte 0 */
#define ICM_TURN_LEFT     0x01u
#define ICM_TURN_RIGHT    0x02u
#define ICM_TURN_HAZARD   0x04u
#define ICM_HIGH_BEAM     0x08u

/* M4 byte 1: telltales requested by other ECUs */
#define ICM_WARN_OIL      0x01u
#define ICM_WARN_BATTERY  0x02u
#define ICM_WARN_BRAKE    0x04u
#define ICM_WARN_MIL      0x08u /* check engine */
#define ICM_WARN_ABS      0x10u
#define ICM_WARN_AIRBAG   0x20u
#define ICM_WARN_SEATBELT 0x40u
#define ICM_WARN_DOOR     0x80u

/* Decoded content of one frame. Only the fields of `msg` are meaningful. */
typedef struct {
  icm_msg_t msg;
  uint8_t counter;
  uint16_t engine_speed_raw;  /* M1: 0.25 rpm/bit            */
  uint16_t vehicle_speed_raw; /* M2: 0.01 km/h/bit           */
  uint8_t fuel_raw;           /* M3 byte 0: 0.5 %/bit        */
  uint8_t coolant_raw;        /* M3 byte 1: 1 degC/bit, -40  */
  uint8_t turn_bits;          /* M4 byte 0                   */
  uint8_t warning_bits;       /* M4 byte 1                   */
} icm_can_values_t;

typedef enum {
  CAN_DB_OK = 0,
  CAN_DB_UNKNOWN_ID,
  CAN_DB_BAD_DLC,
  CAN_DB_BAD_CRC
} can_db_status_t;

typedef enum {
  CAN_DB_COUNTER_OK = 0, /* advanced by 1..ICM_CAN_MAX_COUNTER_DELTA       */
  CAN_DB_COUNTER_REPEATED, /* same value again: stale frame, discarded    */
  CAN_DB_COUNTER_JUMP      /* advanced too far: frames were lost, accepted */
} can_db_counter_t;

uint8_t can_db_crc8(const uint8_t *data, size_t len);

/* Returns ICM_MSG_COUNT for identifiers the cluster does not listen to. */
icm_msg_t can_db_msg_from_id(uint32_t id);
uint32_t can_db_id_from_msg(icm_msg_t msg);
const char *can_db_msg_name(icm_msg_t msg);

can_db_status_t can_db_decode(const icm_can_frame_t *frame,
                              icm_can_values_t *out);

/* Used by the unit tests to build frames; the simulator has its own copy. */
void can_db_encode(const icm_can_values_t *values, icm_can_frame_t *out);

can_db_counter_t can_db_check_counter(uint8_t previous, uint8_t current);

#endif /* CAN_DB_H */
