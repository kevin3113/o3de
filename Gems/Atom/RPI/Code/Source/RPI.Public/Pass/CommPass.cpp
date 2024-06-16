/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/RHI/CommandList.h>
#include <Atom/RHI/ShaderResourceGroup.h>
#include <Atom/RHI/DrawListTagRegistry.h>
#include <Atom/RHI/RHISystemInterface.h>

#include <Atom/RPI.Public/RenderPipeline.h>
#include <Atom/RPI.Public/RPISystemInterface.h>
#include <Atom/RPI.Public/Pass/PassDistSystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/View.h>
#include <Atom/RPI.Public/Pass/CommPass.h>
#include <Atom/RPI.Public/Pass/PassUtils.h>

namespace AZ
{
    namespace RPI
    {
        // --- Creation & Initialization ---

        Ptr<CommPass> CommPass::Create(const PassDescriptor& descriptor)
        {
            Ptr<CommPass> pass = aznew CommPass(descriptor);
            return pass;
        }

        CommPass::CommPass(const PassDescriptor& descriptor)
            : RenderPass(descriptor)
        {
            const CommPassData* commData = PassUtils::GetPassData<CommPassData>(descriptor);

            if (commData)
            {
                m_data = *commData;

                if (commData->m_useCopyQueue)
                {
                    m_hardwareQueueClass = RHI::HardwareQueueClass::Copy;
                }
            }
        }

        RHI::CopyItemType CommPass::GetCopyItemType()
        {
            RHI::AttachmentType inputType = GetInputBinding(0).GetAttachment()->GetAttachmentType();
            RHI::AttachmentType outputType = GetOutputBinding(0).GetAttachment()->GetAttachmentType();

            RHI::CopyItemType copyType = RHI::CopyItemType::Invalid;

            if (inputType == RHI::AttachmentType::Buffer && outputType == RHI::AttachmentType::Buffer)
            {
                copyType = RHI::CopyItemType::Buffer;
            }
            else if (inputType == RHI::AttachmentType::Image && outputType == RHI::AttachmentType::Image)
            {
                copyType = RHI::CopyItemType::Image;
            }
            else if (inputType == RHI::AttachmentType::Buffer && outputType == RHI::AttachmentType::Image)
            {
                copyType = RHI::CopyItemType::BufferToImage;
            }
            else if (inputType == RHI::AttachmentType::Image && outputType == RHI::AttachmentType::Buffer)
            {
                copyType = RHI::CopyItemType::ImageToBuffer;
            }

            return copyType;
        }

        // --- Pass behavior overrides ---

        void CommPass::BuildInternal()
        {
        }

        // --- Scope producer functions ---

        void CommPass::SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph)
        {
            RenderPass::SetupFrameGraphDependencies(frameGraph);
        }

        void CommPass::CompileResources(const RHI::FrameGraphCompileContext& context)
        {
            if (!m_data.m_submit)
            {
                printf("CommPass::CompileResources no submit no need compile!\n");
                return;
            }
            RHI::CopyItemType copyType = GetCopyItemType();
            switch (copyType)
            {
            case AZ::RHI::CopyItemType::Buffer:
                CopyBuffer(context);
                break;
            case AZ::RHI::CopyItemType::Image:
                CopyImage(context);
                break;
            case AZ::RHI::CopyItemType::BufferToImage:
                CopyBufferToImage(context);
                break;
            case AZ::RHI::CopyItemType::ImageToBuffer:
                CopyImageToBuffer(context);
                break;
            default:
                break;
            }
        }

