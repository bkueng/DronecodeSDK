#include <iostream>
#include <future>
#include "log.h"
#include "integration_test_helper.h"
#include "dronecode_sdk.h"
#include "plugins/mavlink_backdoor/mavlink_backdoor.h"

using namespace dronecode_sdk;

TEST_F(SitlTest, MavlinkBackdoor)
{
    DronecodeSDK dc;
    ASSERT_EQ(dc.add_udp_connection(), ConnectionResult::SUCCESS);

    {
        LogInfo() << "Waiting to discover vehicle";
        std::promise<void> prom;
        std::future<void> fut = prom.get_future();
        dc.register_on_discover([&prom](uint64_t uuid) {
            prom.set_value();
            UNUSED(uuid);
        });
        ASSERT_EQ(fut.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    }

    System &system = dc.system();
    auto mavlink_backdoor = std::make_shared<MavlinkBackdoor>(system);

    {
        mavlink_message_t message;
        mavlink_msg_command_long_pack(mavlink_backdoor->get_our_sysid(),
                                      mavlink_backdoor->get_our_compid(),
                                      &message,
                                      mavlink_backdoor->get_target_sysid(),
                                      mavlink_backdoor->get_target_compid(),
                                      MAV_CMD_SET_MESSAGE_INTERVAL,
                                      0, // first transmission
                                      float(MAVLINK_MSG_ID_HIGHRES_IMU),
                                      2000.0f, // 50 Hz
                                      NAN,
                                      NAN,
                                      NAN,
                                      NAN,
                                      NAN);
        mavlink_backdoor->send_message(message);
    }

    {
        std::promise<void> prom;
        std::future<void> fut = prom.get_future();
        unsigned counter = 0;

        mavlink_backdoor->subscribe_message_async(
            MAVLINK_MSG_ID_HIGHRES_IMU, [&prom, &counter](const mavlink_message_t &message) {
                mavlink_highres_imu_t highres_imu;
                mavlink_msg_highres_imu_decode(&message, &highres_imu);

                LogInfo() << "HIGHRES_IMU.temperature: " << highres_imu.temperature << " degrees C";
                if (++counter > 100) {
                    prom.set_value();
                };
            });

        // At 50 Hz we should have received 100 temperature measurements in 2 seconds.
        // After 3 seconds we give up.
        EXPECT_EQ(fut.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    }
}
