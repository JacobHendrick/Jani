#include "net_component.h"
#include "virtio_net.h"
#include "../wasm/runtime.h"
#include "../wasm/syscalls.h"
#include "../lib/string.h"
#include "../lib/printk.h"

extern const uint8_t _binary_build_net_driver_wasm_start[], _binary_build_net_driver_wasm_end[];
static struct component driver;
static uint32_t generation;
static int alive, busy, received, completed, response_status;
static size_t request_length;
static uint8_t request_bytes[VIRTIO_NET_MAX_FRAME_SIZE];
static struct net_frame_result response;

static int equal(const void *left, const void *right, size_t n) {
    const uint8_t *a=left,*b=right;
    for(size_t i=0;i<n;i++) if(a[i]!=b[i]) return 0;
    return 1;
}
int net_component_start(void) {
    if(busy || generation == UINT32_MAX) return 0;
    if(alive) return 1;
    if(driver.instance != NULL) component_release(&driver);
    memset(&driver,0,sizeof(driver)); driver.root_id=(struct object_id){10,3};
    capability_table_init(&driver.capability_table);
    if(!jani_wasm_runtime_start() ||
       !jani_wasm_instance_create(_binary_build_net_driver_wasm_start,
           (size_t)(_binary_build_net_driver_wasm_end-_binary_build_net_driver_wasm_start),
           &driver.module,&driver.instance,&driver.exec_env,&driver.module_bytes)) return 0;
    generation++; alive=1;
    kputs("virtio-net: Zig protocol component ready\n"); return 1;
}
int net_component_request(struct component *caller, uint8_t *bytes, uint32_t length) {
    if(caller != &driver || !alive || !busy || received ||
       bytes == NULL || length != sizeof(request_bytes)) return JANI_EPERM;
    memset(bytes,0,length); memcpy(bytes,request_bytes,request_length); received=1;
    return (int)request_length;
}
int net_component_complete(struct component *caller, int32_t status, const uint8_t *bytes, uint32_t length) {
    if(caller != &driver || !alive || !busy || !received || completed ||
       (status != 0 && status != -1)) return JANI_EPERM;
    if(status == -1) {
        if(length != 0) return JANI_EINVAL;
        response_status=-1; completed=1; return 0;
    }
    if(bytes == NULL || length != sizeof(response)) return JANI_EINVAL;
    struct net_frame_result next; memcpy(&next,bytes,sizeof(next));
    if(next.reserved != 0 || next.source_port == 0 || next.destination_port == 0 ||
       next.payload_offset > request_length || next.payload_length > request_length-next.payload_offset) return JANI_EINVAL;
    response=next; response_status=0; completed=1; return 0;
}
int net_component_validate(const uint8_t *frame, size_t length, struct jani_udp_datagram *out) {
    if(busy || frame == NULL || out == NULL || length < VIRTIO_NET_MIN_FRAME_SIZE ||
       length > sizeof(request_bytes) || !net_component_start()) return 0;
    memcpy(request_bytes,frame,length); request_length=length;
    received=completed=0; response_status=-1; memset(&response,0,sizeof(response)); busy=1;
    struct component *previous=jani_syscall_current();
    jani_wasm_set_current_component(&driver);
    int ok=jani_wasm_instance_call(driver.instance,driver.exec_env,"jani_on_message");
    jani_wasm_set_current_component(previous); busy=0;
    if(!ok || !completed) { alive=0; return 0; }
    if(response_status != 0) return 0;
    struct jani_udp_datagram checked;
    if(!jani_udp_frame_decode(request_bytes,request_length,&checked) ||
       !equal(&response.source,checked.source,4) || !equal(&response.destination,checked.destination,4) ||
       response.source_port != checked.source_port || response.destination_port != checked.destination_port ||
       response.payload_offset != checked.payload_offset || response.payload_length != checked.payload_length) {
        alive=0; return 0;
    }
    *out=checked; return 1;
}
void net_component_stop(void) {
    if(busy) return;
    if(driver.instance != NULL) component_release(&driver);
    alive=0; memset(request_bytes,0,sizeof(request_bytes)); request_length=0;
}