        void CommPass::BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context)
        {
            // pending data in
            if (m_data.m_recvData)
            {
                // test
                void *buf[8];
                uint32_t len[8];
                uint32_t count = 0;
                int ret = PassDistSystemInterface::Get()->RecvData(buf, len, 8, &count);
                printf("CommPass recv data message count %u ret %d\n", count, ret);
                for (int i = 0; i < count && i < 8; i++)
                {
                    printf("CommPass recv data message %p len %u\n", buf[i], len[i]);
                    free(buf[i]);
                }
            }

            // build commnad
            if (m_data.m_submit && m_copyItem.m_type != RHI::CopyItemType::Invalid)
            {
                context.GetCommandList()->Submit(m_copyItem);
            }

            // send data to out
            if (m_data.m_sendData)
            {
                // test
                void *data[1];
                uint32_t len[1];
                void *buf = (char *)malloc(1024);
                memset(buf, 0x5a, 1024);
                data[0] = buf;
                len[0] = 1024;
                int ret = PassDistSystemInterface::Get()->SendData(data, len, 1);
                printf("CommPass send data message ret %d\n", ret);
            }
        }

        // --- Copy setup functions ---

        void CommPass::CopyBuffer(const RHI::FrameGraphCompileContext& context)
        {
            RHI::CopyBufferDescriptor copyDesc;

            // Source Buffer
            PassAttachmentBinding& copySource = GetInputBinding(0);
            const AZ::RHI::Buffer* sourceBuffer = context.GetBuffer(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceBuffer = sourceBuffer;
            copyDesc.m_size = static_cast<uint32_t>(sourceBuffer->GetDescriptor().m_byteCount);
            //copyDesc.m_sourceOffset = m_data.m_bufferSourceOffset;

            // Destination Buffer
            PassAttachmentBinding& copyDest = GetOutputBinding(0);
            copyDesc.m_destinationBuffer = context.GetBuffer(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOffset = m_data.m_bufferDestinationOffset;

            m_copyItem = copyDesc;
        }

        void CommPass::CopyImage(const RHI::FrameGraphCompileContext& context)
        {
            RHI::CopyImageDescriptor copyDesc;

            // Source Image
            PassAttachmentBinding& copySource = GetInputBinding(0);
            const AZ::RHI::Image* sourceImage = context.GetImage(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceImage = sourceImage;
            copyDesc.m_sourceSize = sourceImage->GetDescriptor().m_size;
            //copyDesc.m_sourceOrigin = m_data.m_imageSourceOrigin;
            //copyDesc.m_sourceSubresource = m_data.m_imageSourceSubresource;

            // Destination Image
            PassAttachmentBinding& copyDest = GetOutputBinding(0);
            copyDesc.m_destinationImage = context.GetImage(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOrigin = m_data.m_imageDestinationOrigin;
            //copyDesc.m_destinationSubresource = m_data.m_imageDestinationSubresource;

            m_copyItem = copyDesc;
        }

        void CommPass::CopyBufferToImage(const RHI::FrameGraphCompileContext& context)
        {
            RHI::CopyBufferToImageDescriptor copyDesc;

            // Source Buffer
            PassAttachmentBinding& copySource = GetInputBinding(0);
            const AZ::RHI::Buffer* sourceBuffer = context.GetBuffer(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceBuffer = sourceBuffer;
            //copyDesc.m_sourceOffset = m_data.m_bufferSourceOffset;
            //copyDesc.m_sourceBytesPerRow = m_data.m_bufferSourceBytesPerRow;
            //copyDesc.m_sourceBytesPerImage = m_data.m_bufferSourceBytesPerImage;

            // Destination Image
            PassAttachmentBinding& copyDest = GetOutputBinding(0);
            copyDesc.m_destinationImage = context.GetImage(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOrigin = m_data.m_imageDestinationOrigin;
            //copyDesc.m_destinationSubresource = m_data.m_imageDestinationSubresource;
            copyDesc.m_sourceSize = copyDesc.m_destinationImage->GetDescriptor().m_size;

            m_copyItem = copyDesc;
        }

        void CommPass::CopyImageToBuffer(const RHI::FrameGraphCompileContext& context)
        {
            RHI::CopyImageToBufferDescriptor copyDesc;

            // Source Image
            PassAttachmentBinding& copySource = GetInputBinding(0);
            const AZ::RHI::Image* sourceImage = context.GetImage(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceImage = sourceImage;
            copyDesc.m_sourceSize = sourceImage->GetDescriptor().m_size;
            //copyDesc.m_sourceOrigin = m_data.m_imageSourceOrigin;
            //copyDesc.m_sourceSubresource = m_data.m_imageSourceSubresource;

            // Destination Buffer
            PassAttachmentBinding& copyDest = GetOutputBinding(0);
            copyDesc.m_destinationBuffer = context.GetBuffer(copyDest.GetAttachment()->GetAttachmentId());
            printf("CopyImageToBuffer pass [%s] image format %d/%d buf atta id [%s] pointer %p\n",
                GetName().GetCStr(), (int)sourceImage->GetDescriptor().m_format,
                (int)RHI::GetFormatSize(sourceImage->GetDescriptor().m_format),
                copyDest.GetAttachment()->GetAttachmentId().GetCStr(), copyDesc.m_destinationBuffer);
            //print_stack();
            copyDesc.m_destinationOffset = 0;
            copyDesc.m_destinationBytesPerRow = m_data.m_bufferDestinationBytesPerRow;
            copyDesc.m_destinationBytesPerImage = 0;
            copyDesc.m_destinationFormat = sourceImage->GetDescriptor().m_format;

            m_copyItem = copyDesc;
        }

    }   // namespace RPI
}   // namespace AZ
