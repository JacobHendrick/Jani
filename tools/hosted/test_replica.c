#include "check.h"
#include "../../kernel/net/replica.h"
#include "../../kernel/net/keys.h"
#include "../../kernel/net/peer.h"
#include "../../kernel/net/config.h"
#include "../../kernel/drivers/virtio_net.h"
#include <string.h>

#define SECTORS 512u
#define TABLE 24u

unsigned long checks_passed;
struct disk { uint8_t bytes[SECTORS][512]; long budget; unsigned int operations; };
struct fixture {
    struct disk disk;
    struct object_store store;
    struct object_table_entry entries[TABLE], scratch[TABLE];
    uint8_t cache[8192], bitmap[SECTORS/8], arena[32768];
    struct net_identity identity;
    struct net_session session;
    struct replica replica;
};
static struct fixture a, b;
static uint8_t baseline[SECTORS][512];

static int read_sector(void *context, uint64_t sector, uint8_t *bytes) {
    struct disk *disk = context;
    if (sector >= SECTORS) return 0;
    memcpy(bytes, disk->bytes[sector], 512); return 1;
}
static int permit(struct disk *disk) {
    if (disk->budget == 0) return 0;
    if (disk->budget > 0) disk->budget--;
    disk->operations++; return 1;
}
static int write_sector(void *context, uint64_t sector, const uint8_t *bytes) {
    struct disk *disk = context;
    if (sector >= SECTORS || !permit(disk)) return 0;
    memcpy(disk->bytes[sector], bytes, 512); return 1;
}
static int flush(void *context) { return permit(context); }
static int mount_store(struct fixture *f, int format) {
    struct object_store_io io = {&f->disk, SECTORS, read_sector, write_sector, flush};
    if (format) return object_store_format(&f->store, io, f->entries, f->scratch, TABLE,
        f->cache, sizeof(f->cache), f->bitmap, sizeof(f->bitmap), f->arena, sizeof(f->arena));
    return object_store_mount(&f->store, io, f->entries, f->scratch, TABLE,
        f->cache, sizeof(f->cache), f->bitmap, sizeof(f->bitmap), f->arena, sizeof(f->arena));
}
static void pair(void) {
    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    a.disk.budget = b.disk.budget = -1;
    CHECK(mount_store(&a, 1)); CHECK(mount_store(&b, 1));
    /* Public test fixtures only, never used for real key provisioning. */
    uint8_t seed[32] = {1}, secret[32] = {9};
    CHECK(net_identity_from_seed(&a.identity, seed));
    seed[0] = 2; CHECK(net_identity_from_seed(&b.identity, seed));
    CHECK(net_session_init(&a.session, &a.identity, b.identity.public_key, secret));
    secret[1] = 1;
    CHECK(net_session_init(&b.session, &b.identity, a.identity.public_key, secret));
    CHECK(net_session_accept(&a.session, b.session.local_hello, NET_HELLO_BYTES));
    CHECK(net_session_accept(&b.session, a.session.local_hello, NET_HELLO_BYTES));
    CHECK(replica_open(&a.replica, &a.store, &a.session) == 1);
    CHECK(replica_open(&b.replica, &b.store, &b.session) == 1);
}
static void tick(struct fixture *from, struct fixture *to) {
    uint8_t packet[NET_PACKET_MAX], reply[NET_PACKET_MAX], unused[NET_PACKET_MAX];
    size_t reply_length = 0, ignored = 0;
    size_t length = replica_next_packet(&from->replica, packet, sizeof(packet));
    if (length == 0) return;
    int result = replica_receive(&to->replica, packet, length, reply, sizeof(reply), &reply_length);
    CHECK(result == 1);
    if (reply_length != 0) {
        CHECK(replica_receive(&from->replica, reply, reply_length, unused, sizeof(unused), &ignored) == 1);
        CHECK(ignored == 0);
    }
}
static void sync_pair(void) {
    for (unsigned int i = 0; i < 32; i++) { tick(&a, &b); tick(&b, &a); }
}
static void state_equal(struct object_id id) {
    struct net_crdt left, right;
    CHECK(replica_read(&a.replica, id, &left) == 1);
    CHECK(replica_read(&b.replica, id, &right) == 1);
    CHECK(memcmp(&left, &right, sizeof(left)) == 0);
}
static void convergence(void) {
    pair();
    struct object_id id, set;
    CHECK(replica_create(&a.replica, 1, NET_CRDT_LWW, &id) == 1);
    CHECK(replica_set(&a.replica, id, (const uint8_t *)"initial", 7) == 1);
    CHECK(replica_share(&a.replica, id, REPLICA_READ) == 1);
    sync_pair(); state_equal(id);
    CHECK(replica_set(&b.replica, id, (const uint8_t *)"forbidden", 9) == 0);
    CHECK(replica_share(&b.replica, id, 3) == 0);
    CHECK(replica_set(&a.replica, id, (const uint8_t *)"read-only sync", 14) == 1);
    sync_pair(); state_equal(id);
    CHECK(replica_share(&a.replica, id, 3) == 1);
    sync_pair();
    CHECK(replica_set(&a.replica, id, (const uint8_t *)"offline A", 9) == 1);
    CHECK(replica_set(&b.replica, id, (const uint8_t *)"offline B", 9) == 1);
    sync_pair(); state_equal(id);

    CHECK(replica_create(&a.replica, 2, NET_CRDT_ORSET, &set) == 1);
    CHECK(replica_add(&a.replica, set, 7) == 1);
    CHECK(replica_share(&a.replica, set, 3) == 1);
    sync_pair(); state_equal(set);
    CHECK(replica_remove(&a.replica, set, 7) == 1);
    CHECK(replica_add(&b.replica, set, 7) == 1);
    sync_pair(); state_equal(set);
    struct net_crdt value;
    CHECK(replica_read(&a.replica, set, &value) == 1);
    CHECK(value.count == 2 && value.dots[0].removed != value.dots[1].removed);
    CHECK(replica_remove(&b.replica, set, 7) == 1);
    sync_pair(); state_equal(set);

    uint64_t generation = a.store.current_generation;
    sync_pair(); CHECK(a.store.current_generation == generation);
    CHECK(replica_share(&a.replica, id, 0) == 1);
    sync_pair();
    CHECK(replica_read(&b.replica, id, &value) == 0);
    CHECK(replica_set(&b.replica, id, (const uint8_t *)"revoked", 7) == 0);
    CHECK(mount_store(&b, 0));
    CHECK(replica_open(&b.replica, &b.store, &b.session) == 1);
    CHECK(replica_read(&b.replica, id, &value) == 0);
    CHECK(replica_share(&a.replica, id, 3) == 1);
    sync_pair(); state_equal(id);

    struct net_session wrong = b.session;
    wrong.pinned_peer[0] ^= 1;
    struct replica untouched = b.replica;
    CHECK(replica_open(&b.replica, &b.store, &wrong) == 0);
    CHECK(memcmp(&b.replica, &untouched, sizeof(untouched)) == 0);
}
static void crash_create(void) {
    pair();
    memcpy(baseline, a.disk.bytes, sizeof(baseline));
    unsigned int start = a.disk.operations;
    struct object_id id;
    CHECK(replica_create(&a.replica, 1, NET_CRDT_LWW, &id) == 1);
    unsigned int operations = a.disk.operations - start;
    CHECK(operations > 0);
    for (unsigned int cut = 0; cut <= operations; cut++) {
        memcpy(a.disk.bytes, baseline, sizeof(baseline));
        a.disk.budget = -1; CHECK(mount_store(&a, 0));
        CHECK(replica_open(&a.replica, &a.store, &a.session) == 1);
        a.disk.budget = (long)cut;
        struct object_id output = {123, 456};
        int result = replica_create(&a.replica, 1, NET_CRDT_LWW, &output);
        if (result != 1) CHECK(output.high == 123 && output.low == 456);
        if (result == -1) {
            CHECK(object_store_requires_recovery(&a.store));
            CHECK(replica_create(&a.replica, 2, NET_CRDT_LWW, &output) == -1);
        }
        a.disk.budget = -1;
        CHECK(mount_store(&a, 0)); CHECK(replica_open(&a.replica, &a.store, &a.session) == 1);
        CHECK(a.replica.catalog.count <= 1);
        struct object_id expected = {a.replica.local_namespace, 1};
        CHECK((object_table_find(&a.store.table, expected) != NULL) == (a.replica.catalog.count == 1));
        if (a.replica.catalog.count == 1) {
            struct net_crdt state; CHECK(replica_read(&a.replica, expected, &state) == 1);
            CHECK(state.kind == NET_CRDT_LWW && state.timestamp == 0);
        }
    }
}
static void crash_update(void) {
    pair();
    struct object_id id;
    CHECK(replica_create(&a.replica, 1, NET_CRDT_LWW, &id) == 1);
    CHECK(replica_set(&a.replica, id, (const uint8_t *)"old", 3) == 1);
    memcpy(baseline, a.disk.bytes, sizeof(baseline));
    unsigned int start = a.disk.operations;
    CHECK(replica_set(&a.replica, id, (const uint8_t *)"new", 3) == 1);
    unsigned int operations = a.disk.operations - start;
    for (unsigned int cut = 0; cut <= operations; cut++) {
        memcpy(a.disk.bytes, baseline, sizeof(baseline));
        a.disk.budget = -1; CHECK(mount_store(&a, 0));
        CHECK(replica_open(&a.replica, &a.store, &a.session) == 1);
        a.disk.budget = (long)cut;
        int result = replica_set(&a.replica, id, (const uint8_t *)"new", 3);
        if (result == -1) {
            struct net_crdt unused;
            CHECK(replica_read(&a.replica, id, &unused) == -1);
        }
        a.disk.budget = -1; CHECK(mount_store(&a, 0));
        CHECK(replica_open(&a.replica, &a.store, &a.session) == 1);
        struct net_crdt state;
        CHECK(replica_read(&a.replica, id, &state) == 1);
        CHECK((memcmp(state.value, "old", 3) == 0 && state.timestamp == 1 &&
               state.vector[a.replica.local_node] == 1) ||
              (memcmp(state.value, "new", 3) == 0 && state.timestamp == 2 &&
               state.vector[a.replica.local_node] == 2));
    }
}
static void limits(void) {
    pair(); struct object_id id;
    for (uint64_t i = 1; i <= REPLICA_OBJECTS_MAX; i++)
        CHECK(replica_create(&a.replica, i, NET_CRDT_LWW, &id) == 1);
    uint64_t generation = a.store.current_generation;
    CHECK(replica_create(&a.replica, 9, NET_CRDT_LWW, &id) == 0);
    CHECK(a.store.current_generation == generation);
    uint8_t bytes[REPLICA_CATALOG_BYTES], before[REPLICA_CATALOG_BYTES];
    CHECK(replica_catalog_encode(bytes, sizeof(bytes), a.identity.public_key,
        b.identity.public_key, &a.replica.catalog) == sizeof(bytes));
    struct replica_catalog catalog = a.replica.catalog, saved = catalog;
    CHECK(!replica_catalog_decode(bytes, sizeof(bytes)-1, a.identity.public_key,
        b.identity.public_key, &catalog)); CHECK(memcmp(&catalog, &saved, sizeof(catalog)) == 0);
    bytes[5] = 1;
    CHECK(!replica_catalog_decode(bytes, sizeof(bytes), a.identity.public_key,
        b.identity.public_key, &catalog)); CHECK(memcmp(&catalog, &saved, sizeof(catalog)) == 0);
    memcpy(before, bytes, sizeof(bytes)); catalog.count = 9;
    CHECK(!replica_catalog_encode(bytes, sizeof(bytes), a.identity.public_key,
        b.identity.public_key, &catalog)); CHECK(memcmp(bytes, before, sizeof(bytes)) == 0);
}

