/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/Serialization/Utils.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Interface/Interface.h>

#include <AzCore/Serialization/Json/JsonUtils.h>

#include <Atom/RHI/FrameGraphBuilder.h>

#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>
#include <Atom/RPI.Public/Pass/PassDefines.h>
#include <Atom/RPI.Public/Pass/PassFactory.h>
#include <Atom/RPI.Public/Pass/PassLibrary.h>
#include <Atom/RPI.Public/Pass/PassDistSystem.h>
#include <Atom/RPI.Public/Pass/PassUtils.h>
#include <Atom/RPI.Public/Pass/Specific/SwapChainPass.h>
#include <Atom/RPI.Public/RenderPipeline.h>

#include <Atom/RPI.Reflect/Pass/ComputePassData.h>
#include <Atom/RPI.Reflect/Pass/CopyPassData.h>
#include <Atom/RPI.Reflect/Pass/DownsampleMipChainPassData.h>
#include <Atom/RPI.Reflect/Pass/FullscreenTrianglePassData.h>
#include <Atom/RPI.Reflect/Pass/EnvironmentCubeMapPassData.h>
#include <Atom/RPI.Reflect/Pass/RenderToTexturePassData.h>
#include <Atom/RPI.Reflect/Pass/PassAsset.h>
#include <Atom/RPI.Reflect/Pass/PassData.h>
#include <Atom/RPI.Reflect/Pass/PassTemplate.h>
#include <Atom/RPI.Reflect/Pass/RasterPassData.h>
#include <Atom/RPI.Reflect/Pass/RenderPassData.h>
#include <Atom/RPI.Reflect/Pass/SlowClearPassData.h>

#include <unistd.h>
#include <sys/syscall.h>
#include <signal.h>
#define gettid() syscall(SYS_gettid)

#include <execinfo.h>
namespace {
static void print_stack_pds(void)
{
    void *stack[32];
    char **msg;
    int sz = backtrace(stack, 32);
    msg = backtrace_symbols(stack, sz);
    printf("[bt] #0 thread %d\n", (int)gettid());
    for (int i = 1; i < sz; i++) {
        printf("[bt] #%d %s\n", i, msg[i]);
    }
}
}
#define print_stack print_stack_pds

namespace AZ
{
    namespace RPI
    {

        PassDistSystemInterface* PassDistSystemInterface::Get()
        {
            return Interface<PassDistSystemInterface>::Get();
        }

        void PassDistSystem::Reflect(AZ::ReflectContext* context)
        {
        }

        void PassDistSystem::Init()
        {
            Interface<PassDistSystemInterface>::Register(this);
        }

        void PassDistSystem::Shutdown()
        {
            Interface<PassDistSystemInterface>::Unregister(this);
        }

        void PassDistSystem::FrameEnd(void)
        {
        }

        void ProcessSubPasses(Ptr<Pass> pass, AZStd::vector<Ptr<Pass>>& subPasses)
        {
            ParentPass *parent = pass->AsParent();
            if (parent == nullptr) {
                subPasses.emplace_back(pass);
                //printf("PassDistSystem add leaf pass [%s]\n", pass->GetName().GetCStr());
            } else {
                //printf("PassDistSystem walk parent pass [%s]\n", pass->GetName().GetCStr());
                for (auto& subPass : parent->GetChildren()) {
                    ProcessSubPasses(subPass, subPasses);
                }
            }
        }

        void PassDistSystem::ProcessDistChanges(Ptr<ParentPass> root)
        {
            printf("PassDistSystem pipeline [%s] root pass [%s]\n",
                root->GetRenderPipeline()->GetId().GetCStr(),
                root->GetName().GetCStr());

            AZStd::vector<Ptr<Pass>> subPasses;
            
            for (auto& pass : root->GetChildren())
            {
                ProcessSubPasses(pass, subPasses);
            }
            for (auto& pass : subPasses)
            {
                if (strstr(pass->GetName().GetCStr(), "PassB_0")) {
                    printf("current pass [%s] is PassB_0\n", pass->GetName().GetCStr());
                    for (PassAttachmentBinding& binding : pass->m_attachmentBindings)
                    {
                        printf("pass [%s] slot type [%d] name [%s] connect [%s]\n", pass->GetName().GetCStr(),
                            (uint32_t)binding.m_slotType, binding.m_name.GetCStr(),
                            binding.m_unifiedScopeDesc.m_attachmentId.GetCStr());
                        if ((uint32_t)PassSlotType::Input & (uint32_t)binding.m_slotType)
                        {
                            printf("pass [%s] input [%s] connect [%s] match\n", pass->GetName().GetCStr(),
                                binding.m_name.GetCStr(), binding.m_unifiedScopeDesc.m_attachmentId.GetCStr());
                        }
                    }
                }
            }
            subPasses.clear();
        }

        bool PassDistSystem::IsDistProcessed(Name name)
        {
            auto itr = m_node_to_add.find(name);
            return itr != m_node_to_add.end();
        }

        void PassDistSystem::AddDistNode(struct PassDistNode &node)
        {
            auto itr = m_node_to_add.find(node.m_modify->GetName());
            if (itr != m_node_to_add.end()) {
                printf("Pass [%s] has been modified by [%s]!\n",
                    node.m_modify->GetName().GetCStr(),
                    node.m_self->GetName().GetCStr());
            }
            m_node_to_add.emplace(node.m_modify->GetName(), node);
        }

        void PassDistSystem::UpdateDistPasses(void)
        {
            for (auto itr : m_node_to_add)
            {
                itr.second.m_modify->GetParent()->AddChild(itr.second.m_self);
                itr.second.m_self->Build(false);
            }
        }

    }   // namespace RPI
}   // namespace AZ
