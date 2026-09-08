#include "lineage.h"
#include "../lib/string.h"

struct lineage_header {
    uint64_t magic;
    uint32_t version;
    uint32_t count;
    uint32_t crc;
    uint32_t reserved;
};
_Static_assert(sizeof(struct lineage_header) == 24, "lineage header layout");

int capability_lineage_encode(const struct capability_derivation_table *table,
                              uint8_t *bytes, size_t capacity) {
    struct lineage_header header = {0};
    if (bytes == NULL || capacity < CAP_LINEAGE_BYTES ||
        !capability_derivation_table_is_valid(table)) {
        return 0;
    }
    header.magic = CAP_LINEAGE_MAGIC;
    header.version = 1;
    header.count = table->count;
    memcpy(bytes + sizeof(header), table->records, sizeof(table->records));
    header.crc = object_crc32c(bytes + sizeof(header), sizeof(table->records));
    memcpy(bytes, &header, sizeof(header));
    return 1;
}

int capability_lineage_decode(struct capability_derivation_table *table,
                              const uint8_t *bytes, size_t length) {
    struct lineage_header header;
    if (table == NULL || !capability_lineage_validate(bytes, length)) {
        return 0;
    }
    memcpy(&header, bytes, sizeof(header));
    memcpy(table->records, bytes + sizeof(header), sizeof(table->records));
    table->count = header.count;
    return 1;
}

int capability_lineage_load(struct object_store *store,
                            struct capability_derivation_table *table) {
    struct object_header header;
    const uint8_t *bytes;
    size_t length;
    const struct object_id id = {4, 0};
    const struct object_id type = {0, 7};
    if (!object_store_get(store, id, &header, &bytes, &length) ||
        !object_id_equal(header.type_id, type)) {
        return 0;
    }
    return capability_lineage_decode(table, bytes, length);
}

struct object_store_put_request capability_lineage_request(const uint8_t *bytes) {
    struct object_store_put_request request = {0};
    request.id = (struct object_id){4, 0};
    request.type_id = (struct object_id){0, 7};
    request.payload = bytes;
    request.payload_size = CAP_LINEAGE_BYTES;
    return request;
}