struct entropy_state { unsigned int calls; int fail; uint8_t seed[32]; };
static int entropy(void *context, uint8_t *out, size_t length) {
    struct entropy_state *state = context;
    state->calls++;
    if (state->fail || length != 32) return 0;
    memcpy(out, state->seed, length); return 1;
}
static void persisted_identity(void) {
    pair();
    struct entropy_state random = {.seed = {31}};
    struct net_identity identity, expected;
    CHECK(net_identity_from_seed(&expected, random.seed));
    memcpy(baseline, a.disk.bytes, sizeof(baseline));
    unsigned int start = a.disk.operations;
    CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 1);
    CHECK(random.calls == 1 && memcmp(&identity, &expected, sizeof(identity)) == 0);
    unsigned int operations = a.disk.operations - start;
    CHECK(mount_store(&a, 0));
    CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 1);
    CHECK(random.calls == 1 && memcmp(&identity, &expected, sizeof(identity)) == 0);
    for (unsigned int cut = 0; cut <= operations; cut++) {
        memcpy(a.disk.bytes, baseline, sizeof(baseline));
        a.disk.budget = -1; CHECK(mount_store(&a, 0));
        memset(&identity, 0xa5, sizeof(identity));
        struct net_identity sentinel = identity;
        a.disk.budget = (long)cut;
        int result = net_identity_open(&a.store, entropy, &random, &identity);
        if (result != 1) CHECK(memcmp(&identity, &sentinel, sizeof(identity)) == 0);
        if (result == -1) {
            CHECK(net_identity_open(&a.store, entropy, &random, &identity) == -1);
            CHECK(memcmp(&identity, &sentinel, sizeof(identity)) == 0);
        }
        a.disk.budget = -1; CHECK(mount_store(&a, 0));
        struct object_id id = {NET_KEYS_HIGH, NET_KEYS_LOW};
        if (object_table_find(&a.store.table, id) != NULL) {
            unsigned int calls = random.calls;
            CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 1);
            CHECK(calls == random.calls && memcmp(&identity, &expected, sizeof(identity)) == 0);
        } else CHECK(net_identity_open(&a.store, NULL, NULL, &identity) == 0);
    }
    pair(); random.fail = 1;
    memset(&identity, 0xa5, sizeof(identity));
    struct net_identity sentinel = identity;
    uint64_t generation = a.store.current_generation;
    CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 0);
    CHECK(generation == a.store.current_generation &&
        memcmp(&identity, &sentinel, sizeof(identity)) == 0);
    random.fail = 0;
    CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 1);
    uint8_t bad[40] = {0};
    struct object_store_put_request request = {
        .id = {NET_KEYS_HIGH, NET_KEYS_LOW}, .type_id = {0, NET_KEYS_TYPE},
        .payload = bad, .payload_size = sizeof(bad)};
    CHECK(object_store_put_many(&a.store, &request, 1) == OBJECT_STORE_BATCH_COMMITTED);
    CHECK(mount_store(&a, 0));
    unsigned int calls = random.calls;
    sentinel = identity;
    CHECK(net_identity_open(&a.store, entropy, &random, &identity) == 0);
    CHECK(random.calls == calls && memcmp(&identity, &sentinel, sizeof(identity)) == 0);
    struct net_session session = a.session, saved = session;
    random.fail = 1;
    CHECK(!net_session_random(&session, &a.identity, b.identity.public_key, entropy, &random));
    CHECK(memcmp(&session, &saved, sizeof(session)) == 0);
    random.fail = 0;
    CHECK(net_session_random(&session, &a.identity, b.identity.public_key, entropy, &random));
    uint8_t seed[32]; memset(seed, 0xa5, sizeof(seed));
    CHECK(!net_seed_decode(NULL, 40, seed));
    CHECK(!net_seed_decode(bad, 39, seed));
    CHECK(!net_seed_decode(bad, 40, seed));
    for (size_t i = 0; i < sizeof(seed); i++) CHECK(seed[i] == 0xa5);
}


