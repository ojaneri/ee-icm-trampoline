/*
 * ICM — CAN database implementation.
 * Osvaldo Janeri Filho <janeri@gmail.com>
 */
#include "can_db.h"

#include <string.h>

#include "icm_config.h"

static const uint32_t msg_ids[ICM_MSG_COUNT] = {
    ICM_CAN_ID_M1_ENGINE_SPEED,
    ICM_CAN_ID_M2_VEHICLE_SPEED,
    ICM_CAN_ID_M3_ENGINE_STATUS,
    ICM_CAN_ID_M4_BODY_STATUS,
};

static const char *const msg_names[ICM_MSG_COUNT] = {
    "M1_EngineSpeed",
    "M2_VehicleSpeed",
    "M3_EngineStatus",
    "M4_BodyStatus",
};

/*
 * CRC-8 SAE J1850: polynomial 0x1D, initial value 0xFF, final XOR 0xFF.
 * This is the CRC AUTOSAR E2E Profiles 1 and 2 use. Bitwise rather than
 * table-driven: nine bytes per frame and a few frames per second do not
 * justify 256 bytes of table on a cluster MCU. Check value for "123456789"
 * is 0x4B, asserted in tests/unit/test_can_db.c.
 */
uint8_t can_db_crc8(const uint8_t *data, size_t len)
{
  uint8_t crc = 0xFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; bit++) {
      unsigned shifted = (unsigned)crc << 1;
      crc = (uint8_t)((crc & 0x80u) ? (shifted ^ 0x1Du) : shifted);
    }
  }
  return (uint8_t)(crc ^ 0xFFu);
}

static uint8_t frame_crc(uint32_t id, const uint8_t data[8])
{
  uint8_t buf[9];
  buf[0] = (uint8_t)(id & 0xFFu);
  buf[1] = (uint8_t)((id >> 8) & 0xFFu);
  memcpy(&buf[2], data, 7); /* bytes 0..6; byte 7 is the CRC itself */
  return can_db_crc8(buf, sizeof buf);
}

icm_msg_t can_db_msg_from_id(uint32_t id)
{
  for (int m = 0; m < ICM_MSG_COUNT; m++) {
    if (msg_ids[m] == id) {
      return (icm_msg_t)m;
    }
  }
  return ICM_MSG_COUNT;
}

uint32_t can_db_id_from_msg(icm_msg_t msg)
{
  return (msg < ICM_MSG_COUNT) ? msg_ids[msg] : 0u;
}

const char *can_db_msg_name(icm_msg_t msg)
{
  return (msg < ICM_MSG_COUNT) ? msg_names[msg] : "unknown";
}

static uint16_t get_u16_le(const uint8_t *p)
{
  return (uint16_t)(p[0] | (p[1] << 8));
}

can_db_status_t can_db_decode(const icm_can_frame_t *frame,
                              icm_can_values_t *out)
{
  icm_msg_t msg = can_db_msg_from_id(frame->id);
  if (msg == ICM_MSG_COUNT) {
    return CAN_DB_UNKNOWN_ID;
  }
  /* Every ICM frame is 8 bytes. A shorter frame is a sender bug and decoding
   * it would read bytes that were never transmitted. */
  if (frame->dlc != 8u) {
    return CAN_DB_BAD_DLC;
  }
  if (frame_crc(frame->id, frame->data) != frame->data[7]) {
    return CAN_DB_BAD_CRC;
  }

  memset(out, 0, sizeof *out);
  out->msg = msg;
  out->counter = (uint8_t)(frame->data[6] & 0x0Fu);

  switch (msg) {
  case ICM_MSG_M1:
    out->engine_speed_raw = get_u16_le(&frame->data[0]);
    break;
  case ICM_MSG_M2:
    out->vehicle_speed_raw = get_u16_le(&frame->data[0]);
    break;
  case ICM_MSG_M3:
    out->fuel_raw = frame->data[0];
    out->coolant_raw = frame->data[1];
    break;
  case ICM_MSG_M4:
    out->turn_bits = frame->data[0];
    out->warning_bits = frame->data[1];
    break;
  default:
    break;
  }
  return CAN_DB_OK;
}

void can_db_encode(const icm_can_values_t *values, icm_can_frame_t *out)
{
  memset(out, 0, sizeof *out);
  out->id = can_db_id_from_msg(values->msg);
  out->dlc = 8u;

  switch (values->msg) {
  case ICM_MSG_M1:
    out->data[0] = (uint8_t)(values->engine_speed_raw & 0xFFu);
    out->data[1] = (uint8_t)(values->engine_speed_raw >> 8);
    break;
  case ICM_MSG_M2:
    out->data[0] = (uint8_t)(values->vehicle_speed_raw & 0xFFu);
    out->data[1] = (uint8_t)(values->vehicle_speed_raw >> 8);
    break;
  case ICM_MSG_M3:
    out->data[0] = values->fuel_raw;
    out->data[1] = values->coolant_raw;
    break;
  case ICM_MSG_M4:
    out->data[0] = values->turn_bits;
    out->data[1] = values->warning_bits;
    break;
  default:
    break;
  }
  out->data[6] = (uint8_t)(values->counter & 0x0Fu);
  out->data[7] = frame_crc(out->id, out->data);
}

/*
 * The alive counter is 4 bits and wraps 15 -> 0. A repeated value means the
 * sender's software stopped while its CAN controller kept re-sending the last
 * mailbox, which is precisely the failure the counter exists to catch: the
 * frame is fresh on the bus but the data in it is not. A jump means frames
 * were lost; the data in this one is still fresh and its CRC is good, so it is
 * accepted and the gap only counted.
 */
can_db_counter_t can_db_check_counter(uint8_t previous, uint8_t current)
{
  uint8_t delta = (uint8_t)((current - previous) & 0x0Fu);
  if (delta == 0u) {
    return CAN_DB_COUNTER_REPEATED;
  }
  if (delta > ICM_CAN_MAX_COUNTER_DELTA) {
    return CAN_DB_COUNTER_JUMP;
  }
  return CAN_DB_COUNTER_OK;
}
