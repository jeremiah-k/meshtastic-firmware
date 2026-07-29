#include "MeshTypes.h"
#include "TestUtil.h"
#include "nimble/BleLogPolicy.h"

#include <unity.h>

using meshtastic::bluetooth::BleLogPolicy;

static void test_value_must_fit_negotiated_att_payload()
{
    TEST_ASSERT_FALSE(BleLogPolicy::fitsPeerMtu(0, BleLogPolicy::ATT_VALUE_OVERHEAD));
    TEST_ASSERT_TRUE(BleLogPolicy::fitsPeerMtu(0, BleLogPolicy::ATT_VALUE_OVERHEAD + 1));
    TEST_ASSERT_TRUE(BleLogPolicy::fitsPeerMtu(1, BleLogPolicy::ATT_VALUE_OVERHEAD + 1));
    TEST_ASSERT_FALSE(BleLogPolicy::fitsPeerMtu(2, BleLogPolicy::ATT_VALUE_OVERHEAD + 1));
    TEST_ASSERT_TRUE(BleLogPolicy::fitsPeerMtu(244, 247));
    TEST_ASSERT_FALSE(BleLogPolicy::fitsPeerMtu(245, 247));
}

static void test_value_retains_application_size_cap()
{
    TEST_ASSERT_TRUE(BleLogPolicy::fitsPeerMtu(BleLogPolicy::MAX_VALUE_LENGTH, 517));
    TEST_ASSERT_FALSE(BleLogPolicy::fitsPeerMtu(BleLogPolicy::MAX_VALUE_LENGTH + 1, 517));
}

static void test_missing_timestamp_is_immediately_ready()
{
    TEST_ASSERT_TRUE(BleLogPolicy::intervalElapsed(false, 0, 25, 0));
}

static void test_interval_opens_exactly_at_deadline()
{
    TEST_ASSERT_FALSE(BleLogPolicy::intervalElapsed(true, 100, 25, 124));
    TEST_ASSERT_TRUE(BleLogPolicy::intervalElapsed(true, 100, 25, 125));
}

static void test_interval_is_millis_wrap_safe()
{
    constexpr uint32_t startedAtMs = UINT32_MAX - 10;
    TEST_ASSERT_FALSE(BleLogPolicy::intervalElapsed(true, startedAtMs, 25, 10));
    TEST_ASSERT_TRUE(BleLogPolicy::intervalElapsed(true, startedAtMs, 25, 14));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_value_must_fit_negotiated_att_payload);
    RUN_TEST(test_value_retains_application_size_cap);
    RUN_TEST(test_missing_timestamp_is_immediately_ready);
    RUN_TEST(test_interval_opens_exactly_at_deadline);
    RUN_TEST(test_interval_is_millis_wrap_safe);
    exit(UNITY_END());
}

void loop() {}