static uint8_t wire[2][1514];
static size_t wire_length[2];
static unsigned int selected, borrowed, blocked;
int virtio_net_is_ready(void) { return 1; }
enum virtio_net_transmit_result virtio_net_poll_transmit(void) { return VIRTIO_NET_TRANSMIT_IDLE; }
enum virtio_net_send_result virtio_net_send(const uint8_t *frame, size_t length) {
    if (blocked || wire_length[1-selected] != 0) return VIRTIO_NET_SEND_BUSY;
    CHECK(length <= sizeof(wire[0]));
    memcpy(wire[1-selected], frame, length); wire_length[1-selected] = length;
    return VIRTIO_NET_SEND_SUBMITTED;
}
enum virtio_net_receive_result virtio_net_poll(struct jani_udp_datagram *out) {
    if (wire_length[selected] == 0) return VIRTIO_NET_RECEIVE_EMPTY;
    CHECK(!borrowed);
    if (!jani_udp_frame_decode(wire[selected], wire_length[selected], out)) {
        wire_length[selected] = 0; return VIRTIO_NET_RECEIVE_DROPPED;
    }
    borrowed = 1; return VIRTIO_NET_RECEIVE_PACKET;
}
const uint8_t *virtio_net_received_frame(void) { return borrowed ? wire[selected] : NULL; }
size_t virtio_net_received_frame_length(void) { return borrowed ? wire_length[selected] : 0; }
int virtio_net_release_receive(void) {
    CHECK(borrowed); borrowed = 0; wire_length[selected] = 0; return 1;
}
static unsigned int extension_sends;
static int extension_receive(void *ctx,const uint8_t *packet,size_t n,uint8_t *reply,size_t cap,size_t *out) {
    (void)ctx;(void)packet;(void)n;(void)reply;(void)cap;*out=0;return 0;
}
static size_t extension_send(void *ctx,uint8_t *out,size_t capacity) {
    extension_sends++;
    return net_session_seal(ctx,NET_MESSAGE_MIGRATION,NULL,0,out,capacity);
}
static void live_transport(void) {
    pair();
    struct net_peer left, right;
    struct jani_udp_frame_fields route = {
        .source_mac = {0x52,0x54,0,0,0,1}, .destination_mac = {0x52,0x54,0,0,0,2},
        .source_ip = {10,0,0,1}, .destination_ip = {10,0,0,2},
        .source_port = 5555, .destination_port = 5555, .ttl = 64};
    uint8_t secret[32] = {51};
    CHECK(net_peer_start(&left, &a.store, &a.identity, b.identity.public_key, secret, &route));
    secret[1] = 1;
    uint8_t swap[6];
    memcpy(swap, route.source_mac, 6);
    memcpy(route.source_mac, route.destination_mac, 6);
    memcpy(route.destination_mac, swap, 6);
    route.source_ip[3] = 2; route.destination_ip[3] = 1;
    CHECK(net_peer_start(&right, &b.store, &b.identity, a.identity.public_key, secret, &route));
    struct object_id id;
    CHECK(replica_create(&left.replica, 1, NET_CRDT_LWW, &id) == 1);
    CHECK(replica_set(&left.replica, id, (const uint8_t *)"wire", 4) == 1);
    CHECK(replica_share(&left.replica, id, 3) == 1);
    wire_length[0] = wire_length[1] = borrowed = 0;
    selected = 0; blocked = 1;
    CHECK(net_peer_step(&left, 0) == 1 && left.pending_length == NET_HELLO_BYTES);
    uint8_t pending[NET_PACKET_MAX]; memcpy(pending, left.pending, sizeof(pending));
    CHECK(net_peer_step(&left, 1) == 1 && memcmp(pending, left.pending, sizeof(pending)) == 0);
    blocked = 0;
    for (uint64_t time = 2; time < 800; time++) {
        selected = 0; CHECK(net_peer_step(&left, time) == 1);
        selected = 1; CHECK(net_peer_step(&right, time) == 1);
        CHECK(!borrowed);
    }
    CHECK(left.session.active && right.session.active);
    struct net_crdt state;
    CHECK(replica_read(&right.replica, id, &state) == 1);
    CHECK(state.length == 4 && memcmp(state.value, "wire", 4) == 0);
    CHECK(replica_set(&right.replica, id, (const uint8_t *)"edit", 4) == 1);
    for (uint64_t time = 800; time < 1000; time++) {
        selected = 0; CHECK(net_peer_step(&left, time) == 1);
        selected = 1; CHECK(net_peer_step(&right, time) == 1);
    }
    CHECK(replica_read(&left.replica, id, &state) == 1 && memcmp(state.value, "edit", 4) == 0);
    wire_length[0]=wire_length[1]=0; selected=0; extension_sends=0;
    CHECK(net_peer_extension(&left,&left.session,extension_receive,extension_send));
    left.summary_tick=0;
    CHECK(net_peer_step(&left,1000)==1 && extension_sends==1);
    for(uint64_t time=1001;time<1100;time++) {
        CHECK(net_peer_step(&left,time)==1);
        CHECK(extension_sends==1);
    }
    CHECK(net_peer_step(&left,1100)==1 && extension_sends==2);
    struct net_boot_config config, sentinel; memset(&config, 0xa5, sizeof(config)); sentinel = config;
    uint8_t bytes[104] = {0}; memcpy(bytes, "JN5C", 4); bytes[4] = 1;
    memcpy(bytes + 8, secret, 32); memcpy(bytes + 40, b.identity.public_key, 32);
    memcpy(bytes + 72, a.identity.public_key, 32);
    CHECK(net_config_decode(bytes, sizeof(bytes), &config));
    CHECK(config.role == 0 && memcmp(config.local_key, a.identity.public_key, 32) == 0);
    config = sentinel; bytes[6] = 1;
    CHECK(!net_config_decode(bytes, sizeof(bytes), &config));
    CHECK(memcmp(&config, &sentinel, sizeof(config)) == 0);
    CHECK(!net_config_decode(bytes, sizeof(bytes)-1, &config));
    wire_length[0] = wire_length[1] = 0;
}

