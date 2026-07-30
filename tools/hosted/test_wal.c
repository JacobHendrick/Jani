#include <stdio.h>

#include "../../kernel/lib/string.h"
#include "../../kernel/obj/object_header.h"
#include "../../kernel/obj/wal.h"
#include "check.h"

unsigned long checks_passed;

static void make_valid_record(struct object_wal_record *record) {
    memset(record, 0, sizeof(*record));
    record->magic = OBJECT_WAL_MAGIC;
    record->format_version = OBJECT_WAL_FORMAT_VERSION;
    record->table_sector = 10;
    record->table_count = 2;
    record->generation = 3;
    record->checksum = object_crc32c((const uint8_t *)record, sizeof(*record));
}

int main(void) {
    struct object_wal_record record;

    make_valid_record(&record);
    CHECK(object_wal_validate((const uint8_t *)&record, sizeof(record)));

    record.table_count = 3;
    CHECK(!object_wal_validate((const uint8_t *)&record, sizeof(record)));

    make_valid_record(&record);
    record.flags = 1;
    CHECK(!object_wal_validate((const uint8_t *)&record, sizeof(record)));

    make_valid_record(&record);
    record.reserved = 1;
    CHECK(!object_wal_validate((const uint8_t *)&record, sizeof(record)));

    make_valid_record(&record);
    CHECK(!object_wal_validate((const uint8_t *)&record, sizeof(record) - 1));

    printf("test_wal: %lu checks passed\n", checks_passed);
    return 0;
}
