/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */
#pragma once
#include <app/OTAUserConsentCommon.h>

namespace chip {
namespace ota {

class OTARequestorUserConsentDelegate
{
public:
    virtual ~OTARequestorUserConsentDelegate() = default;

    virtual UserConsentState GetUserConsentState(const UserConsentSubject & subject) = 0;

    // When GetUserConsentState() returns kObtaining this will be called to
    // check if the user consent is granted or denied.
    virtual UserConsentState CheckDeferredUserConsentState() = 0;

    static const char * UserConsentStateToString(UserConsentState state)
    {
        switch (state)
        {
        case kGranted:
            return "Granted";
        case kObtaining:
            return "Obtaining";
        case kDenied:
            return "Denied";
        default:
            return "Unknown";
        }
    }
};

} // namespace ota
} // namespace chip
