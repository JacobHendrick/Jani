#include "../kernel/net/session.h"
#include "../third_party/monocypher/src/monocypher.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>

static int random_bytes(uint8_t *bytes, size_t length) {
    size_t used = 0;
    while (used < length) {
        ssize_t result = getrandom(bytes + used, length - used, 0);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return 0;
        used += (size_t)result;
    }
    return 1;
}
int main(int argc, char **argv) {
    if (argc != 3) { fputs("usage: net_provision node-a.bin node-b.bin\n", stderr); return 1; }
    umask(077);
    uint8_t seeds[2][32], record[104];
    struct net_identity identities[2];
    int status = 1;
    for (size_t i = 0; i < 2; i++)
        if (!random_bytes(seeds[i], 32) ||
            !net_identity_from_seed(&identities[i], seeds[i])) goto done;
    for (size_t i = 0; i < 2; i++) {
        memset(record, 0, sizeof(record)); memcpy(record, "JN5C", 4);
        record[4] = 1; record[5] = (uint8_t)i;
        memcpy(record + 8, seeds[i], 32);
        memcpy(record + 40, identities[1-i].public_key, 32);
        memcpy(record + 72, identities[i].public_key, 32);
        FILE *file = fopen(argv[1+i], "wb");
        if (file == NULL) goto done;
        int written = fwrite(record, 1, sizeof(record), file) == sizeof(record);
        if (fclose(file) != 0 || !written) goto done;
    }
    status = 0;
done:
    crypto_wipe(seeds, sizeof(seeds)); crypto_wipe(record, sizeof(record));
    crypto_wipe(identities, sizeof(identities)); return status;
}
