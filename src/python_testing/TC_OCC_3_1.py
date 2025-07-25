#
#    Copyright (c) 2024 Project CHIP Authors
#    All rights reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License");
#    you may not use this file except in compliance with the License.
#    You may obtain a copy of the License at
#
#        http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS,
#    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#    See the License for the specific language governing permissions and
#    limitations under the License.
#
# === BEGIN CI TEST ARGUMENTS ===
# test-runner-runs:
#   run1:
#     app: ${ALL_CLUSTERS_APP}
#     app-args: --discriminator 1234 --KVS kvs1 --trace-to json:${TRACE_APP}.json --app-pipe /tmp/occ_3_1_fifo
#     script-args: >
#       --storage-path admin_storage.json
#       --commissioning-method on-network
#       --discriminator 1234
#       --passcode 20202021
#       --trace-to json:${TRACE_TEST_JSON}.json
#       --trace-to perfetto:${TRACE_TEST_PERFETTO}.perfetto
#       --endpoint 1
#       --app-pipe /tmp/occ_3_1_fifo
#       --bool-arg simulate_occupancy:true
#     factory-reset: true
#     quiet: true
# === END CI TEST ARGUMENTS ===
#
#  There are CI issues to be followed up for the test cases below that implements manually controlling sensor device for
#  the occupancy state ON/OFF change.
#  [TC-OCC-3.1] test procedure step 3, 4
#  [TC-OCC-3.2] test precedure step 3a, 3c

import logging
import time
from typing import Any, Optional

import chip.clusters as Clusters
from chip.interaction_model import Status
from chip.testing.event_attribute_reporting import AttributeSubscriptionHandler, EventSubscriptionHandler
from chip.testing.matter_testing import MatterBaseTest, TestStep, async_test_body, default_matter_test_main
from mobly import asserts


