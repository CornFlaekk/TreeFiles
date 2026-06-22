#include "file_utils.h"
#include <cstdio>
#include <cstring>
#include <string>

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) do { tests_run++; printf("  %s... ", name); } while(0)
#define CHECK(cond) do { if (!(cond)) { printf("FAIL\n"); tests_failed++; return; } } while(0)
#define PASS() printf("OK\n")

void test_zero() {
    TEST("0 bytes");
    CHECK(human_readable_size(0) == "0 bytes");
    PASS();
}

void test_bytes_range() {
    TEST("1 byte");
    CHECK(human_readable_size(1) == "1 bytes");
    PASS();

    TEST("1023 bytes");
    CHECK(human_readable_size(1023) == "1023 bytes");
    PASS();
}

void test_kilobytes() {
    TEST("1024 bytes = 1.00 KB");
    CHECK(human_readable_size(1024) == "1.00 KB");
    PASS();

    TEST("1536 bytes = 1.50 KB");
    CHECK(human_readable_size(1536) == "1.50 KB");
    PASS();

    TEST("10240 bytes = 10.00 KB");
    CHECK(human_readable_size(10240) == "10.00 KB");
    PASS();
}

void test_megabytes() {
    TEST("1048576 bytes = 1.00 MB");
    CHECK(human_readable_size(1048576) == "1.00 MB");
    PASS();

    TEST("1572864 bytes = 1.50 MB");
    CHECK(human_readable_size(1572864) == "1.50 MB");
    PASS();
}

void test_gigabytes() {
    TEST("1073741824 bytes = 1.00 GB");
    CHECK(human_readable_size(1073741824ULL) == "1.00 GB");
    PASS();
}

void test_terabytes() {
    TEST("1099511627776 bytes = 1.00 TB");
    CHECK(human_readable_size(1099511627776ULL) == "1.00 TB");
    PASS();

    TEST("1.5 TB");
    CHECK(human_readable_size(1649267441664ULL) == "1.50 TB");
    PASS();
}

void test_large_but_not_tb() {
    TEST("4.00 GB");
    CHECK(human_readable_size(4294967296ULL) == "4.00 GB");
    PASS();
}

int main() {
    printf("test_human_readable_size\n");
    test_zero();
    test_bytes_range();
    test_kilobytes();
    test_megabytes();
    test_gigabytes();
    test_terabytes();
    test_large_but_not_tb();
    printf("  %d run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
