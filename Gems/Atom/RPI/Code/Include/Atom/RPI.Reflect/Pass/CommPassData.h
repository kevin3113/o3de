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
        enum class CommOper : uint32_t {
            None = 0,
            CopyInput,
            PrepareInput,
            CopyOutput,
            MergeOutput,
            Count
        };
        //! Custom data for the CopyPass. Should be specified in the PassRequest.
        struct CommPassData
            : public PassData
        {
            AZ_RTTI(CommPassData, "{654770CE-2B11-11EF-9E17-0D025945CCB6}", PassData);
            AZ_CLASS_ALLOCATOR(CommPassData, SystemAllocator);

            CommPassData() = default;
            virtual ~CommPassData() = default;

            static void Reflect(ReflectContext* context);

            bool m_cloneInput = false;

            uint32_t m_splitCnt = 1;

            // Whether the pass should use the copy queue.
            bool m_useCopyQueue = false;

            CommOper m_commOper = CommOper::None;

            bool m_submit = false;
        };
    } // namespace RPI
} // namespace AZ

