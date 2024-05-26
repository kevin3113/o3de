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

        struct PassDistNode {
            Ptr<Pass> m_self;
            Ptr<Pass> m_modify;
        };

        class PassDistSystemInterface
        {
            friend class Pass;
            friend class ParentPass;
            friend class PassTests;
        public:
            AZ_RTTI(PassDistSystemInterface, "{18067FB6-1B1D-11EF-9B62-194A7A597F6C}");

            PassDistSystemInterface() = default;
            virtual ~PassDistSystemInterface() = default;

            // Note that you have to delete these for safety reasons, you will trip a static_assert if you do not
            AZ_DISABLE_COPY_MOVE(PassDistSystemInterface);

            static PassDistSystemInterface* Get();

            virtual void ProcessDistChanges(Ptr<ParentPass> root) = 0;

            virtual void FrameEnd(void) = 0;
        };
                
    }   // namespace RPI
}   // namespace AZ
