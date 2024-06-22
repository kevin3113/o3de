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

        RHI::CopyItemType CommPass::GetCopyItemType(uint32_t index)
        {
            RHI::AttachmentType inputType = GetInputBinding(index).GetAttachment()->GetAttachmentType();
            RHI::AttachmentType outputType = GetOutputBinding(index).GetAttachment()->GetAttachmentType();

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
            if (m_data.m_cloneInput)
            {
                for (uint32_t index = 0; index < GetInputCount(); index++)
                {
                    const Ptr<PassAttachment>& source = GetInputBinding(index).GetAttachment();
                    Ptr<PassAttachment> dest = source->Clone();

                    // Set bind flags to CopyWrite. Other bind flags will be auto-inferred by pass system
                    if (dest->m_descriptor.m_type == RHI::AttachmentType::Image)
                    {
                        dest->m_descriptor.m_image.m_bindFlags = RHI::ImageBindFlags::CopyWrite;
                    }
                    else if (dest->m_descriptor.m_type == RHI::AttachmentType::Buffer)
                    {
                        dest->m_descriptor.m_buffer.m_bindFlags = RHI::BufferBindFlags::CopyWrite;
                    }

                    // Set path name for the new attachment and add it to our attachment list
                    dest->ComputePathName(GetPathName());
                    m_ownedAttachments.push_back(dest);

                    // Set the output binding to the new attachment
                    GetOutputBinding(index).SetAttachment(dest);
                }
            }
#if 0
            for (uint32_t index = 0; index < GetInputCount(); index++)
            {
                const Ptr<PassAttachment>& inputAtt = GetInputBinding(index).GetAttachment();
                if (inputAtt->m_descriptor.m_type == RHI::AttachmentType::Image && GetInputOutputCount())
                {
                    PassBufferAttachmentDesc desc;
                    std::string bufName = std::string("_CommTempBuffer_") + (char)(0x41 + index);
                    desc.m_name = Name(bufName.c_str());
                    desc.m_bufferDescriptor.m_byteCount = RHI::GetFormatSize(inputAtt->m_descriptor.m_image.m_format) *
                        inputAtt->m_descriptor.m_image.m_size.m_width * inputAtt->m_descriptor.m_image.m_size.m_height * inputAtt->m_descriptor.m_image.m_size.m_depth;
                    printf("CommPass::BuildInternal %s Input Image size is %d %d %d, format %d size %d buf size %d\n", GetName().GetCStr(),
                        (int)inputAtt->m_descriptor.m_image.m_size.m_width, (int)inputAtt->m_descriptor.m_image.m_size.m_height,
                        (int)inputAtt->m_descriptor.m_image.m_size.m_depth,(int)inputAtt->m_descriptor.m_image.m_format,
                        (int)RHI::GetFormatSize(inputAtt->m_descriptor.m_image.m_format), (int)desc.m_bufferDescriptor.m_byteCount);
                    desc.m_bufferDescriptor.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::DynamicInputAssembly;
                    Ptr<PassAttachment> tmpBuf = aznew PassAttachment(desc);
                    tmpBuf->ComputePathName(GetPathName());
                    tmpBuf->m_ownerPass = this;
                    m_ownedAttachments.push_back(tmpBuf);
                    GetInputOutputBinding(index).SetAttachment(tmpBuf);
                }
            }
#endif
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
                printf("CommPass::CompileResources %s no submit no need compile!\n", GetName().GetCStr());
                return;
            }
            for (uint32_t index = 0; index < GetInputCount(); index++)
            {
                RHI::CopyItemType copyType = GetCopyItemType(index);
                switch (copyType)
                {
                case AZ::RHI::CopyItemType::Buffer:
                    CopyBuffer(context, index);
                    break;
                case AZ::RHI::CopyItemType::Image:
                    CopyImage(context, index);
                    break;
                case AZ::RHI::CopyItemType::BufferToImage:
                    CopyBufferToImage(context, index);
                    break;
                case AZ::RHI::CopyItemType::ImageToBuffer:
                    CopyImageToBuffer(context, index);
                    break;
                default:
                    break;
                }
            }
        }

        void CommPass::BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context)
        {
            // pending data in
            if (m_data.m_commOper == CommOper::MergeOutput || m_data.m_commOper == CommOper::PrepareInput)
            {
                // test
                void *buf[8];
                uint32_t len[8];
                uint32_t count = 0;
                SplitInfo splitInfo;
                printf("CommPass %s enter recv data message \n", GetName().GetCStr());
                int ret = PassDistSystemInterface::Get()->RecvData(buf, len, 8, &count, splitInfo);
                printf("CommPass %s recv data message count %u ret %d index %u\n",
                    GetName().GetCStr(), count, ret, splitInfo.m_splitIdx);
                for (int i = 0; i < count && i < 8; i++)
                {
                    printf("CommPass recv data message %p len %u\n", buf[i], len[i]);
                    free(buf[i]);
                }
            }

            // build commnad
            if (m_data.m_submit)
            {
                for (auto& copyItem : m_copyItems)
                {
                    if (copyItem.m_type != RHI::CopyItemType::Invalid)
                    {
                        printf("CommPass::BuildCommandListInternal %s submit command thread %d\n", GetName().GetCStr(), (int)gettid());
                        context.GetCommandList()->Submit(copyItem);
                    }
                }
                m_copyItems.clear();
            }

            // send data to out
            if (m_data.m_commOper == CommOper::CopyInput || m_data.m_commOper == CommOper::CopyOutput)
            {
                // test
                void *data[1];
                uint32_t len[1];
                void *buf = (char *)malloc(1024);
                memset(buf, 0x5a, 1024);
                data[0] = buf;
                len[0] = 1024;
                SplitInfo splitInfo;
                splitInfo.m_splitCnt = m_data.m_splitInfo.m_splitCnt;
                splitInfo.m_splitIdx = m_data.m_splitInfo.m_splitIdx;
                int ret = PassDistSystemInterface::Get()->SendData(data, len, 1, splitInfo);
                printf("CommPass %s send data message ret %d index %u\n", GetName().GetCStr(), ret, splitInfo.m_splitIdx);
            }
        }

        // --- Copy setup functions ---

        void CommPass::CopyBuffer(const RHI::FrameGraphCompileContext& context, uint32_t index)
        {
            RHI::CopyBufferDescriptor copyDesc;

            // Source Buffer
            PassAttachmentBinding& copySource = GetInputBinding(index);
            const AZ::RHI::Buffer* sourceBuffer = context.GetBuffer(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceBuffer = sourceBuffer;
            copyDesc.m_size = static_cast<uint32_t>(sourceBuffer->GetDescriptor().m_byteCount);
            //copyDesc.m_sourceOffset = m_data.m_bufferSourceOffset;

            // Destination Buffer
            PassAttachmentBinding& copyDest = GetOutputBinding(index);
            copyDesc.m_destinationBuffer = context.GetBuffer(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOffset = m_data.m_bufferDestinationOffset;

            RHI::CopyItem copyItem = copyDesc;
            m_copyItems.emplace_back(copyItem);
        }

        void CommPass::CopyImage(const RHI::FrameGraphCompileContext& context, uint32_t index)
        {
            RHI::CopyImageDescriptor copyDesc;

            // Source Image
            PassAttachmentBinding& copySource = GetInputBinding(index);
            const AZ::RHI::Image* sourceImage = context.GetImage(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceImage = sourceImage;
            copyDesc.m_sourceSize = sourceImage->GetDescriptor().m_size;
            //copyDesc.m_sourceOrigin = m_data.m_imageSourceOrigin;
            //copyDesc.m_sourceSubresource = m_data.m_imageSourceSubresource;

            // Destination Image
            PassAttachmentBinding& copyDest = GetOutputBinding(index);
            copyDesc.m_destinationImage = context.GetImage(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOrigin = m_data.m_imageDestinationOrigin;
            //copyDesc.m_destinationSubresource = m_data.m_imageDestinationSubresource;
#if 0
            if (GetInputOutputCount())
            {
                PassAttachmentBinding& tmpBind = GetInputOutputBinding(index);
                const RHI::Buffer *tmpBuf = context.GetBuffer(tmpBind.GetAttachment()->GetAttachmentId());
                printf("CommPass BuildCommandListInternal tmp buf %p\n", tmpBuf);
            }
#endif
            printf("CommPass::CopyImage %s src %s to %s\n", GetName().GetCStr(),
                copySource.GetAttachment()->GetAttachmentId().GetCStr(),
                copyDest.GetAttachment()->GetAttachmentId().GetCStr());

            RHI::CopyItem copyItem = copyDesc;
            m_copyItems.emplace_back(copyItem);

        }

        void CommPass::CopyBufferToImage(const RHI::FrameGraphCompileContext& context, uint32_t index)
        {
            RHI::CopyBufferToImageDescriptor copyDesc;

            // Source Buffer
            PassAttachmentBinding& copySource = GetInputBinding(index);
            const AZ::RHI::Buffer* sourceBuffer = context.GetBuffer(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceBuffer = sourceBuffer;
            //copyDesc.m_sourceOffset = m_data.m_bufferSourceOffset;
            //copyDesc.m_sourceBytesPerRow = m_data.m_bufferSourceBytesPerRow;
            //copyDesc.m_sourceBytesPerImage = m_data.m_bufferSourceBytesPerImage;

            // Destination Image
            PassAttachmentBinding& copyDest = GetOutputBinding(index);
            copyDesc.m_destinationImage = context.GetImage(copyDest.GetAttachment()->GetAttachmentId());
            //copyDesc.m_destinationOrigin = m_data.m_imageDestinationOrigin;
            //copyDesc.m_destinationSubresource = m_data.m_imageDestinationSubresource;
            copyDesc.m_sourceSize = copyDesc.m_destinationImage->GetDescriptor().m_size;

            RHI::CopyItem copyItem = copyDesc;
            m_copyItems.emplace_back(copyItem);
        }

        void CommPass::CopyImageToBuffer(const RHI::FrameGraphCompileContext& context, uint32_t index)
        {
            RHI::CopyImageToBufferDescriptor copyDesc;

            // Source Image
            PassAttachmentBinding& copySource = GetInputBinding(index);
            const AZ::RHI::Image* sourceImage = context.GetImage(copySource.GetAttachment()->GetAttachmentId());
            copyDesc.m_sourceImage = sourceImage;
            copyDesc.m_sourceSize = sourceImage->GetDescriptor().m_size;
            //copyDesc.m_sourceOrigin = m_data.m_imageSourceOrigin;
            //copyDesc.m_sourceSubresource = m_data.m_imageSourceSubresource;

            // Destination Buffer
            PassAttachmentBinding& copyDest = GetOutputBinding(index);
            copyDesc.m_destinationBuffer = context.GetBuffer(copyDest.GetAttachment()->GetAttachmentId());
            printf("CopyImageToBuffer pass [%s] image format %d/%d buf atta id [%s] pointer %p\n",
                GetName().GetCStr(), (int)sourceImage->GetDescriptor().m_format,
                (int)RHI::GetFormatSize(sourceImage->GetDescriptor().m_format),
                copyDest.GetAttachment()->GetAttachmentId().GetCStr(), copyDesc.m_destinationBuffer);
            //print_stack();
            copyDesc.m_destinationOffset = 0;
            copyDesc.m_destinationBytesPerRow = sourceImage->GetDescriptor().m_size.m_width
                * RHI::GetFormatSize(sourceImage->GetDescriptor().m_format);
            copyDesc.m_destinationFormat = sourceImage->GetDescriptor().m_format;

            RHI::CopyItem copyItem = copyDesc;
            m_copyItems.emplace_back(copyItem);
        }

    }   // namespace RPI
}   // namespace AZ
