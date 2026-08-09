#include <stdint.h>
#include <stdio.h>

#include "../../kernel/cap/capability.h"
#include "check.h"

unsigned long checks_passed;

static struct capability make_capability(uint32_t rights, uint32_t badge) {
    struct capability capability;

    capability.object.high = UINT64_C(3);
    capability.object.low = UINT64_C(42);
    capability.rights = rights;
    capability.badge = badge;
    return capability;
}

static void test_validation(void) {
    struct capability capability;

    CHECK(sizeof(struct capability) == CAPABILITY_SIZE);

    capability = make_capability(CAP_RIGHT_READ, 0);
    CHECK(capability_is_valid(&capability) == 1);
    CHECK(capability_is_valid(NULL) == 0);

    capability.object.high = 0;
    capability.object.low = 0;
    CHECK(capability_is_valid(&capability) == 0);

    capability = make_capability(0, 0);
    CHECK(capability_is_valid(&capability) == 0);

    capability = make_capability(UINT32_C(0x10), 0);
    CHECK(capability_is_valid(&capability) == 0);
}

static void test_permission_checks(void) {
    struct capability capability;

    capability = make_capability(CAP_RIGHT_READ | CAP_RIGHT_WRITE, 0);

    CHECK(capability_allows(&capability, CAP_RIGHT_READ) == 1);
    CHECK(capability_allows(&capability, CAP_RIGHT_WRITE) == 1);
    CHECK(capability_allows(
              &capability,
              CAP_RIGHT_READ | CAP_RIGHT_WRITE
          ) == 1);
    CHECK(capability_allows(&capability, CAP_RIGHT_SEND) == 0);
    CHECK(capability_allows(&capability, 0) == 0);
    CHECK(capability_allows(&capability, UINT32_C(0x10)) == 0);
    CHECK(capability_allows(NULL, CAP_RIGHT_READ) == 0);
}

static void test_derivation(void) {
    struct capability parent;
    struct capability child;

    parent = make_capability(
        CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT,
        7
    );

    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 99, &child) == 1);
    CHECK(object_id_equal(child.object, parent.object) == 1);
    CHECK(child.rights == CAP_RIGHT_READ);
    CHECK(child.badge == 99);
    CHECK(parent.rights ==
          (CAP_RIGHT_READ | CAP_RIGHT_WRITE | CAP_RIGHT_GRANT));
    CHECK(parent.badge == 7);
}

static void test_invalid_derivation(void) {
    struct capability parent;
    struct capability child;

    parent = make_capability(CAP_RIGHT_READ, 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 0, &child) == 0);

    parent = make_capability(CAP_RIGHT_READ | CAP_RIGHT_GRANT, 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_WRITE, 0, &child) == 0);
    CHECK(capability_derive(&parent, 0, 0, &child) == 0);
    CHECK(capability_derive(&parent, UINT32_C(0x10), 0, &child) == 0);
    CHECK(capability_derive(NULL, CAP_RIGHT_READ, 0, &child) == 0);
    CHECK(capability_derive(&parent, CAP_RIGHT_READ, 0, NULL) == 0);
}

int main(void) {
    checks_passed = 0;

    test_validation();
    test_permission_checks();
    test_derivation();
    test_invalid_derivation();

    printf("test_capability: %lu checks passed\n", checks_passed);
    return 0;
}
