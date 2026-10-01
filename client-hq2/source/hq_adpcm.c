#include "hq_adpcm.h"
#if defined(__arm__) && defined(ARM9)
#pragma GCC target ("arm")
#pragma GCC optimize ("O3")
#endif
static const int step_table[89] = {
7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,
449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,
2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,
7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,
24623,27086,29794,32767};
static const int index_delta[8] = {-1,-1,-1,-1,2,4,6,8};
static int decode(unsigned code, int *predictor, int *index) {
    int step=step_table[*index], delta=step>>3;
    if(code&4)delta+=step;
    if(code&2)delta+=step>>1;
    if(code&1)delta+=step>>2;
    *predictor+=(code&8)?-delta:delta;
    if(*predictor>32767)*predictor=32767;
    if(*predictor<-32768)*predictor=-32768;
    *index+=index_delta[code&7];
    if(*index<0)*index=0;
    if(*index>88)*index=88;
    return *predictor;
}
int hq_decode_ima(const uint8_t *p,size_t bytes,int16_t *out,size_t capacity) {
    if(bytes<8 || bytes-8>capacity || p[2]>88 || p[6]>88 || p[3] || p[7])return -1;
    int pred[2]={(int16_t)(p[0]|p[1]<<8),(int16_t)(p[4]|p[5]<<8)};
    int index[2]={p[2],p[6]};
    for(size_t i=8;i<bytes;i++) {
        *out++=decode(p[i]>>4,&pred[0],&index[0]);
        *out++=decode(p[i]&15,&pred[1],&index[1]);
    }
    return (int)(bytes-8);
}
