#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

#define EGG2_HEADER_BYTES 80u
#define EGG2_SAMPLE_BYTES 12u
#define EGG_FLAG_INVALID 0x20u
#define EGG_FLAG_HIGH_CLIPPED 0x40u

// Header calibration offsets use +/-2 g live counts (16384 counts/g), even
// though the recording uses +/-16 g low counts (2048 counts/g). Q16 scales
// represent gain as an integer divided by 65536; 65536 means unity gain.
typedef struct {
    uint8_t flags;
    uint32_t rate, count, trigger, post, payload_crc, calibration_generation;
    int32_t offset_live[3];
    uint32_t scale_q16[3];
} egg2_metadata_t;

static inline uint32_t egg_crc32(const uint8_t *bytes, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i=0; i<size; ++i) {
        crc ^= bytes[i];
        for (unsigned b=0; b<8; ++b) crc=(crc>>1)^(0xedb88320u & (uint32_t)-(int32_t)(crc&1u));
    }
    return ~crc;
}
static inline void egg_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static inline void egg_put32(uint8_t *p, uint32_t v) {
    egg_put16(p,(uint16_t)v); egg_put16(p+2,(uint16_t)(v>>16));
}
// Identical integer correction to the on-device low-g calibration.
static inline int16_t egg_correct_low(int16_t raw, int32_t offset_live, uint32_t scale_q16,
                                       uint32_t counts_per_g) {
    int32_t offset=(int32_t)(((int64_t)offset_live*counts_per_g)/16384);
    int64_t corrected=((int64_t)(raw-offset)*scale_q16+32768)>>16;
    if (corrected>INT16_MAX) return INT16_MAX;
    if (corrected<INT16_MIN) return INT16_MIN;
    return (int16_t)corrected;
}
// Serialize little-endian fields and a header CRC independently of host ABI.
// The payload has its own CRC; corruption detection is not sensor calibration
// and does not establish physical accuracy. See EGG_FORMAT.md for every offset.
static inline void egg2_header(uint8_t out[EGG2_HEADER_BYTES], const egg2_metadata_t *m) {
    memset(out,0,EGG2_HEADER_BYTES);
    memcpy(out,"EGG2",4); out[4]=2; out[5]=m->flags;
    egg_put16(out+6,EGG2_HEADER_BYTES);
    egg_put32(out+8,m->rate); egg_put32(out+12,m->count);
    egg_put32(out+16,m->trigger); egg_put32(out+20,m->post);
    egg_put16(out+24,2048); egg_put16(out+26,EGG2_SAMPLE_BYTES); egg_put32(out+28,m->payload_crc);
    egg_put32(out+32,10417); // high-g micro-g/count, nominal ST sensitivity
    egg_put16(out+36,0x73); egg_put16(out+38,320);
    for (unsigned a=0; a<3; ++a) {
        egg_put32(out+40+4*a,(uint32_t)m->offset_live[a]);
        egg_put32(out+52+4*a,m->scale_q16[a]);
    }
    egg_put32(out+64,m->calibration_generation); egg_put32(out+68,16384);
    egg_put32(out+76,egg_crc32(out,76));
}