class TC_OCC_3_1(MatterBaseTest):
    def setup_test(self):
        super().setup_test()
        self.is_ci = self.matter_test_config.global_test_params.get('simulate_occupancy', False)

    async def read_occ_attribute_expect_success(self, attribute):
        cluster = Clusters.Objects.OccupancySensing
        endpoint = self.get_endpoint()
        return await self.read_single_attribute_check_success(endpoint=endpoint, cluster=cluster, attribute=attribute)

    async def write_hold_time(self, hold_time: Optional[Any]) -> Status:
        dev_ctrl = self.default_controller
        node_id = self.dut_node_id
        endpoint = self.get_endpoint()

        cluster = Clusters.OccupancySensing
        write_result = await dev_ctrl.WriteAttribute(node_id, [(endpoint, cluster.Attributes.HoldTime(hold_time))])
        return write_result[0].Status

    def desc_TC_OCC_3_1(self) -> str:
        return "[TC-OCC-3.1] Primary functionality with server as DUT"

    def steps_TC_OCC_3_1(self) -> list[TestStep]:
        steps = [
            TestStep(1, "Commission DUT to TH.", is_commissioning=True),
            TestStep(2, "If HoldTime is supported, TH writes HoldTime attribute to 10 sec on DUT."),
            TestStep(3, "Prompt operator to await until DUT occupancy changes to unoccupied state."),
            TestStep(4, "TH subscribes to Occupancy sensor attributes and events."),
            TestStep("5a", "Prompt operator to trigger occupancy change."),
            TestStep("5b", "TH reads Occupancy attribute from DUT. Verify occupancy changed to occupied and Occupancy attribute was reported as occupied."),
            TestStep("5c", "If supported, verify OccupancyChangedEvent was reported as occupied."),
            TestStep(6, "If HoldTime is supported, wait for HoldTime, otherwise prompt operator to wait until no longer occupied."),
            TestStep("7a", "TH reads Occupancy attribute from DUT. Verify occupancy changed to unoccupied and Occupancy attribute was reported as unoccupied."),
            TestStep("7b", "If supported, verify OccupancyChangedEvent was reported as unoccupied."),
        ]
        return steps

    def pics_TC_OCC_3_1(self) -> list[str]:
        pics = [
            "OCC.S",
        ]
        return pics

    @async_test_body
    async def test_TC_OCC_3_1(self):
        hold_time = 10 if not self.is_ci else 1.0  # 10 seconds for occupancy state hold time
        self.step(1)  # Commissioning already done

        self.step(2)

        cluster = Clusters.OccupancySensing
        attributes = cluster.Attributes
        attribute_list = await self.read_occ_attribute_expect_success(attribute=attributes.AttributeList)

        has_hold_time = attributes.HoldTime.attribute_id in attribute_list
        occupancy_event_supported = self.check_pics("OCC.M.OccupancyChange") or self.is_ci

        if has_hold_time:
            # write HoldTimeLimits HoldtimeMin to be 10 sec.
            await self.write_single_attribute(cluster.Attributes.HoldTime(hold_time))
            holdtime_dut = await self.read_occ_attribute_expect_success(attribute=attributes.HoldTime)
            asserts.assert_equal(holdtime_dut, hold_time, "Hold time read-back does not match hold time written")
        else:
            logging.info("No HoldTime attribute supports. Will test only occupancy attribute triggering functionality only.")

        self.step(3)

        if self.is_ci:
            # CI call to trigger unoccupied.
            self.write_to_app_pipe({"Name": "SetOccupancy", "EndpointId": 1, "Occupancy": 0})
        else:
            self.wait_for_user_input(
                prompt_msg="Type any letter and press ENTER after the sensor occupancy is unoccupied state (occupancy attribute = 0)")

        # check sensor occupancy state is 0 for the next test step
        occupancy_dut = await self.read_occ_attribute_expect_success(attribute=attributes.Occupancy)
        asserts.assert_equal(occupancy_dut, 0, "Occupancy attribute is not unoccupied.")

        self.step(4)
        # Setup Occupancy attribute subscription here
        endpoint_id = self.get_endpoint()
        node_id = self.dut_node_id
        dev_ctrl = self.default_controller
        attrib_listener = AttributeSubscriptionHandler(expected_cluster=cluster)
        await attrib_listener.start(dev_ctrl, node_id, endpoint=endpoint_id, min_interval_sec=0, max_interval_sec=30)

        if occupancy_event_supported:
            event_listener = EventSubscriptionHandler(expected_cluster=cluster)
            await event_listener.start(dev_ctrl, node_id, endpoint=endpoint_id, min_interval_sec=0, max_interval_sec=30)

        self.step("5a")
        # CI call to trigger on
        if self.is_ci:
            self.write_to_app_pipe({"Name": "SetOccupancy", "EndpointId": 1, "Occupancy": 1})
        else:
            # Trigger occupancy sensor to change Occupancy attribute value to 1 => TESTER ACTION on DUT
            self.wait_for_user_input(prompt_msg="Type any letter and press ENTER after a sensor occupancy is triggered.")

        # And then check if Occupancy attribute has changed.
        self.step("5b")
        occupancy_dut = await self.read_occ_attribute_expect_success(attribute=attributes.Occupancy)
        asserts.assert_equal(occupancy_dut, 1, "Occupancy state is not changed to 1")

        # subscription verification
        post_prompt_settle_delay_seconds = 1.0 if self.is_ci else 10.0
        attrib_listener.await_sequence_of_reports(attribute=cluster.Attributes.Occupancy, sequence=[
                                                  1], timeout_sec=post_prompt_settle_delay_seconds)

        if occupancy_event_supported:
            self.step("5c")
            event = event_listener.wait_for_event_report(
                cluster.Events.OccupancyChanged, timeout_sec=post_prompt_settle_delay_seconds)
            asserts.assert_equal(event.occupancy, 1, "Unexpected occupancy on OccupancyChanged")
        else:
            self.skip_step("5c")

        self.step(6)
        if self.is_ci:
            # CI call to trigger unoccupied.
            self.write_to_app_pipe({"Name": "SetOccupancy", "EndpointId": 1, "Occupancy": 0})

        if has_hold_time:
            time.sleep(hold_time + 2.0)  # add some extra 2 seconds to ensure hold time has passed.
        else:
            self.wait_for_user_input(
                prompt_msg="Type any letter and press ENTER after the sensor occupancy is back to unoccupied state (occupancy attribute = 0)")

        # Check if Occupancy attribute is back to 0 after HoldTime attribute period
        # Tester should not be triggering the sensor for this test step.
        self.step("7a")
        occupancy_dut = await self.read_occ_attribute_expect_success(attribute=attributes.Occupancy)
        asserts.assert_equal(occupancy_dut, 0, "Occupancy state is not back to 0 after HoldTime period")

        attrib_listener.await_sequence_of_reports(attribute=cluster.Attributes.Occupancy, sequence=[
            0], timeout_sec=post_prompt_settle_delay_seconds)

        if occupancy_event_supported:
            self.step("7b")
            event = event_listener.wait_for_event_report(
                cluster.Events.OccupancyChanged, timeout_sec=post_prompt_settle_delay_seconds)
            asserts.assert_equal(event.occupancy, 0, "Unexpected occupancy on OccupancyChanged")
        else:
            self.skip_step("7b")


if __name__ == "__main__":
    default_matter_test_main()
