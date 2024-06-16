/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI.Reflect/AttachmentEnums.h>
#include <Atom/RHI.Reflect/ImageSubresource.h>
#include <Atom/RHI.Reflect/Origin.h>

#include <Atom/RPI.Reflect/Pass/PassData.h>

namespace AZ
{
    namespace RPI
    {
        //! Custom data for the CopyPass. Should be specified in the PassRequest.
        struct CommPassData
            : public PassData
        {
            AZ_RTTI(CommPassData, "{654770CE-2B11-11EF-9E17-0D025945CCB6}", PassData);
            AZ_CLASS_ALLOCATOR(CommPassData, SystemAllocator);

            CommPassData() = default;
            virtual ~CommPassData() = default;

            static void Reflect(ReflectContext* context);

            // Whether the pass should use the copy queue.
            bool m_useCopyQueue = false;

            bool m_sendData = false;

            bool m_recvData = false;

            bool m_distMain = false;

            bool m_submit = true;
        };
    } // namespace RPI
} // namespace AZ

