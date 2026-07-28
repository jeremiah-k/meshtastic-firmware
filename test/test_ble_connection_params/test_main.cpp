#include "MeshTypes.h"
#include "TestUtil.h"
#include "nimble/ConnectionParamsUpdateController.h"

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <unity.h>

using meshtastic::bluetooth::ConnectionParamsMode;
using meshtastic::bluetooth::ConnectionParamsRequest;
using meshtastic::bluetooth::ConnectionParamsUpdateController;

static void assertRequest(const ConnectionParamsRequest &request, uint16_t handle, ConnectionParamsMode mode)
{
    TEST_ASSERT_EQUAL_UINT16(handle, request.connHandle);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(mode), static_cast<uint8_t>(request.mode));
}

static bool requestAt(ConnectionParamsUpdateController &controller, ConnectionParamsMode mode, uint32_t nowMs = 0)
{
    return controller.request(controller.currentGeneration(), mode, nowMs);
}

static std::optional<ConnectionParamsRequest> serviceAt(ConnectionParamsUpdateController &controller, uint32_t nowMs,
                                                        bool accepted = true, int32_t *nextDelayMs = nullptr)
{
    std::optional<ConnectionParamsRequest> submitted;
    const int32_t delay = controller.servicePending(nowMs, [&](const ConnectionParamsRequest &request) {
        submitted = request;
        return accepted;
    });
    if (nextDelayMs != nullptr) {
        *nextDelayMs = delay;
    }
    return submitted;
}

static void test_connect_waits_for_a_config_profile_request()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(7);

    TEST_ASSERT_FALSE(serviceAt(controller, 0).has_value());
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));

    const auto request = serviceAt(controller, 1);
    TEST_ASSERT_TRUE(request.has_value());
    assertRequest(*request, 7, ConnectionParamsMode::HIGH_THROUGHPUT);
}

static void test_peer_high_throughput_update_avoids_redundant_submission()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);

    controller.recordUpdate(4, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);
    TEST_ASSERT_FALSE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_FALSE(serviceAt(controller, 0).has_value());
}

static void test_satisfying_update_releases_lane_when_no_opposite_mode_is_pending()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100).has_value());

    controller.recordUpdate(4, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);
    TEST_ASSERT_FALSE(serviceAt(controller, 101).has_value());
}

static void test_satisfying_callback_does_not_release_opposite_pending_mode()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100).has_value());
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER));

    controller.recordUpdate(4, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);
    int32_t delay = 0;
    TEST_ASSERT_FALSE(serviceAt(controller, 200, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS - 100, delay);

    const uint32_t laneExpiry = 100 + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS;
    TEST_ASSERT_FALSE(serviceAt(controller, laneExpiry).has_value());
    const auto lowPower = serviceAt(controller, laneExpiry + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS);
    TEST_ASSERT_TRUE(lowPower.has_value());
    assertRequest(*lowPower, 4, ConnectionParamsMode::LOW_POWER);
}

static void test_unmatched_success_does_not_release_active_request()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));
    TEST_ASSERT_TRUE(serviceAt(controller, 100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS).has_value());

    controller.recordUpdate(4, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);
    TEST_ASSERT_FALSE(serviceAt(controller, 200 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS).has_value());

    const auto retry = serviceAt(controller, 100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS +
                                                 ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS);
    TEST_ASSERT_TRUE(retry.has_value());
    assertRequest(*retry, 4, ConnectionParamsMode::LOW_POWER);
}

static void test_peer_failure_does_not_release_active_request()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100).has_value());

    controller.recordUpdate(1, 0, 1);
    TEST_ASSERT_FALSE(serviceAt(controller, 200).has_value());

    const auto retry = serviceAt(controller, 100 + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS);
    TEST_ASSERT_TRUE(retry.has_value());
    assertRequest(*retry, 1, ConnectionParamsMode::HIGH_THROUGHPUT);
}

static void test_latest_request_wins_after_lane_timeout()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100).has_value());

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER));
    const uint32_t laneExpiry = 100 + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS;
    TEST_ASSERT_FALSE(serviceAt(controller, laneExpiry).has_value());
    const auto request = serviceAt(controller, laneExpiry + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS);
    TEST_ASSERT_TRUE(request.has_value());
    assertRequest(*request, 4, ConnectionParamsMode::LOW_POWER);
}

static void test_latest_mode_already_satisfied_at_timeout_needs_no_submission()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER));
    TEST_ASSERT_TRUE(serviceAt(controller, ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS).has_value());

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    controller.recordUpdate(4, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);

    const uint32_t laneExpiry =
        ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS;
    const auto afterTimeout = serviceAt(controller, laneExpiry);
    TEST_ASSERT_FALSE(afterTimeout.has_value());
}

static void test_low_power_waits_for_quiet_period()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));

    int32_t delay = 0;
    TEST_ASSERT_FALSE(serviceAt(controller, 100, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS, delay);

    const auto request = serviceAt(controller, 100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS);
    TEST_ASSERT_TRUE(request.has_value());
    assertRequest(*request, 4, ConnectionParamsMode::LOW_POWER);
}

