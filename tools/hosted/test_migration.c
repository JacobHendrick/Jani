#include "check.h"
#include "../../kernel/net/migration.h"
#include "../../kernel/wasm/runtime.h"
#include <stdlib.h>
#include <string.h>

#define SECTORS 1024u
#define TABLE 32u
unsigned long checks_passed;
static unsigned int initializations;
struct disk { uint8_t bytes[SECTORS][512]; long budget; unsigned int operations; };
struct fixture {
    struct disk disk;
    struct object_store store;
    struct object_table_entry entries[TABLE], scratch[TABLE];
    uint8_t cache[65536], bitmap[SECTORS/8], arena[32768];
    struct net_identity identity;
    struct net_session session;
    struct migration migration;
    struct component component;
};
static struct fixture a,b;
static uint8_t baseline[SECTORS][512];
static const uint8_t module_bytes[]={0,0x61,0x73,0x6d,1,0,0,0};

void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *p) { free(p); }
int jani_wasm_instance_create(const uint8_t *bytes,size_t n,void **module,void **instance,void **env,void **owned) {
    *instance=calloc(1,256); *owned=malloc(n);
    if(*instance == NULL || *owned == NULL) { free(*instance); free(*owned); return 0; }
    memcpy(*owned,bytes,n); *module=*instance; *env=*instance; return 1;
}
void jani_wasm_instance_destroy(void *module,void *instance,void *env,void *owned) {
    (void)module; (void)env; free(instance); free(owned);
}
int jani_wasm_instance_memory(void *instance,uint8_t **out,size_t *length) {
    if(instance == NULL) return 0; *out=instance; *length=256; return 1;
}
int jani_wasm_instance_memory_grow(void *instance,size_t length) { return instance != NULL && length <= 256; }
int jani_wasm_instance_call(void *instance,void *env,const char *name) {
    (void)env;
    if(strcmp(name,"jani_init")==0) { initializations++; memset(instance,0,256); }
    return 1;
}
void jani_wasm_set_current_component(struct component *c) { (void)c; }
static int read_sector(void *ctx,uint64_t sector,uint8_t *out) {
    struct disk *d=ctx; if(sector>=SECTORS) return 0; memcpy(out,d->bytes[sector],512); return 1;
}
static int permit(struct disk *d) {
    if(d->budget==0) return 0; if(d->budget>0) d->budget--; d->operations++; return 1;
}
static int write_sector(void *ctx,uint64_t sector,const uint8_t *bytes) {
    struct disk *d=ctx; if(sector>=SECTORS || !permit(d)) return 0;
    memcpy(d->bytes[sector],bytes,512); return 1;
}
static int flush(void *ctx) { return permit(ctx); }
static int mount(struct fixture *f,int format) {
    struct object_store_io io={&f->disk,SECTORS,read_sector,write_sector,flush};
    if(format) return object_store_format(&f->store,io,f->entries,f->scratch,TABLE,
        f->cache,sizeof(f->cache),f->bitmap,sizeof(f->bitmap),f->arena,sizeof(f->arena));
    return object_store_mount(&f->store,io,f->entries,f->scratch,TABLE,
        f->cache,sizeof(f->cache),f->bitmap,sizeof(f->bitmap),f->arena,sizeof(f->arena));
}
static void cleanup(void) {
    migration_close(&a.migration); migration_close(&b.migration);
    if(a.component.instance != NULL) CHECK(component_release(&a.component));
    if(b.component.instance != NULL) CHECK(component_release(&b.component));
}
static void pair(void) {
    cleanup(); memset(&a,0,sizeof(a)); memset(&b,0,sizeof(b));
    a.disk.budget=b.disk.budget=-1; CHECK(mount(&a,1)); CHECK(mount(&b,1));
    /* Public fixtures only; production provisioning uses trusted entropy. */
    uint8_t seed[32]={1}, secret[32]={51};
    CHECK(net_identity_from_seed(&a.identity,seed));
    seed[0]=2; CHECK(net_identity_from_seed(&b.identity,seed));
    CHECK(net_session_init(&a.session,&a.identity,b.identity.public_key,secret));
    secret[1]=1; CHECK(net_session_init(&b.session,&b.identity,a.identity.public_key,secret));
    CHECK(net_session_accept(&a.session,b.session.local_hello,NET_HELLO_BYTES));
    CHECK(net_session_accept(&b.session,a.session.local_hello,NET_HELLO_BYTES));
    CHECK(migration_open(&a.migration,&a.store,&a.session,0));
    CHECK(migration_open(&b.migration,&b.store,&b.session,1));
    CHECK(component_install(&a.store,module_bytes,sizeof(module_bytes),&a.component));
    uint64_t counter=5; memcpy(a.component.instance,&counter,8);
    a.component.logical_time=7; a.component.timer_armed=1; a.component.timer_deadline=8;
    a.component.capability_table.generations[7]=3;
    a.component.capabilities_dirty=1;
    CHECK(component_commit(&a.store,&a.component));
}
static int exchange(int drop_reply) {
    uint8_t packet[NET_PACKET_MAX],reply[NET_PACKET_MAX],unused[NET_PACKET_MAX];
    size_t length=migration_next_packet(&a.migration,packet,sizeof(packet)), response=0, ignored=0;
    CHECK(length != 0);
    int result=migration_receive(&b.migration,packet,length,reply,sizeof(reply),&response);
    if(result == 1 && !drop_reply) {
        CHECK(response != 0);
        result=migration_receive(&a.migration,reply,response,unused,sizeof(unused),&ignored);
        CHECK(ignored == 0);
    }
    return result;
}
static void transfer(void) {
    unsigned int attempts=0;
    while(a.migration.phase != 3 && attempts++<20) CHECK(exchange(attempts%3==0) == 1);
    CHECK(a.migration.phase == 3);
}
static void assertions(void) {
    pair(); struct capability_table saved=a.component.capability_table;
    uint64_t generation=a.store.current_generation;
    uint32_t slot;
    CHECK(component_capability_insert(&a.component,(struct object_id){3,999},CAP_RIGHT_READ,0,&slot));
    CHECK(migration_begin(&a.migration,&a.component) == 0);
    CHECK(a.store.current_generation == generation && !a.component.exited);
    a.component.capability_table=saved; a.component.capability_count=1; a.component.capabilities_dirty=0;
    unsigned int before=initializations;
    CHECK(migration_begin(&a.migration,&a.component) == 1);
    CHECK(a.component.exited && component_is_fenced(&a.store,a.component.root_id));
    struct component blocked={0};
    CHECK(!component_resume(&a.store,a.component.root_id,&blocked));
    CHECK(!component_invoke_timer(&a.component));
    struct object_id roots[COMPONENT_MAX]; size_t count; uint64_t next;
    CHECK(component_registry_load(&a.store,roots,COMPONENT_MAX,&count,&next) && count==0);
    CHECK(exchange(0) == 1);
    CHECK(initializations == before && b.store.table.count==0);
    transfer();
    CHECK(initializations == before);
    CHECK(component_registry_load(&b.store,roots,COMPONENT_MAX,&count,&next) && count==1);
    CHECK(object_id_equal(roots[0],a.migration.record.target));
    CHECK(component_resume(&b.store,roots[0],&b.component));
    CHECK(initializations == before);
    uint64_t counter; memcpy(&counter,b.component.instance,8); CHECK(counter==5);
    CHECK(b.component.logical_time==7 && b.component.timer_armed==1 && b.component.timer_deadline==8);
    CHECK(object_id_equal(b.component.capability_table.slots[0].object,b.component.root_id));
    CHECK(b.component.capability_table.slots[0].rights==(CAP_RIGHT_READ|CAP_RIGHT_SEND));
    CHECK(b.component.capability_table.generations[7]==3);
    CHECK(migration_attach_target(&b.migration,&b.component));
    CHECK(migration_forward(&a.migration,(const uint8_t *)"ping",4) == 1);
    generation=a.store.current_generation;
    CHECK(migration_forward(&a.migration,(const uint8_t *)"busy",4) == 0);
    CHECK(a.store.current_generation==generation);
    CHECK(exchange(1) == 1);
    uint64_t received_generation=b.store.current_generation;
    CHECK(exchange(0) == 1);
    CHECK(b.store.current_generation==received_generation);
    CHECK(b.component.mailbox_used==12);
    uint8_t received[8]; uint32_t received_length; int32_t attachment;
    CHECK(component_mailbox_pop(&b.component,received,sizeof(received),&received_length,&attachment));
    CHECK(received_length==4 && attachment==-1 && memcmp(received,"ping",4)==0);
    CHECK(component_commit(&b.store,&b.component));
    struct object_id original=a.component.root_id;
    migration_close(&a.migration); CHECK(mount(&a,0));
    CHECK(migration_open(&a.migration,&a.store,&a.session,0));
    CHECK(migration_resume_source(&a.migration,original));
    CHECK(a.migration.phase==3 && !object_id_is_zero(a.migration.record.target));
    CHECK(migration_forward(&a.migration,(const uint8_t *)"again",5) == 1);
    CHECK(exchange(0) == 1 && b.component.mailbox_used==13);
    CHECK(!component_resume(&a.store,original,&blocked));
    uint8_t invalid[176]={0};
    struct object_store_put_request corrupt={.id={COMPONENT_FENCE_ID_HIGH,original.low},
        .type_id={0,MIGRATION_TYPE_FENCE},.payload=invalid,.payload_size=sizeof(invalid)};
    CHECK(object_store_put_many(&a.store,&corrupt,1)==OBJECT_STORE_BATCH_COMMITTED);
    CHECK(!component_resume(&a.store,original,&blocked));
}
static void source_crashes(void) {
    pair(); struct object_id root=a.component.root_id;
    memcpy(baseline,a.disk.bytes,sizeof(baseline));
    unsigned int start=a.disk.operations;
    CHECK(migration_begin(&a.migration,&a.component)==1);
    unsigned int operations=a.disk.operations-start;
    for(unsigned int cut=0;cut<=operations;cut++) {
        migration_close(&a.migration); CHECK(component_release(&a.component));
        memcpy(a.disk.bytes,baseline,sizeof(baseline)); a.disk.budget=-1;
        CHECK(mount(&a,0)); CHECK(component_resume(&a.store,root,&a.component));
        CHECK(migration_open(&a.migration,&a.store,&a.session,0));
        a.disk.budget=(long)cut;
        int result=migration_begin(&a.migration,&a.component);
        if(result==-1) CHECK(a.component.exited && object_store_requires_recovery(&a.store));
        a.disk.budget=-1; CHECK(mount(&a,0));
        struct object_id roots[COMPONENT_MAX]; size_t count; uint64_t next;
        CHECK(component_registry_load(&a.store,roots,COMPONENT_MAX,&count,&next));
        int fenced=component_is_fenced(&a.store,root);
        CHECK((count==0)==fenced);
        if(fenced) {
            struct component denied={0}; CHECK(!component_resume(&a.store,root,&denied));
        } else CHECK(count==1 && object_id_equal(roots[0],root));
    }
}
static void target_crashes(void) {
    pair(); CHECK(migration_begin(&a.migration,&a.component)==1);
    memcpy(baseline,b.disk.bytes,sizeof(baseline));
    while(a.migration.received<a.migration.total) CHECK(exchange(0)==1);
    unsigned int start=b.disk.operations;
    CHECK(exchange(1)==1);
    unsigned int operations=b.disk.operations-start;
    CHECK(operations>0);
    for(unsigned int cut=0;cut<=operations;cut++) {
        migration_close(&b.migration);
        memcpy(b.disk.bytes,baseline,sizeof(baseline)); b.disk.budget=-1; CHECK(mount(&b,0));
        CHECK(migration_open(&b.migration,&b.store,&b.session,1));
        a.migration.phase=0; a.migration.received=0;
        while(a.migration.received<a.migration.total) CHECK(exchange(0)==1);
        b.disk.budget=(long)cut;
        int result=exchange(1);
        if(result==-1) CHECK(object_store_requires_recovery(&b.store));
        b.disk.budget=-1; CHECK(mount(&b,0));
        struct object_id roots[COMPONENT_MAX]; size_t count=0; uint64_t next;
        int present=component_registry_load(&b.store,roots,COMPONENT_MAX,&count,&next);
        CHECK((!present && b.store.table.count==0) || (present && count==1 && b.store.table.count==6));
        if(present) {
            struct component resumed={0}; CHECK(component_resume(&b.store,roots[0],&resumed));
            uint64_t counter; memcpy(&counter,resumed.instance,8); CHECK(counter==5);
            CHECK(component_release(&resumed));
        }
        CHECK(component_is_fenced(&a.store,a.component.root_id));
    }
}
static void malformed(void) {
    pair(); CHECK(migration_begin(&a.migration,&a.component)==1);
    struct migration_bundle_view view, saved;
    memset(&view,0xa5,sizeof(view)); saved=view;
    CHECK(!migration_bundle_validate(a.migration.bundle,a.migration.total-1,
        a.identity.public_key,b.identity.public_key,&view));
    CHECK(memcmp(&view,&saved,sizeof(view))==0);
    uint8_t *copy=malloc(a.migration.total); CHECK(copy!=NULL);
    memcpy(copy,a.migration.bundle,a.migration.total);
    copy[a.migration.total-1]^=1;
    CHECK(!migration_bundle_validate(copy,a.migration.total,a.identity.public_key,b.identity.public_key,&view));
    memcpy(copy,a.migration.bundle,a.migration.total); copy[108]=1;
    CHECK(!migration_bundle_validate(copy,a.migration.total,a.identity.public_key,b.identity.public_key,&view));
    CHECK(!migration_bundle_validate(a.migration.bundle,a.migration.total,
        b.identity.public_key,a.identity.public_key,&view));
    free(copy);
    b.migration.allow_incoming=0;
    uint64_t generation=b.store.current_generation;
    CHECK(exchange(1)==0);
    CHECK(b.store.current_generation==generation && b.migration.bundle==NULL);
}
static void prepare_forward(void) {
    pair(); CHECK(migration_begin(&a.migration,&a.component)==1); transfer();
    CHECK(component_resume(&b.store,a.migration.record.target,&b.component));
    CHECK(migration_attach_target(&b.migration,&b.component));
}
static void forward_crashes(void) {
    for(unsigned int stage=0;stage<3;stage++) {
        prepare_forward();
        if(stage>0) CHECK(migration_forward(&a.migration,(const uint8_t *)"ping",4)==1);
        if(stage==2) CHECK(exchange(1)==1);
        struct fixture *f=stage==1?&b:&a;
        memcpy(baseline,f->disk.bytes,sizeof(baseline));
        struct object_id root=f->component.root_id;
        unsigned int start=f->disk.operations;
        if(stage==0) CHECK(migration_forward(&a.migration,(const uint8_t *)"ping",4)==1);
        else CHECK(exchange(stage==1)==1);
        unsigned int operations=f->disk.operations-start; CHECK(operations>0);
        for(unsigned int cut=0;cut<=operations;cut++) {
            migration_close(&f->migration); CHECK(component_release(&f->component));
            memcpy(f->disk.bytes,baseline,sizeof(baseline)); f->disk.budget=-1;
            CHECK(mount(f,0));
            CHECK(migration_open(&f->migration,&f->store,&f->session,stage==1));
            if(stage==1) {
                CHECK(component_resume(&f->store,root,&f->component));
                CHECK(migration_attach_target(&f->migration,&f->component));
            } else CHECK(migration_resume_source(&f->migration,root));
            f->disk.budget=(long)cut;
            int result=stage==0?migration_forward(&a.migration,(const uint8_t *)"ping",4):exchange(stage==1);
            if(result==-1) CHECK(object_store_requires_recovery(&f->store));
            migration_close(&f->migration); CHECK(component_release(&f->component));
            f->disk.budget=-1; CHECK(mount(f,0));
            CHECK(migration_open(&f->migration,&f->store,&f->session,stage==1));
            if(stage==1) {
                CHECK(component_resume(&f->store,root,&f->component));
                CHECK(migration_attach_target(&f->migration,&f->component));
                CHECK(f->component.mailbox_used==0 || f->component.mailbox_used==12);
                uint64_t generation=f->store.current_generation;
                CHECK(exchange(1)==1);
                CHECK(f->component.mailbox_used==12);
                CHECK(exchange(1)==1 && f->component.mailbox_used==12);
                CHECK(f->store.current_generation==generation || f->store.current_generation==generation+1);
            } else {
                CHECK(migration_resume_source(&f->migration,root));
                int pending=migration_forward_pending(&f->migration);
                CHECK(pending==0 || pending==1);
                if(stage==0) CHECK(f->migration.record.forward_sequence==(uint64_t)pending);
                if(stage==2 && pending) {
                    uint64_t generation=b.store.current_generation;
                    CHECK(exchange(0)==1);
                    CHECK(b.store.current_generation==generation && b.component.mailbox_used==12);
                    CHECK(migration_forward_pending(&f->migration)==0);
                }
            }
        }
    }
}
static void reordered_wire(void) {
    pair(); CHECK(migration_begin(&a.migration,&a.component)==1);
    uint8_t packet[NET_PACKET_MAX],body[NET_BODY_MAX],reply[NET_PACKET_MAX]; size_t n=0,response=0;
    uint32_t kind=0; struct net_session decoder=b.session;
    size_t length=migration_next_packet(&a.migration,packet,sizeof(packet));
    CHECK(net_session_open(&decoder,packet,length,body,sizeof(body),&kind,&n));
    body[6]=1;
    length=net_session_seal(&a.session,kind,body,n,packet,sizeof(packet));
    uint64_t generation=b.store.current_generation;
    CHECK(migration_receive(&b.migration,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(b.migration.bundle==NULL && b.store.current_generation==generation);
    CHECK(exchange(0)==1); /* accepted BEGIN */
    decoder=b.session; length=migration_next_packet(&a.migration,packet,sizeof(packet));
    CHECK(net_session_open(&decoder,packet,length,body,sizeof(body),&kind,&n));
    body[64]=1; /* chunk starts beyond the received prefix */
    length=net_session_seal(&a.session,kind,body,n,packet,sizeof(packet));
    CHECK(migration_receive(&b.migration,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(b.migration.received==0 && b.store.current_generation==generation);
    body[64]=0; body[8]=2; /* different owner generation */
    length=net_session_seal(&a.session,kind,body,n,packet,sizeof(packet));
    CHECK(migration_receive(&b.migration,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(b.migration.received==0 && b.store.current_generation==generation);
    transfer(); CHECK(component_resume(&b.store,a.migration.record.target,&b.component));
    CHECK(migration_attach_target(&b.migration,&b.component));
    CHECK(migration_forward(&a.migration,(const uint8_t *)"ping",4)==1);
    decoder=b.session; length=migration_next_packet(&a.migration,packet,sizeof(packet));
    CHECK(net_session_open(&decoder,packet,length,body,sizeof(body),&kind,&n));
    body[80]^=1; /* wrong target root */
    length=net_session_seal(&a.session,kind,body,n,packet,sizeof(packet));
    generation=b.store.current_generation;
    CHECK(migration_receive(&b.migration,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(b.component.mailbox_used==0 && b.store.current_generation==generation);
    CHECK(exchange(0)==1 && b.component.mailbox_used==12);
}
int main(void) {
    assertions(); source_crashes(); target_crashes(); forward_crashes(); reordered_wire(); malformed(); cleanup();
    printf("test_migration: %lu checks passed\n",checks_passed);
    return 0;
}
