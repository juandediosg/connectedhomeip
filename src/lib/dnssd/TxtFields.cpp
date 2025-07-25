/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
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

#include "TxtFields.h"

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdio>
#include <inttypes.h>
#include <limits>
#include <stdlib.h>
#include <string.h>

#include <lib/core/CHIPSafeCasts.h>
#include <lib/dnssd/Advertiser.h>
#include <lib/dnssd/Resolver.h>
#include <lib/support/BytesToHex.h>
#include <lib/support/CHIPMemString.h>
#include <lib/support/SafeInt.h>

namespace chip {
namespace Dnssd {

namespace Internal {

constexpr uint8_t kTCPClient = 1;
constexpr uint8_t kTCPServer = 2;

namespace {

char SafeToLower(uint8_t ch)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}
bool IsKey(const ByteSpan & key, const char * desired)
{
    if (key.size() != strlen(desired))
    {
        return false;
    }

    auto desired_bytes = Uint8::from_const_char(desired);
    for (size_t i = 0; i < key.size(); ++i)
    {
        if (SafeToLower(key.data()[i]) != SafeToLower(desired_bytes[i]))
        {
            return false;
        }
    }
    return true;
}

uint32_t MakeU32FromAsciiDecimal(const ByteSpan & val, uint32_t defaultValue = 0)
{
    // +1 because `digits10` means the number of decimal digits that fit in `uint32_t`,
    // not how many digits are enough to represent any `uint32_t`
    // +1 for null-terminator
    char nullTerminatedValue[std::numeric_limits<uint32_t>::digits10 + 2];

    // value is too long to store `uint32_t`
    if (val.size() >= sizeof(nullTerminatedValue))
        return defaultValue;

    // value contains leading zeros
    if (val.size() > 1 && *val.data() == static_cast<uint8_t>('0'))
        return defaultValue;

    Platform::CopyString(nullTerminatedValue, sizeof(nullTerminatedValue), val);

    char * endPtr;
    unsigned long num = strtoul(nullTerminatedValue, &endPtr, 10);

    if (endPtr > nullTerminatedValue && *endPtr == '\0' && num != ULONG_MAX && CanCastTo<uint32_t>(num))
        return static_cast<uint32_t>(num);

    return defaultValue;
}

uint16_t MakeU16FromAsciiDecimal(const ByteSpan & val)
{
    const uint32_t num = MakeU32FromAsciiDecimal(val);
    return CanCastTo<uint16_t>(num) ? static_cast<uint16_t>(num) : 0;
}

uint8_t MakeU8FromAsciiDecimal(const ByteSpan & val)
{
    const uint32_t num = MakeU32FromAsciiDecimal(val);
    return CanCastTo<uint8_t>(num) ? static_cast<uint8_t>(num) : 0;
}

bool MakeBoolFromAsciiDecimal(const ByteSpan & val)
{
    return val.size() == 1 && static_cast<char>(*val.data()) == '1';
}

std::optional<bool> MakeOptionalBoolFromAsciiDecimal(const ByteSpan & val)
{
    char character = static_cast<char>(*val.data());
    if (val.size() == 1 && ((character == '1') || (character == '0')))
    {
        return std::make_optional(character == '1');
    }
    return std::nullopt;
}

size_t GetPlusSignIdx(const ByteSpan & value)
{
    // First value is the vendor id, second (after the +) is the product.
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (static_cast<char>(value.data()[i]) == '+')
        {
            return i;
        }
    }
    return value.size();
}

} // namespace

uint16_t GetProduct(const ByteSpan & value)
{
    size_t plussign = GetPlusSignIdx(value);
    if (plussign < value.size() - 1)
    {
        const uint8_t * productStrStart = value.data() + plussign + 1;
        size_t productStrLen            = value.size() - plussign - 1;
        return MakeU16FromAsciiDecimal(ByteSpan(productStrStart, productStrLen));
    }
    return 0;
}

uint16_t GetVendor(const ByteSpan & value)
{
    size_t plussign = GetPlusSignIdx(value);
    return MakeU16FromAsciiDecimal(ByteSpan(value.data(), plussign));
}

uint16_t GetLongDiscriminator(const ByteSpan & value)
{
    return MakeU16FromAsciiDecimal(value);
}

uint8_t GetCommissioningMode(const ByteSpan & value)
{
    return MakeU8FromAsciiDecimal(value);
}

#if CHIP_DEVICE_CONFIG_ENABLE_JOINT_FABRIC
BitFlags<JointFabricMode> GetJointFabricMode(const ByteSpan & value)
{
    return BitFlags<JointFabricMode>(MakeU8FromAsciiDecimal(value));
}
#endif // CHIP_DEVICE_CONFIG_ENABLE_JOINT_FABRIC

uint32_t GetDeviceType(const ByteSpan & value)
{
    return MakeU32FromAsciiDecimal(value);
}

void GetDeviceName(const ByteSpan & value, char * name)
{
    Platform::CopyString(name, kMaxDeviceNameLen + 1, value);
}

void GetRotatingDeviceId(const ByteSpan & value, uint8_t * rotatingId, size_t * len)
{
    *len = Encoding::HexToBytes(reinterpret_cast<const char *>(value.data()), value.size(), rotatingId, kMaxRotatingIdLen);
}

uint16_t GetPairingHint(const ByteSpan & value)
{
    return MakeU16FromAsciiDecimal(value);
}

void GetPairingInstruction(const ByteSpan & value, char * pairingInstruction)
{
    Platform::CopyString(pairingInstruction, kMaxPairingInstructionLen + 1, value);
}

uint8_t GetCommissionerPasscode(const ByteSpan & value)
{
    return MakeBoolFromAsciiDecimal(value);
}

std::optional<System::Clock::Milliseconds32> GetRetryInterval(const ByteSpan & value)
{
    const auto undefined     = std::numeric_limits<uint32_t>::max();
    const auto retryInterval = MakeU32FromAsciiDecimal(value, undefined);

    if (retryInterval != undefined && retryInterval <= kMaxRetryInterval.count())
        return std::make_optional(System::Clock::Milliseconds32(retryInterval));

    return std::nullopt;
}

std::optional<System::Clock::Milliseconds16> GetRetryActiveThreshold(const ByteSpan & value)
{
    const auto retryInterval = MakeU16FromAsciiDecimal(value);

    if (retryInterval == 0)
    {
        return std::nullopt;
    }

    return std::make_optional(System::Clock::Milliseconds16(retryInterval));
}

TxtFieldKey GetTxtFieldKey(const ByteSpan & key)
{
    for (auto & info : txtFieldInfo)
    {
        if (IsKey(key, info.keyStr))
        {
            return info.key;
        }
    }
    return TxtFieldKey::kUnknown;
}

} // namespace Internal