static void test_high_throughput_replaces_deferred_low_power()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));
    TEST_ASSERT_FALSE(serviceAt(controller, 200).has_value());

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT, 300));
    const auto request = serviceAt(controller, 300);
    TEST_ASSERT_TRUE(request.has_value());
    assertRequest(*request, 4, ConnectionParamsMode::HIGH_THROUGHPUT);
}

static void test_repeated_low_power_request_restarts_quiet_period()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 600));

    int32_t delay = 0;
    TEST_ASSERT_FALSE(serviceAt(controller, 1000, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS - 400, delay);
    TEST_ASSERT_TRUE(serviceAt(controller, 1600).has_value());
}

static void test_low_power_quiet_period_is_millis_wrap_safe()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(4);

    constexpr uint32_t start = UINT32_MAX - 100;
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, start));
    TEST_ASSERT_FALSE(serviceAt(controller, 50).has_value());
    TEST_ASSERT_TRUE(serviceAt(controller, start + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS).has_value());
}

static void test_submission_rejection_retries_same_request_after_backoff()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(9);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));

    int32_t delay = 0;
    const auto first = serviceAt(controller, 100, false, &delay);
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::SUBMISSION_RETRY_MS, delay);

    TEST_ASSERT_FALSE(
        serviceAt(controller, 100 + ConnectionParamsUpdateController::SUBMISSION_RETRY_MS - 1, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(1, delay);

    const auto retry = serviceAt(controller, 100 + ConnectionParamsUpdateController::SUBMISSION_RETRY_MS);
    TEST_ASSERT_TRUE(retry.has_value());
    assertRequest(*retry, 9, ConnectionParamsMode::HIGH_THROUGHPUT);
}

static void test_submission_backoff_is_millis_wrap_safe()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(9);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));

    constexpr uint32_t start = UINT32_MAX - 100;
    TEST_ASSERT_TRUE(serviceAt(controller, start, false).has_value());

    int32_t delay = 0;
    TEST_ASSERT_FALSE(
        serviceAt(controller, start + ConnectionParamsUpdateController::SUBMISSION_RETRY_MS - 1, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(1, delay);
    TEST_ASSERT_TRUE(serviceAt(controller, start + ConnectionParamsUpdateController::SUBMISSION_RETRY_MS).has_value());
}

static void test_low_power_submission_rejection_retries_after_backoff()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(9);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));

    int32_t delay = 0;
    const auto first = serviceAt(controller, 100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS, false, &delay);
    TEST_ASSERT_TRUE(first.has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::SUBMISSION_RETRY_MS, delay);

    const uint32_t retryAt =
        100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS + ConnectionParamsUpdateController::SUBMISSION_RETRY_MS;
    TEST_ASSERT_FALSE(serviceAt(controller, retryAt - 1, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(1, delay);

    const auto retry = serviceAt(controller, retryAt);
    TEST_ASSERT_TRUE(retry.has_value());
    assertRequest(*retry, 9, ConnectionParamsMode::LOW_POWER);
}

static void test_low_power_request_during_backoff_gets_full_quiet_period()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(9);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100, false).has_value());

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 200));
    int32_t delay = 0;
    TEST_ASSERT_FALSE(serviceAt(controller, 350, true, &delay).has_value());
    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS - 150, delay);

    const auto lowPower = serviceAt(controller, 1200);
    TEST_ASSERT_TRUE(lowPower.has_value());
    assertRequest(*lowPower, 9, ConnectionParamsMode::LOW_POWER);
}

static void test_satisfied_low_power_request_cancels_pending_high_throughput()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    controller.recordUpdate(1, ConnectionParamsUpdateController::LOW_POWER_MIN_INTERVAL, 0);

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_FALSE(requestAt(controller, ConnectionParamsMode::LOW_POWER));
    TEST_ASSERT_FALSE(serviceAt(controller, 1).has_value());
}

static void test_satisfied_high_throughput_request_cancels_deferred_low_power()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    controller.recordUpdate(1, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);

    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::LOW_POWER, 100));
    TEST_ASSERT_FALSE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT, 200));
    TEST_ASSERT_FALSE(serviceAt(controller, 100 + ConnectionParamsUpdateController::LOW_POWER_SETTLE_MS).has_value());
}

static void test_reset_mid_flight_retires_request()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    const auto retired = serviceAt(controller, 100);
    TEST_ASSERT_TRUE(retired.has_value());

    controller.reset();
    controller.recordUpdate(retired->connHandle, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);

    TEST_ASSERT_FALSE(serviceAt(controller, 101).has_value());
}

