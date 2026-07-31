#include "MeshTypes.h"
#include "TestUtil.h"
#include "nimble/ConnectionParamsUpdateController.h"
#include <unity.h>

using meshtastic::bluetooth::ConnectionParamsMode;
using meshtastic::bluetooth::ConnectionParamsRequest;
using meshtastic::bluetooth::ConnectionParamsUpdateController;

static void assertRequest(const ConnectionParamsRequest &request, uint16_t handle, ConnectionParamsMode mode)
{
    TEST_ASSERT_EQUAL_UINT16(handle, request.connHandle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(mode), static_cast<uint8_t>(request.mode));
}

static void test_connect_queues_initial_profile()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest request{};

    controller.onConnected(7);

    TEST_ASSERT_TRUE(controller.beginNext(request));
    assertRequest(request, 7, ConnectionParamsMode::INITIAL_HIGH_THROUGHPUT);
    TEST_ASSERT_FALSE(controller.beginNext(request));
    TEST_ASSERT_FALSE(controller.onUpdateComplete(7));
    TEST_ASSERT_FALSE(controller.beginNext(request));
}

static void test_latest_request_wins_while_update_is_active()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest request{};
    controller.onConnected(4);
    TEST_ASSERT_TRUE(controller.beginNext(request));

    TEST_ASSERT_TRUE(controller.request(controller.currentGeneration(), ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(controller.request(controller.currentGeneration(), ConnectionParamsMode::LOW_POWER));
    TEST_ASSERT_TRUE(controller.onUpdateComplete(4));

    TEST_ASSERT_TRUE(controller.beginNext(request));
    assertRequest(request, 4, ConnectionParamsMode::LOW_POWER);
    TEST_ASSERT_FALSE(controller.onUpdateComplete(4));
}

static void test_repeating_active_mode_is_coalesced()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest request{};
    controller.onConnected(3);
    TEST_ASSERT_TRUE(controller.beginNext(request));

    TEST_ASSERT_TRUE(controller.request(controller.currentGeneration(), ConnectionParamsMode::INITIAL_HIGH_THROUGHPUT));
    TEST_ASSERT_FALSE(controller.onUpdateComplete(3));
    TEST_ASSERT_FALSE(controller.beginNext(request));
}

static void test_submission_rejection_retries_same_request()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest first{};
    ConnectionParamsRequest retry{};
    controller.onConnected(9);
    TEST_ASSERT_TRUE(controller.beginNext(first));

    TEST_ASSERT_TRUE(controller.onSubmissionRejected(first));
    TEST_ASSERT_TRUE(controller.beginNext(retry));
    assertRequest(retry, 9, ConnectionParamsMode::INITIAL_HIGH_THROUGHPUT);
    TEST_ASSERT_EQUAL_UINT32(first.generation, retry.generation);
}

static void test_retired_session_rejects_stale_submission_and_wrong_handle_completion()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest retired{};
    ConnectionParamsRequest current{};
    controller.onConnected(1);
    TEST_ASSERT_TRUE(controller.beginNext(retired));

    controller.reset();
    controller.onConnected(1); // ESP32 commonly reuses connection handle zero/one.
    TEST_ASSERT_TRUE(controller.beginNext(current));

    TEST_ASSERT_FALSE(controller.onSubmissionRejected(retired));
    TEST_ASSERT_FALSE(controller.onUpdateComplete(2));
    TEST_ASSERT_FALSE(controller.beginNext(retired));

    // NimBLE identifies completion callbacks by connection handle. Once the reused handle completes,
    // it must release the current session rather than resurrecting the retired request.
    TEST_ASSERT_FALSE(controller.onUpdateComplete(1));
    TEST_ASSERT_FALSE(controller.beginNext(retired));
}

static void test_retired_session_request_cannot_target_reconnect()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest request{};
    controller.onConnected(1);
    const uint32_t retiredGeneration = controller.currentGeneration();

    controller.reset();
    controller.onConnected(1);

    TEST_ASSERT_FALSE(controller.request(retiredGeneration, ConnectionParamsMode::LOW_POWER));
    TEST_ASSERT_TRUE(controller.beginNext(request));
    assertRequest(request, 1, ConnectionParamsMode::INITIAL_HIGH_THROUGHPUT);
}

static void test_disconnected_controller_ignores_requests()
{
    ConnectionParamsUpdateController controller;
    ConnectionParamsRequest request{};

    TEST_ASSERT_FALSE(controller.request(controller.currentGeneration(), ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_FALSE(controller.request(controller.currentGeneration(), ConnectionParamsMode::NONE));
    TEST_ASSERT_FALSE(controller.beginNext(request));
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_connect_queues_initial_profile);
    RUN_TEST(test_latest_request_wins_while_update_is_active);
    RUN_TEST(test_repeating_active_mode_is_coalesced);
    RUN_TEST(test_submission_rejection_retries_same_request);
    RUN_TEST(test_retired_session_rejects_stale_submission_and_wrong_handle_completion);
    RUN_TEST(test_retired_session_request_cannot_target_reconnect);
    RUN_TEST(test_disconnected_controller_ignores_requests);
    exit(UNITY_END());
}

void loop() {}