void FillNodeDataFromTxt(const ByteSpan & key, const ByteSpan & val, CommissionNodeData & nodeData)
{
    TxtFieldKey keyType = Internal::GetTxtFieldKey(key);
    switch (keyType)
    {
    case TxtFieldKey::kLongDiscriminator:
        nodeData.longDiscriminator = Internal::GetLongDiscriminator(val);
        break;
    case TxtFieldKey::kVendorProduct:
        nodeData.vendorId  = Internal::GetVendor(val);
        nodeData.productId = Internal::GetProduct(val);
        break;
    case TxtFieldKey::kCommissioningMode:
        nodeData.commissioningMode = Internal::GetCommissioningMode(val);
        break;
    case TxtFieldKey::kDeviceType:
        nodeData.deviceType = Internal::GetDeviceType(val);
        break;
    case TxtFieldKey::kDeviceName:
        Internal::GetDeviceName(val, nodeData.deviceName);
        break;
    case TxtFieldKey::kRotatingDeviceId:
        Internal::GetRotatingDeviceId(val, nodeData.rotatingId, &nodeData.rotatingIdLen);
        break;
    case TxtFieldKey::kPairingInstruction:
        Internal::GetPairingInstruction(val, nodeData.pairingInstruction);
        break;
    case TxtFieldKey::kPairingHint:
        nodeData.pairingHint = Internal::GetPairingHint(val);
        break;
    case TxtFieldKey::kCommissionerPasscode:
        nodeData.supportsCommissionerGeneratedPasscode = Internal::GetCommissionerPasscode(val);
        break;
#if CHIP_DEVICE_CONFIG_ENABLE_JOINT_FABRIC
    case TxtFieldKey::kJointFabricMode:
        nodeData.jointFabricMode = Internal::GetJointFabricMode(val);
        break;
#endif // CHIP_DEVICE_CONFIG_ENABLE_JOINT_FABRIC
    default:
        FillNodeDataFromTxt(key, val, static_cast<CommonResolutionData &>(nodeData));
        break;
    }
}

void FillNodeDataFromTxt(const ByteSpan & key, const ByteSpan & value, CommonResolutionData & nodeData)
{
    switch (Internal::GetTxtFieldKey(key))
    {
    case TxtFieldKey::kSessionIdleInterval:
        nodeData.mrpRetryIntervalIdle = Internal::GetRetryInterval(value);
        break;
    case TxtFieldKey::kSessionActiveInterval:
        nodeData.mrpRetryIntervalActive = Internal::GetRetryInterval(value);
        break;
    case TxtFieldKey::kSessionActiveThreshold:
        nodeData.mrpRetryActiveThreshold = Internal::GetRetryActiveThreshold(value);
        break;
    case TxtFieldKey::kTcpSupported: {
        // bit 0 is reserved and deprecated
        uint8_t support            = Internal::MakeU8FromAsciiDecimal(value);
        nodeData.supportsTcpClient = (support & (1 << Internal::kTCPClient)) != 0;
        nodeData.supportsTcpServer = (support & (1 << Internal::kTCPServer)) != 0;
        break;
    }
    case TxtFieldKey::kLongIdleTimeICD:
        nodeData.isICDOperatingAsLIT = Internal::MakeOptionalBoolFromAsciiDecimal(value);
        break;
    default:
        break;
    }
}

} // namespace Dnssd
} // namespace chip
