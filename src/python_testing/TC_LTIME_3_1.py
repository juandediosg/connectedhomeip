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
#     app-args: --discriminator 1234 --KVS kvs1 --trace-to json:${TRACE_APP}.json
#     script-args: >
#       --storage-path admin_storage.json
#       --commissioning-method on-network
#       --discriminator 1234
#       --passcode 20202021
#       --trace-to json:${TRACE_TEST_JSON}.json
#       --trace-to perfetto:${TRACE_TEST_PERFETTO}.perfetto
#       --PICS src/app/tests/suites/certification/ci-pics-values
#     factory-reset: true
#     quiet: true
# === END CI TEST ARGUMENTS ===

import chip.clusters as Clusters
from chip.testing.matter_testing import MatterBaseTest, TestStep, async_test_body, default_matter_test_main
from mobly import asserts


class TC_LTIME_3_1(MatterBaseTest):
    def desc_TC_LTIME_3_1(self) -> str:
        return "[TC-LTIME-3.1] Read and Write Time Format Localization Cluster Attributes [DUT as Server]"

    def pics_TC_LTIME_3_1(self):
        """Return the PICS definitions associated with this test."""
        pics = [
            "LTIME.S",      # Time Format Localization as a Server
        ]
        return pics

    def steps_TC_LTIME_3_1(self) -> list[TestStep]:
        steps = [
            TestStep(0, "Commissioning, already done", is_commissioning=True),
            TestStep(1, "TH reads HourFormat attribute from DUT"),
            TestStep(2, "If (LTIME.S.M.12Hr) TH writes 0 to HourFormat attribute"),
            TestStep(3, "TH reads HourFormat attribute"),
            TestStep(4, "If (LTIME.S.M.24Hr) TH writes 1 to HourFormat attribute"),
            TestStep(5, "TH reads HourFormat attribute"),
            TestStep(6, "TH reads ActiveCalendarType attribute from DUT"),
            TestStep(7, "TH reads SupportedCalendarTypes attribute from DUT"),
            TestStep(8, "TH writes value in PIXIT.LTIME.SCT to ActiveCalendarType attribute, followed by reading the ActiveCalendarType attribute value"),
            TestStep(9, "Repeat step 8 for all the values in PIXIT.LTIME.SCT"),
            TestStep(10, "TH writes 50 to ActiveCalendarType attribute"),
            TestStep(11, "TH writes 5 to HourFormat attribute"),
        ]
        return steps

    @async_test_body
    async def test_TC_LTIME_3_1(self):
        self.step(0)

    # Read the Spteps
        self.step(1)
        self.step(2)
        self.step(3)
        self.step(4)
        self.step(5)
        self.step(6)
        self.step(7)
        self.step(8)
        self.step(9)
        self.step(10)
        self.step(11)


if __name__ == "__main__":
    default_matter_test_main()
