#include "egg_format.h"
uint8_t egg2_test_frame[128];
int egg_format_test(void) {
    if (egg_crc32((const uint8_t *)"123456789",9)!=0xcbf43926u) return 1;
    if (egg_correct_low(-1,0,32768,2048)!=0 || egg_correct_low(-3,0,32768,2048)!=-1) return 2;
    if (egg_correct_low(32767,0,131072,2048)!=32767 || egg_correct_low(-32768,0,131072,2048)!=-32768) return 3;
    if (egg_correct_low(0,-129,65536,2048)!=16) return 4;
    const int16_t samples[4][6]={{2048,-2048,0,96,-96,0},{32767,0,-32768,20000,-20000,0},
                               {-1,1,-1,1,-1,1},{0,0,0,30720,-30720,0}};
    for (unsigned i=0;i<4;i++) for (unsigned a=0;a<6;a++) egg_put16(egg2_test_frame+80+i*12+a*2,(uint16_t)samples[i][a]);
    egg2_metadata_t m={.flags=0x51,.rate=3840,.count=4,.trigger=1,.post=2,.calibration_generation=7,
                       .offset_live={128,-129,0},.scale_q16={98304,65536,32768}};
    m.payload_crc=egg_crc32(egg2_test_frame+80,48);
    egg2_header(egg2_test_frame,&m);
    if (memcmp(egg2_test_frame,"EGG2\x02\x51\x50\x00",8)) return 5;
    if (egg_crc32(egg2_test_frame,76)==0) return 6;
    return 0;
}
