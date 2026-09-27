#include "../../kernel/net/crdt.h"
#include "../../kernel/net/session.h"
#include "../../kernel/net/replica.h"
#include "../../kernel/net/migration.h"
#include "../../kernel/net/config.h"
#include "../../kernel/wasm/instance_state.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const uint8_t local[32]={1},peer[32]={2};
/* Every rejecting parser must preserve the entire caller-owned result. */
#define PARSE(type, expression) do { \
    type out,saved; memset(&out,0xa5,sizeof(out)); saved=out; \
    if(!(expression) && memcmp(&out,&saved,sizeof(out))!=0) abort(); \
} while(0)
int LLVMFuzzerTestOneInput(const uint8_t *bytes,size_t length) {
    PARSE(struct net_wire_view,net_wire_validate(bytes,length,&out));
    PARSE(struct net_boot_config,net_config_decode(bytes,length,&out));
    PARSE(struct replica_catalog,replica_catalog_decode(bytes,length,local,peer,&out));
    for(uint32_t kind=1;kind<=3;kind++)
        PARSE(struct replica_message,replica_message_decode(bytes,length,kind,&out));
    for(uint32_t kind=4;kind<=5;kind++)
        PARSE(struct migration_wire_view,migration_wire_decode(bytes,length,kind,&out));
    PARSE(struct migration_record,migration_record_decode(bytes,length,&out));
    PARSE(struct migration_receipt,migration_receipt_decode(bytes,length,&out));
    PARSE(struct migration_bundle_view,migration_bundle_validate(bytes,length,local,peer,&out));
    struct net_crdt state,saved; memset(&state,0xa5,sizeof(state)); saved=state;
    if(net_crdt_decode(bytes,length,&state)) {
        uint8_t canonical[NET_CRDT_BYTES]; struct net_crdt merged;
        if(net_crdt_encode(canonical,sizeof(canonical),&state)!=length ||
           memcmp(canonical,bytes,length)!=0 || !net_crdt_merge(&merged,&state,&state) ||
           memcmp(&merged,&state,sizeof(state))!=0) abort();
    } else if(memcmp(&state,&saved,sizeof(state))!=0) abort();
    return 0;
}
#ifdef JANI_FUZZ_SEEDS
static void put32(uint8_t *b,uint32_t v) { for(size_t i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); }
static void put64(uint8_t *b,uint64_t v) { for(size_t i=0;i<8;i++) b[i]=(uint8_t)(v>>(8*i)); }
static void id(uint8_t *b,uint64_t n) { put64(b,2); put64(b+8,n); }
static void save(const char *dir,const char *name,const uint8_t *b,size_t n) {
    char path[1024]; int length=snprintf(path,sizeof(path),"%s/%s",dir,name);
    if(length<0 || (size_t)length>=sizeof(path)) abort();
    FILE *f=fopen(path,"wb"); if(!f) abort();
    if(fwrite(b,1,n,f)!=n || fclose(f)!=0) abort();
    LLVMFuzzerTestOneInput(b,n);
}
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    uint8_t b[1160]={0}; struct net_crdt state;
    if(!net_crdt_init(&state,1) || !net_crdt_set(&state,0,(const uint8_t *)"value",5)) abort();
    if(net_crdt_encode(b,sizeof(b),&state)!=680) abort(); save(argv[1],"lww",b,680);
    if(!net_crdt_init(&state,2) || !net_crdt_add(&state,0,42) || !net_crdt_remove(&state,1,42)) abort();
    if(net_crdt_encode(b,sizeof(b),&state)!=680) abort(); save(argv[1],"orset",b,680);
    struct replica_catalog catalog={.count=1,.scopes={{.id={UINT64_C(1)<<63,1},.epoch=1,.kind=1,.rights=3}}};
    if(replica_catalog_encode(b,sizeof(b),local,peer,&catalog)!=400) abort();
    save(argv[1],"catalog",b,400);
    memset(b,0,sizeof(b)); memcpy(b,"JN5H",4); b[4]=1; save(argv[1],"hello",b,136);
    memcpy(b,"JN5D",4); b[5]=1; put64(b+40,1); save(argv[1],"session",b,136);
    memset(b,0,sizeof(b)); memcpy(b,"JR5O",4); b[4]=1;b[5]=3;b[6]=1;b[7]=3;
    put64(b+8,UINT64_C(1)<<63);put64(b+16,1);put64(b+24,1);save(argv[1],"grant",b,32);
    b[5]=1;b[7]=0;save(argv[1],"summary",b,80);
    b[5]=2;if(!net_crdt_init(&state,1) || net_crdt_encode(b+32,sizeof(b)-32,&state)!=680) abort();
    save(argv[1],"update",b,712);
    memset(b,0,sizeof(b));memcpy(b,"JF5M",4);b[4]=1;
    memcpy(b+8,peer,32);id(b+72,1);id(b+88,2);id(b+104,3);id(b+120,4);put64(b+136,1);
    save(argv[1],"fence",b,176);
    memset(b,0,sizeof(b));memcpy(b,"JI5M",4);b[4]=1;memcpy(b+8,peer,32);
    id(b+72,1);put64(b+88,1);id(b+96,4);save(argv[1],"receipt",b,128);
    memset(b,0,sizeof(b));memcpy(b,"JM5H",4);b[4]=1;b[5]=1;put64(b+8,1);id(b+48,1);put32(b+68,1000);
    save(argv[1],"migration-begin",b,96);
    b[5]=2;save(argv[1],"migration-chunk",b,100);
    b[5]=3;put32(b+64,1000);save(argv[1],"migration-end",b,96);
    b[5]=4;put32(b+64,0);id(b+72,4);put64(b+96,1);save(argv[1],"forward",b,108);
    memset(b,0,sizeof(b));memcpy(b,"JN5C",4);b[4]=1;memcpy(b+40,peer,32);memcpy(b+72,local,32);
    save(argv[1],"config",b,104);
    memset(b,0,sizeof(b));memcpy(b,"JB5M",4);b[4]=1;memcpy(b+8,local,32);memcpy(b+40,peer,32);
    id(b+72,1);put64(b+88,1);put32(b+96,8);put32(b+100,320);put32(b+104,544);
    const uint8_t module[]={0,0x61,0x73,0x6d,1,0,0,0};memcpy(b+128,module,8);
    uint8_t *caps=b+136;put64(caps,COMPONENT_CAPTABLE_MAGIC);put32(caps+8,COMPONENT_CAPTABLE_FORMAT_VERSION);
    put32(caps+12,1);put64(caps+16,1);id(caps+32,1);put32(caps+48,5);
    for(size_t i=0;i<16;i++) put32(caps+416+4*i,UINT32_MAX);
    put32(caps+480,1);put32(caps+24,object_crc32c(caps+32,512));
    struct instance_state_header h={.magic=INSTANCE_STATE_MAGIC,.format_version=1,.header_size=64,.memory_size=256};
    h.payload_crc32c=object_crc32c(b+744,256);memcpy(b+680,&h,64);
    struct migration_bundle_view view;
    if(!migration_bundle_validate(b,1000,local,peer,&view)) abort();
    save(argv[1],"bundle",b,1000);
    return 0;
}
#endif
