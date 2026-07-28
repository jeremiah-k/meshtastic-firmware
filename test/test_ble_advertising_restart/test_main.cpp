#include "MeshTypes.h"
#include "TestUtil.h"
#include "nimble/AdvertisingRestartController.h"
#include <unity.h>

using meshtastic::bluetooth::AdvertisingRestartController;

static void test_unarmed_controller_is_ready()
{
    AdvertisingRestartController controller;
    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(100));
}

static void test_arm_waits_then_becomes_ready()
{
    AdvertisingRestartController controller;
    controller.arm(100, 250);

    TEST_ASSERT_EQUAL_UINT32(250, controller.remainingMs(100));
    TEST_ASSERT_EQUAL_UINT32(1, controller.remainingMs(349));
    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(350));
}

static void test_arm_if_needed_arms_an_unarmed_controller()
{
    AdvertisingRestartController controller;
    controller.armIfNeeded(100, 250);

    TEST_ASSERT_EQUAL_UINT32(250, controller.remainingMs(100));
    TEST_ASSERT_EQUAL_UINT32(1, controller.remainingMs(349));
    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(350));
}

static void test_arm_if_needed_does_not_extend_existing_wait()
{
    AdvertisingRestartController controller;
    controller.arm(100, 250);
    controller.armIfNeeded(200, 1000);

    TEST_ASSERT_EQUAL_UINT32(50, controller.remainingMs(300));
}

static void test_retry_rearms_from_latest_attempt()
{
    AdvertisingRestartController controller;
    controller.arm(100, 250);
    controller.arm(350, 1000);

    TEST_ASSERT_EQUAL_UINT32(1000, controller.remainingMs(350));
    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(1350));
}

static void test_elapsed_time_handles_millis_wraparound()
{
    AdvertisingRestartController controller;
    controller.arm(UINT32_MAX - 99, 200);

    TEST_ASSERT_EQUAL_UINT32(50, controller.remainingMs(50));
    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(100));
}

static void test_reset_clears_wait()
{
    AdvertisingRestartController controller;
    controller.arm(100, 250);
    controller.reset();

    TEST_ASSERT_EQUAL_UINT32(0, controller.remainingMs(100));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_unarmed_controller_is_ready);
    RUN_TEST(test_arm_waits_then_becomes_ready);
    RUN_TEST(test_arm_if_needed_arms_an_unarmed_controller);
    RUN_TEST(test_arm_if_needed_does_not_extend_existing_wait);
    RUN_TEST(test_retry_rearms_from_latest_attempt);
    RUN_TEST(test_elapsed_time_handles_millis_wraparound);
    RUN_TEST(test_reset_clears_wait);
    exit(UNITY_END());
}

void loop() {}