static void crash_grant(void) {
    for (unsigned int revoke=0;revoke<2;revoke++) {
        pair(); struct object_id id;
        CHECK(replica_create(&a.replica,1,NET_CRDT_LWW,&id)==1);
        CHECK(replica_share(&a.replica,id,3)==1);
        if(revoke) { sync_pair(); CHECK(replica_share(&a.replica,id,0)==1); }
        memcpy(baseline,b.disk.bytes,sizeof(baseline));
        uint8_t packet[NET_PACKET_MAX],reply[NET_PACKET_MAX]; size_t response=0;
        a.replica.cursor=0; a.replica.send_grant=1;
        size_t length=replica_next_packet(&a.replica,packet,sizeof(packet)); CHECK(length!=0);
        unsigned int start=b.disk.operations;
        CHECK(replica_receive(&b.replica,packet,length,reply,sizeof(reply),&response)==1);
        unsigned int operations=b.disk.operations-start; CHECK(operations>0);
        for(unsigned int cut=0;cut<=operations;cut++) {
            memcpy(b.disk.bytes,baseline,sizeof(baseline)); b.disk.budget=-1;
            CHECK(mount_store(&b,0)); CHECK(replica_open(&b.replica,&b.store,&b.session)==1);
            a.replica.cursor=0; a.replica.send_grant=1;
            length=replica_next_packet(&a.replica,packet,sizeof(packet)); CHECK(length!=0);
            b.disk.budget=(long)cut;
            int result=replica_receive(&b.replica,packet,length,reply,sizeof(reply),&response);
            if(result==-1) CHECK(object_store_requires_recovery(&b.store));
            b.disk.budget=-1; CHECK(mount_store(&b,0));
            CHECK(replica_open(&b.replica,&b.store,&b.session)==1);
            if(revoke) {
                CHECK(b.replica.catalog.count==1);
                const struct replica_scope *scope=&b.replica.catalog.scopes[0];
                CHECK((scope->rights==3 && scope->epoch==2) || (scope->rights==0 && scope->epoch==3));
            } else {
                CHECK(b.replica.catalog.count<=1);
                CHECK((object_table_find(&b.store.table,id)!=NULL)==(b.replica.catalog.count==1));
            }
        }
    }
}
static void hostile_peer(void) {
    pair(); struct object_id id;
    CHECK(replica_create(&a.replica,1,NET_CRDT_LWW,&id)==1);
    CHECK(replica_share(&a.replica,id,3)==1); sync_pair();
    uint8_t body[32+NET_CRDT_BYTES]={0},packet[NET_PACKET_MAX],reply[NET_PACKET_MAX];
    memcpy(body,"JR5O",4); body[4]=1; body[5]=3; body[6]=1; body[7]=3;
    memcpy(body+8,&id,sizeof(id)); uint64_t epoch=99; memcpy(body+24,&epoch,8);
    size_t length=net_session_seal(&b.session,NET_MESSAGE_GRANT,body,32,packet,sizeof(packet));
    size_t response=0; uint64_t generation=a.store.current_generation;
    CHECK(replica_receive(&a.replica,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(a.store.current_generation==generation); /* non-owner cannot mint grants */
    struct object_id native={2,1}; memcpy(body+8,&native,sizeof(native));
    length=net_session_seal(&a.session,NET_MESSAGE_GRANT,body,32,packet,sizeof(packet));
    generation=b.store.current_generation;
    CHECK(replica_receive(&b.replica,packet,length,reply,sizeof(reply),&response)==0);
    CHECK(b.store.current_generation==generation); /* protected native IDs */
    CHECK(replica_share(&a.replica,id,REPLICA_READ)==1); sync_pair();
    struct net_crdt value; CHECK(replica_read(&b.replica,id,&value)==1);
    CHECK(net_crdt_set(&value,b.replica.local_node,(const uint8_t *)"forged",6));
    memset(body,0,32); memcpy(body,"JR5O",4); body[4]=1;body[5]=2;body[6]=1;
    memcpy(body+8,&id,sizeof(id));
    CHECK(net_crdt_encode(body+32,NET_CRDT_BYTES,&value)==NET_CRDT_BYTES);
    for(epoch=2;epoch<=3;epoch++) {
        memcpy(body+24,&epoch,8);
        length=net_session_seal(&b.session,NET_MESSAGE_UPDATE,body,sizeof(body),packet,sizeof(packet));
        generation=a.store.current_generation;
        CHECK(replica_receive(&a.replica,packet,length,reply,sizeof(reply),&response)==0);
        CHECK(a.store.current_generation==generation); /* stale epoch or absent WRITE */
    }
}
int main(void) {
    convergence(); hostile_peer(); crash_grant(); crash_create(); crash_update(); limits(); persisted_identity(); live_transport();
    printf("test_replica: %lu checks passed\n", checks_passed);
    return 0;
}
