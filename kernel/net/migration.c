#include "migration.h"
#include "../cap/domain.h"
#include "../lib/string.h"
#include "../mm/heap.h"
#include "../wasm/instance_state.h"
#include "../../third_party/monocypher/src/monocypher.h"

enum { BEGIN = 1, CHUNK, END, FORWARD, ACK, FORWARD_ACK };
static int equal(const void *left, const void *right, size_t length) {
    const uint8_t *a = left, *b = right;
    for (size_t i = 0; i < length; i++) if (a[i] != b[i]) return 0;
    return 1;
}
static void put32(uint8_t *p, uint32_t v) { for (size_t i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void put64(uint8_t *p, uint64_t v) { for (size_t i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static uint64_t get64(const uint8_t *p) { uint64_t v=0; for(size_t i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v; }
static void put_id(uint8_t *p, struct object_id id) { put64(p,id.high); put64(p+8,id.low); }
static struct object_id fence_id(struct object_id root) { return (struct object_id){COMPONENT_FENCE_ID_HIGH,root.low}; }
static struct object_id receipt_id(struct object_id root) { return (struct object_id){MIGRATION_RECEIPT_HIGH,root.low}; }
static struct object_id outbox_id(struct object_id root) { return (struct object_id){MIGRATION_OUTBOX_HIGH,root.low}; }
static struct object_id registry_id(void) { return (struct object_id){COMPONENT_REGISTRY_ID_HIGH,COMPONENT_REGISTRY_ID_LOW}; }
static int healthy(struct migration *m) {
    if (m == NULL || m->store == NULL || m->session == NULL || m->halted) return 0;
    if (object_store_requires_recovery(m->store)) { m->halted=1; return 0; }
    return 1;
}
static int rejected(struct migration *m) { return m != NULL && m->halted ? -1 : 0; }
static int commit(struct migration *m, const struct object_store_put_request *r, size_t n) {
    enum object_store_batch_result status=object_store_put_many(m->store,r,n);
    if (status == OBJECT_STORE_BATCH_RECOVERY_REQUIRED) { m->halted=1; return -1; }
    return status == OBJECT_STORE_BATCH_COMMITTED;
}
static int load(struct migration *m, struct object_id id, uint64_t type,
                const uint8_t **bytes, size_t *length) {
    struct object_header h;
    return object_store_get(m->store,id,&h,bytes,length) &&
        h.type_id.high == 0 && h.type_id.low == type;
}
static void encode_record(uint8_t b[MIGRATION_RECORD_BYTES], const struct migration_record *r) {
    memset(b,0,MIGRATION_RECORD_BYTES); memcpy(b,"JF5M",4); b[4]=1;
    memcpy(b+8,r->peer,32); memcpy(b+40,r->digest,32); put_id(b+72,r->root);
    put_id(b+88,r->module); put_id(b+104,r->state); put_id(b+120,r->captable);
    put64(b+136,r->generation); put_id(b+144,r->target); put64(b+160,r->forward_sequence);
}
static void encode_receipt(uint8_t b[MIGRATION_RECEIPT_BYTES], const struct migration_receipt *r) {
    memset(b,0,MIGRATION_RECEIPT_BYTES); memcpy(b,"JI5M",4); b[4]=1;
    memcpy(b+8,r->peer,32); memcpy(b+40,r->digest,32); put_id(b+72,r->source);
    put64(b+88,r->generation); put_id(b+96,r->target); put64(b+112,r->forward_sequence);
}
static size_t encode_registry(uint8_t *b, const struct object_id *roots, size_t count, uint64_t next) {
    struct component_registry_header header={0};
    size_t total=COMPONENT_REGISTRY_HEADER_SIZE+count*sizeof(struct object_id);
    header.magic=COMPONENT_REGISTRY_MAGIC; header.format_version=COMPONENT_REGISTRY_FORMAT_VERSION;
    header.component_count=(uint32_t)count; header.next_sequence=next;
    memcpy(b+COMPONENT_REGISTRY_HEADER_SIZE,roots,count*sizeof(struct object_id));
    header.payload_crc32c=object_crc32c(b+COMPONENT_REGISTRY_HEADER_SIZE,total-COMPONENT_REGISTRY_HEADER_SIZE);
    memcpy(b,&header,sizeof(header)); return total;
}
static int registry(struct migration *m, struct object_id roots[COMPONENT_MAX], size_t *count, uint64_t *next) {
    if (!component_registry_load(m->store,roots,COMPONENT_MAX,count,next)) {
        if (object_table_find(&m->store->table,registry_id()) != NULL) return 0;
        *count=0; *next=1;
    }
    if (*next == 0) return 0;
    for (size_t i=0;i<*count;i++) {
        if (roots[i].high != COMPONENT_SEQUENCE_ID_HIGH || roots[i].low == 0 || roots[i].low >= *next) return 0;
        for (size_t j=0;j<i;j++) if(object_id_equal(roots[i],roots[j])) return 0;
    }
    return 1;
}
static void header(uint8_t *b, const struct migration_wire_view *v) {
    memset(b,0,MIGRATION_WIRE_BYTES); memcpy(b,"JM5H",4); b[4]=1; b[5]=(uint8_t)v->opcode;
    put64(b+8,v->generation); memcpy(b+16,v->digest,32); put_id(b+48,v->root);
    put32(b+64,v->offset); put32(b+68,v->total); put_id(b+72,v->target);
}
static int matching(const struct migration *m, const struct migration_wire_view *v) {
    return object_id_equal(m->record.root,v->root) && m->record.generation == v->generation &&
        equal(m->record.digest,v->digest,32) && m->total == v->total;
}
static int find_receipt(struct migration *m, const struct migration_wire_view *v, struct migration_receipt *out) {
    for(size_t i=0;i<m->store->table.count;i++) {
        struct object_id id=m->store->table.entries[i].id;
        if(id.high != MIGRATION_RECEIPT_HIGH) continue;
        const uint8_t *b; size_t n; struct migration_receipt r;
        if (!load(m,id,MIGRATION_TYPE_RECEIPT,&b,&n) || !migration_receipt_decode(b,n,&r) ||
            r.target.low != id.low) return -1;
        if (equal(r.peer,m->session->pinned_peer,32) && object_id_equal(r.source,v->root) &&
            r.generation == v->generation && equal(r.digest,v->digest,32)) { *out=r; return 1; }
    }
    return 0;
}
static void free_bundle(struct migration *m) {
    if(m->bundle != NULL) { crypto_wipe(m->bundle,m->total); kfree(m->bundle); m->bundle=NULL; }
    m->received=0;
}
int migration_open(struct migration *m, struct object_store *store, struct net_session *session, int allow) {
    if(m == NULL || store == NULL || session == NULL || session->identity == NULL ||
        (allow != 0 && allow != 1) || object_store_requires_recovery(store)) return 0;
    memset(m,0,sizeof(*m)); m->store=store; m->session=session; m->allow_incoming=(uint32_t)allow; return 1;
}
static int build_bundle(struct migration *m, int verify) {
    const struct object_id ids[3]={m->record.module,m->record.captable,m->record.state};
    const uint64_t types[3]={COMPONENT_TYPE_MODULE,COMPONENT_TYPE_CAPTABLE,COMPONENT_TYPE_INSTANCE_STATE};
    size_t sizes[3], total=MIGRATION_HEADER_BYTES;
    for(size_t i=0;i<3;i++) {
        const uint8_t *b;
        if(!load(m,ids[i],types[i],&b,&sizes[i]) || sizes[i] > MIGRATION_MAX_BYTES-total) return 0;
        total+=sizes[i];
    }
    uint8_t *bundle=kmalloc(total);
    if(bundle == NULL) return 0;
    memset(bundle,0,MIGRATION_HEADER_BYTES); memcpy(bundle,"JB5M",4); bundle[4]=1;
    memcpy(bundle+8,m->session->identity->public_key,32); memcpy(bundle+40,m->session->pinned_peer,32);
    put_id(bundle+72,m->record.root); put64(bundle+88,m->record.generation);
    put32(bundle+96,(uint32_t)sizes[0]); put32(bundle+100,(uint32_t)sizes[2]); put32(bundle+104,(uint32_t)sizes[1]);
    size_t offset=MIGRATION_HEADER_BYTES;
    for(size_t i=0;i<3;i++) {
        const uint8_t *b; size_t n;
        if(!load(m,ids[i],types[i],&b,&n) || n != sizes[i]) { crypto_wipe(bundle,total); kfree(bundle); return 0; }
        memcpy(bundle+offset,b,n); offset+=n;
    }
    struct migration_bundle_view view; uint8_t hash[32];
    int valid=migration_bundle_validate(bundle,total,m->session->identity->public_key,
                                       m->session->pinned_peer,&view);
    crypto_blake2b(hash,32,bundle,total);
    if(!valid || (verify && !equal(hash,m->record.digest,32))) { crypto_wipe(bundle,total); kfree(bundle); return 0; }
    if(!verify) memcpy(m->record.digest,hash,32);
    m->bundle=bundle; m->total=total; m->received=0; return 1;
}
int migration_resume_source(struct migration *m, struct object_id root) {
    if(!healthy(m)) return rejected(m);
    if(m->bundle != NULL || root.high != COMPONENT_SEQUENCE_ID_HIGH || root.low == 0) return 0;
    const uint8_t *b; size_t n; struct migration_record r;
    if(!load(m,fence_id(root),MIGRATION_TYPE_FENCE,&b,&n) || !migration_record_decode(b,n,&r) ||
       !object_id_equal(root,r.root) || !equal(r.peer,m->session->pinned_peer,32)) return 0;
    m->record=r;
    if(!build_bundle(m,1)) return 0;
    m->source=1; m->phase=object_id_is_zero(r.target)?0:3; return 1;
}
int migration_begin(struct migration *m, struct component *c) {
    if(!healthy(m)) return rejected(m);
    if(c == NULL || c->store != m->store || c->instance == NULL || c->exited || m->bundle != NULL ||
       c->root_id.high != COMPONENT_SEQUENCE_ID_HIGH ||
       object_table_find(&m->store->table,fence_id(c->root_id)) != NULL) return 0;
    if (!capability_table_is_valid(&c->capability_table) ||
        c->module_id.high != COMPONENT_SEQUENCE_ID_HIGH ||
        c->captable_id.high != COMPONENT_SEQUENCE_ID_HIGH ||
        c->state_id.high != COMPONENT_SEQUENCE_ID_HIGH) return 0;
    for (uint32_t slot = 0; slot < CAP_TABLE_SLOTS; slot++) {
        const struct capability *cap = capability_table_get(&c->capability_table, slot);
        if (cap != NULL && (!object_id_equal(cap->object, c->root_id) ||
            (cap->rights & ~(CAP_RIGHT_READ | CAP_RIGHT_SEND)) != 0 ||
            c->capability_table.parents[slot] != CAP_SLOT_NONE)) return 0;
    }
    if(c->domain != NULL) {
        struct capability_domain *d=c->domain;
        if(d->active || d->halted) return 0;
        for(uint32_t i=0;i<d->lineage.count;i++)
            if(object_id_equal(d->lineage.records[i].parent.component_id,c->root_id) ||
               object_id_equal(d->lineage.records[i].child.component_id,c->root_id)) return 0;
        for(uint32_t i=0;i<COMPONENT_MAX;i++) {
            const struct component *other=d->components->items[i];
            if(other == NULL || other == c) continue;
            for(uint32_t slot=0;slot<CAP_TABLE_SLOTS;slot++)
                if(object_id_equal(other->capability_table.slots[slot].object,c->root_id)) return 0;
        }
    }
    struct object_id roots[COMPONENT_MAX]; size_t count, index; uint64_t next;
    if(!registry(m,roots,&count,&next)) return 0;
    for(index=0;index<count;index++) if(object_id_equal(roots[index],c->root_id)) break;
    if(index == count) return 0;
    if(!component_commit(m->store,c)) {
        if(object_store_requires_recovery(m->store)) { m->halted=1; c->exited=1; }
        return rejected(m);
    }
    memset(&m->record,0,sizeof(m->record)); memcpy(m->record.peer,m->session->pinned_peer,32);
    m->record.root=c->root_id; m->record.module=c->module_id; m->record.state=c->state_id;
    m->record.captable=c->captable_id; m->record.generation=1;
    if(object_table_find(&m->store->table,receipt_id(c->root_id)) != NULL) {
        const uint8_t *b; size_t n; struct migration_receipt prior;
        if(!load(m,receipt_id(c->root_id),MIGRATION_TYPE_RECEIPT,&b,&n) ||
           !migration_receipt_decode(b,n,&prior) || !object_id_equal(prior.target,c->root_id) ||
           prior.generation == UINT64_MAX) return 0;
        m->record.generation=prior.generation+1;
    }
    if(!build_bundle(m,0)) return 0;
    for(size_t i=index;i+1<count;i++) roots[i]=roots[i+1];
    uint8_t catalog[COMPONENT_REGISTRY_HEADER_SIZE+COMPONENT_MAX*sizeof(struct object_id)], record[MIGRATION_RECORD_BYTES];
    size_t length=encode_registry(catalog,roots,count-1,next);
    encode_record(record,&m->record);
    struct object_store_put_request requests[2]={
        {.id=registry_id(),.type_id={0,COMPONENT_TYPE_REGISTRY},.payload=catalog,.payload_size=length},
        {.id=fence_id(c->root_id),.type_id={0,MIGRATION_TYPE_FENCE},.payload=record,.payload_size=sizeof(record)}};
    int result=commit(m,requests,2);
    if(result != 1) { if(result == -1) c->exited=1; free_bundle(m); return result; }
    c->exited=1;
    if(c->domain != NULL) (void)component_set_remove(c->domain->components,c->root_id);
    m->source=1; m->phase=0; return 1;
}
static int publish_import(struct migration *m, const struct migration_wire_view *wire) {
    uint8_t digest[32]; struct migration_bundle_view view;
    crypto_blake2b(digest,32,m->bundle,m->total);
    if(!equal(digest,wire->digest,32) ||
       !migration_bundle_validate(m->bundle,m->total,m->session->pinned_peer,m->session->identity->public_key,&view) ||
       !object_id_equal(view.root,wire->root) || view.generation != wire->generation) return 0;
    struct object_id roots[COMPONENT_MAX]; size_t count; uint64_t next;
    if(!registry(m,roots,&count,&next) || count == COMPONENT_MAX || next > UINT64_MAX-4) return 0;
    struct object_id module={2,next}, captable={2,next+1}, state={2,next+2}, root={2,next+3};
    struct object_id ids[4]={module,captable,state,root};
    for(size_t i=0;i<4;i++) if(object_table_find(&m->store->table,ids[i]) != NULL) return 0;
    if(object_table_find(&m->store->table,receipt_id(root)) != NULL ||
       object_table_find(&m->store->table,fence_id(root)) != NULL) return 0;
    uint8_t caps[COMPONENT_CAPTABLE_BYTES];
    memcpy(caps,m->bundle+MIGRATION_HEADER_BYTES+view.module_length,sizeof(caps));
    for(size_t slot=0;slot<CAP_TABLE_SLOTS;slot++) {
        struct object_id id;
        memcpy(&id,caps+32+slot*sizeof(struct capability),sizeof(id));
        if(!object_id_is_zero(id)) memcpy(caps+32+slot*sizeof(struct capability),&root,sizeof(root));
    }
    uint32_t crc=object_crc32c(caps+32,sizeof(caps)-32); memcpy(caps+24,&crc,4);
    struct component_root_record root_record={0};
    root_record.magic=COMPONENT_ROOT_MAGIC; root_record.format_version=COMPONENT_ROOT_FORMAT_VERSION;
    root_record.module_id=module; root_record.captable_id=captable; root_record.state_id=state;
    root_record.payload_crc32c=object_crc32c((const uint8_t *)&root_record.module_id,48);
    uint8_t catalog[COMPONENT_REGISTRY_HEADER_SIZE+COMPONENT_MAX*sizeof(struct object_id)], receipt[MIGRATION_RECEIPT_BYTES];
    roots[count++]=root; size_t catalog_size=encode_registry(catalog,roots,count,next+4);
    struct migration_receipt r={.source=wire->root,.generation=wire->generation,.target=root};
    memcpy(r.peer,m->session->pinned_peer,32); memcpy(r.digest,wire->digest,32); encode_receipt(receipt,&r);
    struct object_store_put_request requests[6]={
        {.id=module,.type_id={0,COMPONENT_TYPE_MODULE},.creator_id=root,.modifier_id=root,
         .payload=m->bundle+128,.payload_size=view.module_length},
        {.id=captable,.type_id={0,COMPONENT_TYPE_CAPTABLE},.creator_id=root,.modifier_id=root,.payload=caps,.payload_size=sizeof(caps)},
        {.id=state,.type_id={0,COMPONENT_TYPE_INSTANCE_STATE},.creator_id=root,.modifier_id=root,
         .payload=m->bundle+128+view.module_length+view.captable_length,.payload_size=view.state_length},
        {.id=root,.type_id={0,COMPONENT_TYPE_ROOT},.creator_id=root,.modifier_id=root,
         .payload=(const uint8_t *)&root_record,.payload_size=sizeof(root_record)},
        {.id=registry_id(),.type_id={0,COMPONENT_TYPE_REGISTRY},.payload=catalog,.payload_size=catalog_size},
        {.id=receipt_id(root),.type_id={0,MIGRATION_TYPE_RECEIPT},.payload=receipt,.payload_size=sizeof(receipt)}};
    int result=commit(m,requests,6);
    if(result == 1) { m->imported=root; m->record.target=root; free_bundle(m); }
    return result;
}
static size_t acknowledge(struct migration *m, struct migration_wire_view v, uint64_t sequence,
                          uint8_t *out, size_t capacity) {
    uint8_t b[MIGRATION_WIRE_BYTES+8];
    v.opcode=sequence?FORWARD_ACK:ACK; header(b,&v);
    if(sequence) put64(b+96,sequence);
    return net_session_seal(m->session,NET_MESSAGE_ACK,b,96+(sequence?8:0),out,capacity);
}
static int outbox(struct migration *m, const uint8_t **b, size_t *n) {
    if(object_table_find(&m->store->table,outbox_id(m->record.root)) == NULL) { *n=0; return 1; }
    if(!load(m,outbox_id(m->record.root),MIGRATION_TYPE_OUTBOX,b,n)) return 0;
    if(*n == 0) return 1;
    struct migration_wire_view v;
    return migration_wire_decode(*b,*n,NET_MESSAGE_MIGRATION,&v) &&
        v.opcode == FORWARD && matching(m,&v) && object_id_equal(v.target,m->record.target) &&
        get64(*b+96) == m->record.forward_sequence;
}
size_t migration_next_packet(struct migration *m, uint8_t *out, size_t capacity) {
    if(!healthy(m) || !m->source || !m->session->active) return 0;
    uint8_t b[NET_BODY_MAX];
    struct migration_wire_view v={.generation=m->record.generation,.root=m->record.root,.total=(uint32_t)m->total};
    memcpy(v.digest,m->record.digest,32);
    if(m->phase == 3) {
        const uint8_t *pending=NULL; size_t n;
        if(!outbox(m,&pending,&n) || n == 0) return 0;
        return net_session_seal(m->session,NET_MESSAGE_MIGRATION,pending,n,out,capacity);
    }
    v.opcode=m->phase == 0?BEGIN:m->received == m->total?END:CHUNK;
    v.offset=(uint32_t)m->received; header(b,&v);
    size_t length=96;
    if(v.opcode == CHUNK) {
        size_t chunk=m->total-m->received;
        if(chunk > MIGRATION_CHUNK_BYTES) chunk=MIGRATION_CHUNK_BYTES;
        memcpy(b+96,m->bundle+m->received,chunk); length+=chunk;
    }
    return net_session_seal(m->session,NET_MESSAGE_MIGRATION,b,length,out,capacity);
}
static int forward_receive(struct migration *m, const struct migration_wire_view *v,
                           const uint8_t *body, struct migration_receipt *receipt) {
    uint64_t sequence=get64(body+96);
    if(!object_id_equal(v->target,receipt->target)) return 0;
    if(sequence == receipt->forward_sequence) return 1;
    struct component *c=m->target_component;
    if(receipt->forward_sequence == UINT64_MAX || sequence != receipt->forward_sequence+1 ||
       c == NULL || c->exited || !object_id_equal(c->root_id,receipt->target) ||
       (c->domain != NULL && (c->domain->active || c->domain->halted))) return 0;
    const uint8_t *old; size_t old_size;
    if(!load(m,c->state_id,COMPONENT_TYPE_INSTANCE_STATE,&old,&old_size) ||
       !instance_state_bytes_validate(old,old_size)) return 0;
    struct instance_state_header h; memcpy(&h,old,sizeof(h));
    size_t message_size=v->length-8;
    if(h.mailbox_used != c->mailbox_used || h.mailbox_used > COMPONENT_MAILBOX_BYTES-8-message_size ||
       (h.mailbox_used != 0 && !equal(old+64,c->mailbox,h.mailbox_used))) return 0;
    if (old_size > SIZE_MAX - 8 - message_size) return 0;
    uint8_t *state=kmalloc(old_size+8+message_size);
    if(state == NULL) return 0;
    memcpy(state,old,64+h.mailbox_used);
    put32(state+64+h.mailbox_used,(uint32_t)message_size); put32(state+68+h.mailbox_used,UINT32_MAX);
    memcpy(state+72+h.mailbox_used,body+104,message_size);
    memcpy(state+72+h.mailbox_used+message_size,old+64+h.mailbox_used,(size_t)h.memory_size);
    h.mailbox_used+=(uint32_t)(8+message_size);
    h.payload_crc32c=object_crc32c(state+64,old_size+8+message_size-64);
    memcpy(state,&h,sizeof(h));
    struct migration_receipt next=*receipt; next.forward_sequence=sequence;
    uint8_t record[MIGRATION_RECEIPT_BYTES]; encode_receipt(record,&next);
    struct object_store_put_request r[2]={
        {.id=c->state_id,.type_id={0,COMPONENT_TYPE_INSTANCE_STATE},.creator_id=c->root_id,.modifier_id=c->root_id,
         .logical_timestamp=c->logical_time,.payload=state,.payload_size=old_size+8+message_size},
        {.id=receipt_id(c->root_id),.type_id={0,MIGRATION_TYPE_RECEIPT},.payload=record,.payload_size=sizeof(record)}};
    int result=commit(m,r,2);
    if(result == 1) { memcpy(c->mailbox,state+64,h.mailbox_used); c->mailbox_used=h.mailbox_used; *receipt=next; }
    crypto_wipe(state,old_size+8+message_size); kfree(state); return result;
}
int migration_receive(struct migration *m, const uint8_t *packet, size_t length,
                      uint8_t *reply, size_t capacity, size_t *reply_length) {
    if(!healthy(m)) return rejected(m);
    if(reply == NULL || reply_length == NULL) return 0;
    uint8_t b[NET_BODY_MAX]; uint32_t kind; size_t n; struct migration_wire_view v;
    if(!net_session_open(m->session,packet,length,b,sizeof(b),&kind,&n) ||
       !migration_wire_decode(b,n,kind,&v)) return 0;
    *reply_length=0;
    if(v.opcode >= ACK) {
        if(!m->source || !matching(m,&v)) return 0;
        if(v.opcode == FORWARD_ACK) {
            if(!object_id_equal(v.target,m->record.target) || get64(b+96) != m->record.forward_sequence) return 0;
            const uint8_t *pending=NULL; size_t size;
            if(!outbox(m,&pending,&size)) return 0;
            if(size == 0) return 1;
            struct object_store_put_request clear={.id=outbox_id(m->record.root),.type_id={0,MIGRATION_TYPE_OUTBOX}};
            return commit(m,&clear,1);
        }
        if(!object_id_is_zero(v.target)) {
            if(!object_id_is_zero(m->record.target) && !object_id_equal(m->record.target,v.target)) return 0;
            struct migration_record r=m->record; r.target=v.target;
            uint8_t encoded[MIGRATION_RECORD_BYTES]; encode_record(encoded,&r);
            if(object_id_is_zero(m->record.target)) {
                struct object_store_put_request request={.id=fence_id(r.root),.type_id={0,MIGRATION_TYPE_FENCE},
                    .payload=encoded,.payload_size=sizeof(encoded)};
                int result=commit(m,&request,1); if(result != 1) return result;
            }
            m->record=r; m->phase=3; free_bundle(m); return 1;
        }
        if(m->phase == 3 || v.offset < m->received) return 0;
        if(m->phase == 1 && v.offset > m->received+MIGRATION_CHUNK_BYTES) return 0;
        m->phase=1; m->received=v.offset; return 1;
    }
    if(m->source || !m->allow_incoming) return 0;
    struct migration_receipt receipt; int found=find_receipt(m,&v,&receipt);
    if(found < 0) { m->halted=1; return -1; }
    if(found == 1) {
        int result=1; uint64_t sequence=0;
        if(v.opcode == FORWARD) {
            result=forward_receive(m,&v,b,&receipt);
            sequence=get64(b+96); v.offset=0;
        } else v.offset=v.total;
        if(result != 1) return result;
        v.target=receipt.target; m->imported=receipt.target;
        *reply_length=acknowledge(m,v,sequence,reply,capacity); return 1;
    }
    if(v.opcode == FORWARD) return 0;
    if(v.opcode == BEGIN) {
        if(m->bundle == NULL) {
            m->bundle=kmalloc(v.total);
            if(m->bundle == NULL) return 0;
            m->total=v.total; m->received=0; memset(&m->record,0,sizeof(m->record));
            m->record.root=v.root; m->record.generation=v.generation; memcpy(m->record.digest,v.digest,32);
        } else if(!matching(m,&v)) return 0;
    } else if(m->bundle == NULL || !matching(m,&v)) return 0;
    if(v.opcode == CHUNK) {
        if(v.offset == m->received) {
            memcpy(m->bundle+m->received,b+96,v.length); m->received+=v.length;
        } else if(v.offset > m->received || v.length > m->received-v.offset ||
                  !equal(m->bundle+v.offset,b+96,v.length)) return 0;
    }
    if(v.opcode == END) {
        if(m->received != m->total) return 0;
        int result=publish_import(m,&v); if(result != 1) return result;
        v.target=m->imported; v.offset=v.total;
    } else v.offset=(uint32_t)m->received;
    *reply_length=acknowledge(m,v,0,reply,capacity); return 1;
}
int migration_attach_target(struct migration *m, struct component *c) {
    if(!healthy(m) || c == NULL || c->store != m->store || c->instance == NULL || m->source) return 0;
    const uint8_t *b; size_t n; struct migration_receipt r;
    if (!load(m,receipt_id(c->root_id),MIGRATION_TYPE_RECEIPT,&b,&n) ||
        !migration_receipt_decode(b,n,&r) || !object_id_equal(r.target,c->root_id) ||
        !equal(r.peer,m->session->pinned_peer,32)) return 0;
    m->imported=c->root_id; m->target_component=c; return 1;
}
int migration_forward_pending(struct migration *m) {
    if (!healthy(m)) return rejected(m);
    if (!m->source || m->phase != 3) return 0;
    const uint8_t *bytes = NULL; size_t length;
    if (!outbox(m, &bytes, &length)) return -1;
    return length != 0;
}
int migration_forward(struct migration *m, const uint8_t *payload, size_t length) {
    if(!healthy(m)) return rejected(m);
    if(!m->source || m->phase != 3 || length > MIGRATION_FORWARD_MAX ||
       (payload == NULL && length != 0) || m->record.forward_sequence == UINT64_MAX) return 0;
    const uint8_t *pending=NULL; size_t old;
    if(!outbox(m,&pending,&old) || old != 0) return 0;
    struct migration_record r=m->record; r.forward_sequence++;
    struct migration_wire_view v={.opcode=FORWARD,.generation=r.generation,.root=r.root,
        .target=r.target,.total=(uint32_t)m->total};
    memcpy(v.digest,r.digest,32);
    uint8_t b[104+MIGRATION_FORWARD_MAX], encoded[MIGRATION_RECORD_BYTES];
    header(b,&v); put64(b+96,r.forward_sequence);
    if(length != 0) memcpy(b+104,payload,length);
    encode_record(encoded,&r);
    struct object_store_put_request requests[2]={
        {.id=fence_id(r.root),.type_id={0,MIGRATION_TYPE_FENCE},.payload=encoded,.payload_size=sizeof(encoded)},
        {.id=outbox_id(r.root),.type_id={0,MIGRATION_TYPE_OUTBOX},.payload=b,.payload_size=104+length}};
    int result=commit(m,requests,2); if(result == 1) m->record=r; return result;
}
void migration_close(struct migration *m) {
    if(m == NULL) return;
    free_bundle(m); memset(m,0,sizeof(*m));
}
