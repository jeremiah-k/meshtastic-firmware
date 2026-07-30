#include "MeshTypes.h"
#include "TestUtil.h"
#include "nimble/BleResourceGate.h"

#include <unity.h>

using meshtastic::bluetooth::BleResourceGate;
using meshtastic::bluetooth::BleResourceLease;

static void test_open_gate_admits_and_releases_access()
{
    BleResourceGate gate;

    TEST_ASSERT_TRUE(gate.tryAcquire());
    TEST_ASSERT_TRUE(gate.hasActiveUsers());

    gate.release();
    TEST_ASSERT_FALSE(gate.hasActiveUsers());
}

static void test_drain_rejects_new_access_while_existing_user_finishes()
{
    BleResourceGate gate;
    TEST_ASSERT_TRUE(gate.tryAcquire());

    gate.beginDrain();

    TEST_ASSERT_TRUE(gate.isDraining());
    TEST_ASSERT_TRUE(gate.hasActiveUsers());
    TEST_ASSERT_FALSE(gate.tryAcquire());

    gate.release();
    TEST_ASSERT_FALSE(gate.hasActiveUsers());
}

static void test_reopen_admits_access_after_drain()
{
    BleResourceGate gate;
    gate.beginDrain();
    TEST_ASSERT_FALSE(gate.tryAcquire());

    gate.reopen();

    TEST_ASSERT_FALSE(gate.isDraining());
    TEST_ASSERT_TRUE(gate.tryAcquire());
    gate.release();
}

static void test_lease_releases_access_at_scope_exit()
{
    BleResourceGate gate;

    {
        BleResourceLease lease(gate);
        TEST_ASSERT_TRUE(static_cast<bool>(lease));
        TEST_ASSERT_TRUE(gate.hasActiveUsers());
    }

    TEST_ASSERT_FALSE(gate.hasActiveUsers());
}

static void test_rejected_lease_does_not_register_an_active_user()
{
    BleResourceGate gate;
    gate.beginDrain();

    {
        BleResourceLease lease(gate);
        TEST_ASSERT_FALSE(static_cast<bool>(lease));
        TEST_ASSERT_FALSE(gate.hasActiveUsers());
    }

    TEST_ASSERT_FALSE(gate.hasActiveUsers());
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_open_gate_admits_and_releases_access);
    RUN_TEST(test_drain_rejects_new_access_while_existing_user_finishes);
    RUN_TEST(test_reopen_admits_access_after_drain);
    RUN_TEST(test_lease_releases_access_at_scope_exit);
    RUN_TEST(test_rejected_lease_does_not_register_an_active_user);
    exit(UNITY_END());
}

void loop() {}
