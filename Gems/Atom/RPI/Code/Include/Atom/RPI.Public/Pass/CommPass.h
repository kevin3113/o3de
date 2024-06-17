/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI/CopyItem.h>
#include <Atom/RHI.Reflect/AttachmentEnums.h>
#include <Atom/RHI.Reflect/Scissor.h>
#include <Atom/RHI.Reflect/Viewport.h>

#include <Atom/RPI.Reflect/Pass/CommPassData.h>

#include <Atom/RPI.Public/Pass/RenderPass.h>

namespace AZ
{
    namespace RPI
    {
        //! A copy pass is a leaf pass (pass with no children) used for copying images and buffers on the GPU.
        class CommPass
            : public RenderPass
        {
            AZ_RPI_PASS(CommPass);

        public:
            AZ_RTTI(CommPass, "{017F438C-2B11-11EF-9E17-0D025945CCB6}", RenderPass);
            AZ_CLASS_ALLOCATOR(CommPass, SystemAllocator);
            virtual ~CommPass() = default;

            static Ptr<CommPass> Create(const PassDescriptor& descriptor);

        protected:
            explicit CommPass(const PassDescriptor& descriptor);

            // Sets up the copy item to perform an image to image copy
            void CopyBuffer(const RHI::FrameGraphCompileContext& context, uint32_t index);
            void CopyImage(const RHI::FrameGraphCompileContext& context, uint32_t index);
            void CopyBufferToImage(const RHI::FrameGraphCompileContext& context, uint32_t index);
            void CopyImageToBuffer(const RHI::FrameGraphCompileContext& context, uint32_t index);

            // Pass behavior overrides
            void BuildInternal() override;

            // Scope producer functions...
            void SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph) override;
            void CompileResources(const RHI::FrameGraphCompileContext& context) override;
            void BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context) override;

            // Retrieves the copy item type based on the input and output attachment type
            RHI::CopyItemType GetCopyItemType(uint32_t index);

            // The copy item submitted to the command list
            AZStd::vector<RHI::CopyItem> m_copyItems;

            // Potential data provided by the PassRequest
            CommPassData m_data;
        };
    }   // namespace RPI
}   // namespace AZ
