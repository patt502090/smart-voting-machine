/*
  Protocol.h — Smart Voting Machine (Two-Board) Protocol
  ------------------------------------------------------
  Framed, TX-only (Arduino -> ESP32). Robust against noise with CRC16-CCITT.
  Frame:
    [STX=0x02][VER][TYPE][LEN][PAYLOAD...][CRC_H][CRC_L][ETX=0x03]
  CRC covers [VER..PAYLOAD] (i.e., from VER byte for LEN+2+payload bytes).

  Types:
    0x10 HEARTBEAT   payload: uptime32(ms) | mode8 | maxSel8 | fwMajor8 | fwMinor8
    0x11 TALLY_SNAP  payload: counts[8] (uint32 each, big-endian)
    0x12 VOTE_CAST   payload: voterId16 | mode8 | candMask8 | ts32
    0x13 TAMPER_EVT  payload: code8 | val16 | ts32
    0x14 BOX_DROP    payload: ok8 | dWeight16 | ts32
    0x1F ADMIN_LOG   payload: code8 | data16 | ts32

  Endian:
    - Multi-byte integers are Big-Endian within frames (network byte order).

  Notes:
    - TX-only ensures one-way data diode; ESP32 never sends commands.
*/
#pragma once
#include <Arduino.h>

namespace SVM {

static constexpr uint8_t STX=0x02, ETX=0x03, VER=0x01;

// CRC16-CCITT (poly 0x1021, init 0xFFFF)
inline uint16_t crc16_ccitt(const uint8_t* data, size_t len){
  uint16_t crc=0xFFFF;
  for(size_t i=0;i<len;i++){
    crc ^= (uint16_t)data[i] << 8;
    for(uint8_t b=0;b<8;b++){
      if(crc & 0x8000) crc = (crc<<1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

inline void be_write32(uint8_t* p, uint32_t v){
  p[0]=(v>>24)&0xFF; p[1]=(v>>16)&0xFF; p[2]=(v>>8)&0xFF; p[3]=v&0xFF;
}
inline void be_write16(uint8_t* p, uint16_t v){
  p[0]=(v>>8)&0xFF; p[1]=v&0xFF;
}

enum MsgType: uint8_t {
  HEARTBEAT = 0x10,
  TALLY_SNAP= 0x11,
  VOTE_CAST = 0x12,
  TAMPER_EVT= 0x13,
  BOX_DROP  = 0x14,
  ADMIN_LOG = 0x1F
};

struct FrameWriter {
  HardwareSerial& ser;
  FrameWriter(HardwareSerial& s): ser(s){}
  void writeFrame(MsgType t, const uint8_t* payload, uint8_t len){
    uint8_t hdr[3] = {VER, (uint8_t)t, len};
    uint16_t crc = crc16_ccitt(hdr, 3);
    crc = crc16_ccitt(payload, len) ^ ((crc<<0)|0); // combine
    // Note: a stricter approach: compute over concatenated [hdr|payload]
    // We'll do it explicitly:
    uint8_t buf[3+255]; // VER,TYPE,LEN + payload
    memcpy(buf, hdr, 3);
    if(len) memcpy(buf+3, payload, len);
    crc = crc16_ccitt(buf, 3+len);

    ser.write(STX);
    ser.write(hdr, 3);
    if(len) ser.write(payload, len);
    ser.write((crc>>8)&0xFF);
    ser.write(crc&0xFF);
    ser.write(ETX);
  }
};

} // namespace SVM
