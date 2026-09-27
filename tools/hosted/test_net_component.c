#include "check.h"
#include "../../kernel/drivers/net_component.h"
#include "../../kernel/drivers/virtio_net.h"
#include "../../kernel/net/frame_encode.h"
#include "../../kernel/wasm/syscalls.h"
#include <string.h>

unsigned long checks_passed;
__asm__(".section .rodata\n.global _binary_build_net_driver_wasm_start\n"
        "_binary_build_net_driver_wasm_start:\n.byte 0\n"
        ".global _binary_build_net_driver_wasm_end\n_binary_build_net_driver_wasm_end:\n.previous");
static struct component *current;
static struct component outsider;
static unsigned int mode, creates, releases;
int jani_wasm_runtime_start(void) { return 1; }
int jani_wasm_instance_create(const uint8_t *p,size_t n,void **m,void **i,void **e,void **b) {
    (void)p; (void)n; creates++; *m=*i=*e=*b=&mode; return 1;
}
int component_release(struct component *c) { releases++; c->instance=NULL; return 1; }
struct component *jani_syscall_current(void) { return current; }
void jani_wasm_set_current_component(struct component *c) { current=c; }
void kputs(const char *s) { (void)s; }
int jani_wasm_instance_call(void *i,void *e,const char *name) {
    (void)i; (void)e; CHECK(strcmp(name,"jani_on_message")==0);
    uint8_t bytes[1514], untouched[1514]; memset(bytes,0xa5,sizeof(bytes));
    memcpy(untouched,bytes,sizeof(bytes));
    CHECK(net_component_request(&outsider,bytes,sizeof(bytes)) == JANI_EPERM);
    CHECK(memcmp(bytes,untouched,sizeof(bytes))==0);
    CHECK(net_component_complete(&outsider,-1,NULL,0)==JANI_EPERM);
    CHECK(net_component_complete(current,-1,NULL,0)==JANI_EPERM);
    CHECK(net_component_request(current,bytes,1513)==JANI_EPERM);
    int length=net_component_request(current,bytes,sizeof(bytes)); CHECK(length>=14);
    CHECK(net_component_request(current,bytes,sizeof(bytes))==JANI_EPERM);
    if(mode==1) return 0;
    if(mode==2) return 1;
    struct jani_udp_datagram packet;
    if(!jani_udp_frame_decode(bytes,(size_t)length,&packet)) {
        CHECK(net_component_complete(current,-1,NULL,0)==0); return 1;
    }
    struct net_frame_result result={0};
    memcpy(&result.source,packet.source,4); memcpy(&result.destination,packet.destination,4);
    result.source_port=packet.source_port; result.destination_port=packet.destination_port;
    result.payload_offset=packet.payload_offset; result.payload_length=packet.payload_length;
    struct net_frame_result bad=result; bad.payload_offset=UINT32_MAX;
    CHECK(net_component_complete(current,0,(const uint8_t *)&bad,sizeof(bad))==JANI_EINVAL);
    CHECK(net_component_complete(current,0,(const uint8_t *)&result,sizeof(result)-1)==JANI_EINVAL);
    if(mode==3) result.source_port++;
    CHECK(net_component_complete(current,0,(const uint8_t *)&result,sizeof(result))==0);
    CHECK(net_component_complete(current,0,(const uint8_t *)&result,sizeof(result))==JANI_EPERM);
    return 1;
}
int main(void) {
    current=&outsider;
    struct jani_udp_frame_fields fields={.source_mac={2,0,0,0,0,1},.destination_mac={2,0,0,0,0,2},
        .source_ip={10,0,0,1},.destination_ip={10,0,0,2},.source_port=5555,.destination_port=5555,.ttl=64};
    uint8_t frame[1514]; size_t length=jani_udp_frame_encode(frame,sizeof(frame),&fields,(const uint8_t *)"data",4);
    CHECK(length>=60);
    struct jani_udp_datagram output,saved; memset(&output,0xa5,sizeof(output)); saved=output;
    CHECK(!net_component_validate(frame,13,&output)); CHECK(creates==0);
    CHECK(!net_component_validate(frame,1515,&output)); CHECK(creates==0);
    CHECK(net_component_validate(frame,length,&output));
    CHECK(output.payload_length==4 && current==&outsider);
    for(mode=1;mode<=3;mode++) {
        output=saved; CHECK(!net_component_validate(frame,length,&output));
        CHECK(memcmp(&output,&saved,sizeof(output))==0 && current==&outsider);
    }
    mode=0; CHECK(net_component_validate(frame,length,&output));
    CHECK(creates==4 && releases==3);
    frame[24]^=1; output=saved; CHECK(!net_component_validate(frame,length,&output));
    CHECK(memcmp(&output,&saved,sizeof(output))==0);
    net_component_stop(); CHECK(releases==4);
    printf("test_net_component: %lu checks passed\n",checks_passed); return 0;
}
