/*
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

#include <app/clusters/ota-requestor/OTADownloader.h>
#include <app/clusters/ota-requestor/OTARequestorInterface.h>

extern "C" {
#if CHIP_DEVICE_LAYER_TARGET_BL616
#include <bflb_ota.h>
#include <bl_sys.h>
#else
#include <hal_sys.h>
#include <hosal_ota.h>
#endif
}
#include "OTAImageProcessorImpl.h"

#if CHIP_DEVICE_LAYER_TARGET_BL602 || CHIP_DEVICE_LAYER_TARGET_BL702 || CHIP_DEVICE_LAYER_TARGET_BL702L
extern "C" void hal_reboot(void);
#endif

using namespace chip::System;

namespace chip {

bool OTAImageProcessorImpl::IsFirstImageRun()
{
    OTARequestorInterface * requestor = chip::GetRequestorInstance();
    if (requestor == nullptr)
    {
        return false;
    }

    return requestor->GetCurrentUpdateState() == OTARequestorInterface::OTAUpdateStateEnum::kApplying;
}

CHIP_ERROR OTAImageProcessorImpl::ConfirmCurrentImage()
{
    OTARequestorInterface * requestor = chip::GetRequestorInstance();
    if (requestor == nullptr)
    {
        return CHIP_ERROR_INTERNAL;
    }

    uint32_t currentVersion;
    uint32_t targetVersion = requestor->GetTargetVersion();
    ReturnErrorOnFailure(DeviceLayer::ConfigurationMgr().GetSoftwareVersion(currentVersion));
    if (currentVersion != targetVersion)
    {
        ChipLogError(SoftwareUpdate, "Current software version = %" PRIu32 ", expected software version = %" PRIu32, currentVersion,
                     targetVersion);
        return CHIP_ERROR_INCORRECT_STATE;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::PrepareDownload()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandlePrepareDownload, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Finalize()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleFinalize, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Apply()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleApply, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::Abort()
{
    DeviceLayer::PlatformMgr().ScheduleWork(HandleAbort, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::ProcessBlock(ByteSpan & block)
{
    if ((nullptr == block.data()) || block.empty())
    {
        return CHIP_ERROR_INVALID_ARGUMENT;
    }

    // Store block data for HandleProcessBlock to access
    CHIP_ERROR err = SetBlock(block);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "Cannot set block data: %" CHIP_ERROR_FORMAT, err.Format());
    }

    DeviceLayer::PlatformMgr().ScheduleWork(HandleProcessBlock, reinterpret_cast<intptr_t>(this));
    return CHIP_NO_ERROR;
}

void OTAImageProcessorImpl::HandlePrepareDownload(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        ChipLogError(SoftwareUpdate, "ImageProcessor context is null");
        return;
    }
    else if (imageProcessor->mDownloader == nullptr)
    {
        ChipLogError(SoftwareUpdate, "mDownloader is null");
        return;
    }

    imageProcessor->mParams.downloadedBytes = 0;
    imageProcessor->mParams.totalFileBytes  = 0;
    imageProcessor->mHeaderParser.Init();

    imageProcessor->mDownloader->OnPreparedForDownload(CHIP_NO_ERROR);
}

void OTAImageProcessorImpl::HandleFinalize(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        return;
    }

#if CHIP_DEVICE_LAYER_TARGET_BL616
    if (bflb_ota_check() < 0)
#else
    if (hosal_ota_check() < 0)
#endif
    {
        imageProcessor->mDownloader->EndDownload(CHIP_ERROR_WRITE_FAILED);
        ChipLogProgress(SoftwareUpdate, "OTA image verification error");
    }
    else
    {
        ChipLogProgress(SoftwareUpdate, "OTA image downloaded");
    }

    imageProcessor->ReleaseBlock();
}

void OTAImageProcessorImpl::HandleApply(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        return;
    }

#if CHIP_DEVICE_LAYER_TARGET_BL616
    bflb_ota_apply();
#else
    hosal_ota_apply(0);
#endif
    DeviceLayer::SystemLayer().StartTimer(
        System::Clock::Seconds32(OTA_AUTO_REBOOT_DELAY),
        [](Layer *, void *) {
            ChipLogProgress(SoftwareUpdate, "Rebooting...");
#if CHIP_DEVICE_LAYER_TARGET_BL616
            bl_sys_reset_por();
#else
            hal_reboot();
#endif
        },
        nullptr);
}

void OTAImageProcessorImpl::HandleAbort(intptr_t context)
{
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);
    if (imageProcessor == nullptr)
    {
        return;
    }

#if CHIP_DEVICE_LAYER_TARGET_BL616
    bflb_ota_abort();
#else
    hosal_ota_abort();
#endif

    imageProcessor->ReleaseBlock();
}

void OTAImageProcessorImpl::HandleProcessBlock(intptr_t context)
{
    OTAImageHeader header;
    CHIP_ERROR error;
    auto * imageProcessor = reinterpret_cast<OTAImageProcessorImpl *>(context);

    if (imageProcessor == nullptr)
    {
        ChipLogError(SoftwareUpdate, "ImageProcessor context is null");
        return;
    }
    else if (imageProcessor->mDownloader == nullptr)
    {
        ChipLogError(SoftwareUpdate, "mDownloader is null");
        return;
    }

    ByteSpan block = imageProcessor->mBlock;
    if (imageProcessor->mHeaderParser.IsInitialized())
    {
        error = imageProcessor->mHeaderParser.AccumulateAndDecode(block, header);
        if (CHIP_ERROR_BUFFER_TOO_SMALL == error)
        {
            return;
        }
        else if (CHIP_NO_ERROR != error)
        {
            ChipLogError(SoftwareUpdate, "Matter image header parser error: %" CHIP_ERROR_FORMAT, error.Format());
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_INVALID_FILE_IDENTIFIER);
            imageProcessor->mHeaderParser.Clear();
            return;
        }

        ChipLogProgress(SoftwareUpdate, "Image Header software version: %ld payload size: %lu", header.mSoftwareVersion,
                        (long unsigned int) header.mPayloadSize);
        imageProcessor->mParams.totalFileBytes = header.mPayloadSize;
        imageProcessor->mHeaderParser.Clear();

#if CHIP_DEVICE_LAYER_TARGET_BL616
        if (bflb_ota_start(header.mPayloadSize) < 0)
#else
        if (hosal_ota_start(header.mPayloadSize) < 0)
#endif
        {
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_OPEN_FAILED);
            return;
        }
    }

    if (imageProcessor->mParams.totalFileBytes)
    {
#if CHIP_DEVICE_LAYER_TARGET_BL616
        if (bflb_ota_update(imageProcessor->mParams.totalFileBytes, imageProcessor->mParams.downloadedBytes,
                            (uint8_t *) block.data(), block.size()) < 0)
#else
        if (hosal_ota_update(imageProcessor->mParams.totalFileBytes, imageProcessor->mParams.downloadedBytes,
                             (uint8_t *) block.data(), block.size()) < 0)
#endif
        {
            imageProcessor->mDownloader->EndDownload(CHIP_ERROR_WRITE_FAILED);
            return;
        }
        imageProcessor->mParams.downloadedBytes += block.size();
    }

    imageProcessor->mDownloader->FetchNextData();
}

// Store block data for HandleProcessBlock to access
CHIP_ERROR OTAImageProcessorImpl::SetBlock(ByteSpan & block)
{
    if ((block.data() == nullptr) || block.empty())
    {
        return CHIP_NO_ERROR;
    }

    // Allocate memory for block data if we don't have enough already
    if (mBlock.size() < block.size())
    {
        ReleaseBlock();

        mBlock = MutableByteSpan(static_cast<uint8_t *>(chip::Platform::MemoryAlloc(block.size())), block.size());
        if (mBlock.data() == nullptr)
        {
            return CHIP_ERROR_NO_MEMORY;
        }
    }

    // Store the actual block data
    CHIP_ERROR err = CopySpanToMutableSpan(block, mBlock);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(SoftwareUpdate, "Cannot copy block data: %" CHIP_ERROR_FORMAT, err.Format());
        return err;
    }

    return CHIP_NO_ERROR;
}

CHIP_ERROR OTAImageProcessorImpl::ReleaseBlock()
{
    if (mBlock.data() != nullptr)
    {
        chip::Platform::MemoryFree(mBlock.data());
    }

    mBlock = MutableByteSpan();
    return CHIP_NO_ERROR;
}

} // namespace chip
