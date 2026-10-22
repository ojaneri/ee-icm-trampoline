/*
 * ICM — unit tests for the CAN database (src/can_db.c).
 * Osvaldo Janeri Filho <janeri@gmail.com>
 *
 * The golden frames below were produced by sim/can_sim.py, not by can_db.c.
 * If the simulator and the cluster ever disagree on the layout, this is the
 * test that says so before the integration tests have to guess why.
 */
#include <string.h>

#include "can_db.h"
#include "check.h"
#include "icm_config.h"

static icm_can_frame_t frame(uint32_t id, const uint8_t data[8])
{
  icm_can_frame_t f = {.id = id, .dlc = 8};
  memcpy(f.data, data, 8);
  return f;
}

/* ICM-S-03: CRC-8 SAE J1850 standard check value. */
static void test_ICM_S_03_crc8_check_value(void)
{
  CHECK(can_db_crc8((const uint8_t *)"123456789", 9) == 0x4B);
}

/* ICM-F-01 ICM-F-02 ICM-F-03 ICM-F-04: golden frames from the simulator. */
static void test_ICM_F_01_to_04_golden_frames_decode(void)
{
  static const uint8_t m1[8] = {0xE0, 0x2E, 0, 0, 0, 0, 0x05, 0xF1};
  static const uint8_t m2[8] = {0xE0, 0x2E, 0, 0, 0, 0, 0x05, 0x10};
  static const uint8_t m3[8] = {0x83, 0x82, 0, 0, 0, 0, 0x0F, 0x4C};
  static const uint8_t m4[8] = {0x01, 0x48, 0, 0, 0, 0, 0x00, 0x1A};
  icm_can_values_t v;
  icm_can_frame_t f;

  f = frame(0x100, m1);
  CHECK(can_db_decode(&f, &v) == CAN_DB_OK);
  CHECK(v.msg == ICM_MSG_M1 && v.counter == 5);
  CHECK(v.engine_speed_raw == 12000); /* 3000 rpm at 0.25 rpm/bit */

  f = frame(0x200, m2);
  CHECK(can_db_decode(&f, &v) == CAN_DB_OK);
  CHECK(v.msg == ICM_MSG_M2 && v.vehicle_speed_raw == 12000); /* 120 km/h */

  f = frame(0x300, m3);
  CHECK(can_db_decode(&f, &v) == CAN_DB_OK);
  CHECK(v.fuel_raw == 131 && v.coolant_raw == 130 && v.counter == 15);

  f = frame(0x400, m4);
  CHECK(can_db_decode(&f, &v) == CAN_DB_OK);
  CHECK(v.turn_bits == ICM_TURN_LEFT);
  CHECK(v.warning_bits == (ICM_WARN_SEATBELT | ICM_WARN_MIL));
}

/* ICM-F-01: encode is the exact inverse of decode, for every message. */
static void test_ICM_F_01_encode_decode_roundtrip(void)
{
  for (int m = 0; m < ICM_MSG_COUNT; m++) {
    icm_can_values_t in = {.msg = (icm_msg_t)m, .counter = 9,
                           .engine_speed_raw = 31999, .vehicle_speed_raw = 25000,
                           .fuel_raw = 200, .coolant_raw = 190,
                           .turn_bits = 0x07, .warning_bits = 0xFF};
    icm_can_frame_t f;
    icm_can_values_t out;
    can_db_encode(&in, &f);
    CHECK(f.id == can_db_id_from_msg((icm_msg_t)m));
    CHECK(can_db_decode(&f, &out) == CAN_DB_OK);
    CHECK(out.msg == in.msg && out.counter == in.counter);
  }
}

/* ICM-S-03: any single-bit error in the payload is caught by the CRC. */
static void test_ICM_S_03_every_single_bit_flip_is_rejected(void)
{
  icm_can_values_t in = {.msg = ICM_MSG_M2, .counter = 3, .vehicle_speed_raw = 8000};
  icm_can_frame_t good;
  can_db_encode(&in, &good);
  for (int byte = 0; byte < 8; byte++) {
    for (int bit = 0; bit < 8; bit++) {
      icm_can_frame_t bad = good;
      bad.data[byte] ^= (uint8_t)(1u << bit);
      icm_can_values_t out;
      CHECK(can_db_decode(&bad, &out) == CAN_DB_BAD_CRC);
    }
  }
}

/* ICM-S-03: the CRC covers the identifier; M1's payload under M2's id fails. */
static void test_ICM_S_03_crc_covers_the_identifier(void)
{
  icm_can_values_t in = {.msg = ICM_MSG_M1, .counter = 1, .engine_speed_raw = 4000};
  icm_can_frame_t f;
  icm_can_values_t out;
  can_db_encode(&in, &f);
  f.id = ICM_CAN_ID_M2_VEHICLE_SPEED;
  CHECK(can_db_decode(&f, &out) == CAN_DB_BAD_CRC);
}

/* ICM-S-03: wrong length and unknown identifiers are refused. */
static void test_ICM_S_03_bad_dlc_and_unknown_id(void)
{
  icm_can_values_t in = {.msg = ICM_MSG_M4, .counter = 1};
  icm_can_frame_t f;
  icm_can_values_t out;
  can_db_encode(&in, &f);
  f.dlc = 7;
  CHECK(can_db_decode(&f, &out) == CAN_DB_BAD_DLC);
  f.dlc = 8;
  f.id = 0x123;
  CHECK(can_db_decode(&f, &out) == CAN_DB_UNKNOWN_ID);
}

/* ICM-S-03: alive counter. Repeat = stale, 1..3 = fine, more = frames lost,
 * and the 4-bit wrap from 15 to 0 is a normal step. */
static void test_ICM_S_03_alive_counter(void)
{
  CHECK(can_db_check_counter(5, 5) == CAN_DB_COUNTER_REPEATED);
  CHECK(can_db_check_counter(5, 6) == CAN_DB_COUNTER_OK);
  CHECK(can_db_check_counter(5, 8) == CAN_DB_COUNTER_OK);
  CHECK(can_db_check_counter(5, 9) == CAN_DB_COUNTER_JUMP);
  CHECK(can_db_check_counter(15, 0) == CAN_DB_COUNTER_OK);
  CHECK(can_db_check_counter(14, 1) == CAN_DB_COUNTER_OK);
  CHECK(can_db_check_counter(0, 15) == CAN_DB_COUNTER_JUMP);
}

int main(void)
{
  RUN(test_ICM_S_03_crc8_check_value);
  RUN(test_ICM_F_01_to_04_golden_frames_decode);
  RUN(test_ICM_F_01_encode_decode_roundtrip);
  RUN(test_ICM_S_03_every_single_bit_flip_is_rejected);
  RUN(test_ICM_S_03_crc_covers_the_identifier);
  RUN(test_ICM_S_03_bad_dlc_and_unknown_id);
  RUN(test_ICM_S_03_alive_counter);
  return CHECK_REPORT();
}
