/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/Name/Name.h>
#include <AzCore/RTTI/RTTI.h>
#include <AzCore/std/string/string_view.h>

#include <Atom/RPI.Public/Base.h>
#include <Atom/RPI.Public/Pass/PassDefines.h>

#include <Atom/RPI.Reflect/Base.h>
#include <Atom/RPI.Reflect/Pass/PassDescriptor.h>
#include <Atom/RPI.Reflect/System/AnyAsset.h>

#include <AzFramework/Windowing/WindowBus.h>

#include <Atom/RPI.Public/Pass/PassDistUtil.h>

namespace AZ
{
    namespace RPI
    {
        class Pass;
        class PassAsset;
        struct PassDescriptor;
        class PassFilter;
        class PassLibrary;
        struct PassRequest;
        class PassTemplate;
        class ParentPass;
        class SwapChainPass;
        struct RenderPipelineDescriptor;

        struct PassDistNode {
            Ptr<Pass> m_self;
            Ptr<Pass> m_modify;
            Ptr<Pass> m_after;
            AZStd::vector<Ptr<Pass>> m_follows;
            bool built = false;
        };

        class PassDistSystemInterface
        {
            friend class Pass;
            friend class ParentPass;
            friend class PassTests;
        public:
            AZ_RTTI(PassDistSystemInterface, "{18067FB6-1B1D-11EF-9B62-194A7A597F6C}");

            PassDistSystemInterface(void) = default;
            virtual ~PassDistSystemInterface(void) = default;

            // Note that you have to delete these for safety reasons, you will trip a static_assert if you do not
            AZ_DISABLE_COPY_MOVE(PassDistSystemInterface);

            static PassDistSystemInterface* Get(void);

            virtual int Connect(int sfd) = 0;

            virtual int Send(int sfd) = 0;

            virtual int SendQue(int sfd) = 0;

            virtual int Recv(int sfd) = 0;

            virtual void *DequePassMsg(bool noWait = false) = 0;

            virtual void EnquePassMsg(void *data) = 0;

            virtual void *DequeInputDataMsg(uint32_t queId, bool noWait = false) = 0;

            virtual void EnqueInputDataMsg(uint32_t queId, void *data) = 0;

            virtual void *DequeOutputDataMsg(uint32_t queId, bool noWait = false) = 0;

            virtual void EnqueOutputDataMsg(uint32_t queId, void *data) = 0;

            virtual int SendData(void *data[], uint32_t len[], uint32_t count, SplitInfo &splitInfo) = 0;

            virtual int RecvData(void *data[], uint32_t len[], uint32_t size, uint32_t *count, SplitInfo &splitInfo) = 0;

            virtual void ProcessDistChanges(Ptr<ParentPass> &root) = 0;

            virtual RenderPipelinePtr CreateDistPipeline(int device, const RenderPipelineDescriptor &desc) = 0;

            virtual RenderPipelinePtr GetDistPipeline(int device) = 0;

            virtual void SetActivePipeline(Name name) = 0;

            virtual Name GetActivePipeline(void) = 0;

            virtual void FrameEnd(void) = 0;

            virtual void Enable(void) = 0;

            virtual void Disable(void) = 0;

            virtual bool IsEnable(void) = 0;
        };
                
    }   // namespace RPI
}   // namespace AZ
