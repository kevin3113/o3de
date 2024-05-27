/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Atom/RHI.Reflect/Base.h>

#include <Atom/RPI.Public/Pass/ParentPass.h>
#include <Atom/RPI.Public/Pass/Pass.h>
#include <Atom/RPI.Public/Pass/PassTree.h>
#include <Atom/RPI.Public/Pass/PassLibrary.h>
#include <Atom/RPI.Public/Pass/PassFactory.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/Pass/PassDistSystemInterface.h>

#include <Atom/RPI.Reflect/Asset/AssetHandler.h>
#include <AzCore/std/containers/map.h>

#include <AzFramework/Windowing/WindowBus.h>

namespace AZ
{
    namespace RHI
    {
        class FrameGraphBuilder;
    }

    namespace RPI
    {
        //! The central class of the pass system.
        //! Responsible for preparing the frame and keeping 
        //! track of which passes need rebuilding or deleting.
        //! Holds the root of the pass hierarchy.
        class PassDistSystem final
            : public PassDistSystemInterface
        {
            friend class PassTests;
        public:
            AZ_RTTI(PassDistSystem, "{F92004C2-1B09-11EF-9B62-194A7A597F6C}", PassDistSystemInterface);
            AZ_CLASS_ALLOCATOR(PassDistSystem, AZ::SystemAllocator);

            static void Reflect(ReflectContext* context);

            PassDistSystem() {}

            AZ_DISABLE_COPY_MOVE(PassDistSystem);
            
            //! Initializes the PassSystem and the Root Pass and creates the Pass InstanceDatabase
            void Init();

            //! Deletes the Root Pass and shuts down the PassSystem
            void Shutdown();

            Ptr<Pass> CreateDistPass(Name name, Ptr<Pass> modify);

            void ProcessDistChanges(Ptr<ParentPass> &root) override;

            void FrameEnd(void) override;

            bool IsDistProcessed(Name name);
            
            void AddDistNode(struct PassDistNode &node);

            void UpdateDistPasses(void);

        private:
            // List of pass node to be add
            AZStd::unordered_map<Name, PassDistNode> m_node_to_add;

        };
    }   // namespace RPI
}   // namespace AZ