static void test_submission_is_atomic_with_session_retirement()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));

    std::atomic<bool> resetLockAttempted{false};
    std::atomic<bool> resetFinished{false};
    bool resetLockAttemptObserved = false;
    bool resetBlockedDuringSubmission = false;
    std::thread resetThread;
    const int32_t delay = controller.servicePending(100, [&](const ConnectionParamsRequest &) {
        resetThread = std::thread([&]() {
            controller.resetForTest([&]() { resetLockAttempted = true; });
            resetFinished = true;
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!resetLockAttempted.load() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        resetLockAttemptObserved = resetLockAttempted.load();
        resetBlockedDuringSubmission = resetLockAttemptObserved && !resetFinished.load();
        return true;
    });

    TEST_ASSERT_EQUAL_INT32(ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS, delay);
    resetThread.join();
    TEST_ASSERT_TRUE_MESSAGE(resetLockAttemptObserved, "Timed out waiting for reset lock attempt");
    TEST_ASSERT_TRUE_MESSAGE(resetBlockedDuringSubmission, "Reset completed while submission held the controller lock");
    TEST_ASSERT_TRUE(resetFinished.load());
    TEST_ASSERT_FALSE(serviceAt(controller, 101).has_value());
}

static void test_retired_session_request_cannot_target_reconnect()
{
    ConnectionParamsUpdateController controller;
    const uint32_t retiredGeneration = controller.onConnected(1);

    controller.reset();
    controller.onConnected(1);

    TEST_ASSERT_FALSE(controller.request(retiredGeneration, ConnectionParamsMode::LOW_POWER, 0));
    TEST_ASSERT_FALSE(serviceAt(controller, 0).has_value());
}

static void test_wrong_handle_update_does_not_change_active_request()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_TRUE(serviceAt(controller, 100).has_value());

    controller.recordUpdate(2, ConnectionParamsUpdateController::HIGH_THROUGHPUT_MAX_INTERVAL, 0);
    TEST_ASSERT_FALSE(serviceAt(controller, 200).has_value());

    const auto retry = serviceAt(controller, 100 + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS);
    TEST_ASSERT_TRUE(retry.has_value());
    assertRequest(*retry, 1, ConnectionParamsMode::HIGH_THROUGHPUT);
}

static void test_lane_timeout_is_millis_wrap_safe()
{
    ConnectionParamsUpdateController controller;
    controller.onConnected(1);
    TEST_ASSERT_TRUE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));

    constexpr uint32_t start = UINT32_MAX - 100;
    TEST_ASSERT_TRUE(serviceAt(controller, start).has_value());
    TEST_ASSERT_FALSE(serviceAt(controller, 50).has_value());

    const uint32_t expiry = start + ConnectionParamsUpdateController::UPDATE_LANE_TIMEOUT_MS;
    TEST_ASSERT_TRUE(serviceAt(controller, expiry).has_value());
}

static void test_disconnected_controller_ignores_requests()
{
    ConnectionParamsUpdateController controller;

    TEST_ASSERT_FALSE(requestAt(controller, ConnectionParamsMode::HIGH_THROUGHPUT));
    TEST_ASSERT_FALSE(requestAt(controller, ConnectionParamsMode::NONE));
    TEST_ASSERT_FALSE(serviceAt(controller, 0).has_value());
}

void setUp(void) {}
void tearDown(void) {}

void setup()
{
    initializeTestEnvironment();
    UNITY_BEGIN();
    RUN_TEST(test_connect_waits_for_a_config_profile_request);
    RUN_TEST(test_peer_high_throughput_update_avoids_redundant_submission);
    RUN_TEST(test_satisfying_update_releases_lane_when_no_opposite_mode_is_pending);
    RUN_TEST(test_satisfying_callback_does_not_release_opposite_pending_mode);
    RUN_TEST(test_unmatched_success_does_not_release_active_request);
    RUN_TEST(test_peer_failure_does_not_release_active_request);
    RUN_TEST(test_latest_request_wins_after_lane_timeout);
    RUN_TEST(test_latest_mode_already_satisfied_at_timeout_needs_no_submission);
    RUN_TEST(test_low_power_waits_for_quiet_period);
    RUN_TEST(test_high_throughput_replaces_deferred_low_power);
    RUN_TEST(test_repeated_low_power_request_restarts_quiet_period);
    RUN_TEST(test_low_power_quiet_period_is_millis_wrap_safe);
    RUN_TEST(test_submission_rejection_retries_same_request_after_backoff);
    RUN_TEST(test_submission_backoff_is_millis_wrap_safe);
    RUN_TEST(test_low_power_submission_rejection_retries_after_backoff);
    RUN_TEST(test_low_power_request_during_backoff_gets_full_quiet_period);
    RUN_TEST(test_satisfied_low_power_request_cancels_pending_high_throughput);
    RUN_TEST(test_satisfied_high_throughput_request_cancels_deferred_low_power);
    RUN_TEST(test_reset_mid_flight_retires_request);
    RUN_TEST(test_submission_is_atomic_with_session_retirement);
    RUN_TEST(test_retired_session_request_cannot_target_reconnect);
    RUN_TEST(test_wrong_handle_update_does_not_change_active_request);
    RUN_TEST(test_lane_timeout_is_millis_wrap_safe);
    RUN_TEST(test_disconnected_controller_ignores_requests);
    exit(UNITY_END());
}

void loop() {}
