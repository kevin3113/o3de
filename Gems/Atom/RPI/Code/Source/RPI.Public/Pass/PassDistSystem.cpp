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
#include <string.h>

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

        Ptr<Pass> CreatePass(Name name, Name modify)
        {
            PassAttachmentBinding inputBinding;
            inputBinding.m_name = "Input";
            inputBinding.m_slotType = PassSlotType::Input;

            PassAttachmentBinding outputBinding;
            outputBinding.m_name = "Output";
            outputBinding.m_slotType = PassSlotType::Output;

            PassBufferAttachmentDesc pbd;
            PassConnection conn;
            PassRequest req;
            req.m_passName = name;
            req.m_templateName = "DistTemplate";;
            conn.m_localSlot = "Input";
            conn.m_attachmentRef.m_pass = modify;
            conn.m_attachmentRef.m_attachment = "Output";
            req.AddInputConnection(conn);

            pbd.m_name = "rep_0_output";
            pbd.m_bufferDescriptor.m_byteCount = 1024;
            req.m_bufferAttachmentOverrides.emplace_back(pbd);
            
            conn.m_localSlot = "Output";
            conn.m_attachmentRef.m_pass = "This";
            conn.m_attachmentRef.m_attachment = "rep_0_output";
            req.AddInputConnection(conn);
            
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            add->AddAttachmentBinding(inputBinding);
            add->AddAttachmentBinding(outputBinding);
            
            return add;
        }

        bool NameEndWith(const Name name, const char *end)
        {
            char *subStr = strstr((char *)name.GetCStr(), end);
            if (!subStr) {
                return false;
            }
            size_t size = strnlen(end, 256);
            char *next;
            while (next = strstr(subStr + size, end)) {
                subStr = next;
            }
            if (subStr && 0 == strcmp(end, subStr)) {
                return true;
            }
            return false;
        }

        void PassDistSystem::ProcessDistChanges(Ptr<ParentPass> &root)
        {
            printf("PassDistSystem pipeline [%s] root pass [%s]\n",
                root->GetRenderPipeline()->GetId().GetCStr(),
                root->GetName().GetCStr());

            AZStd::vector<Ptr<Pass>> subPasses;
            Ptr<Pass> modifyPass = nullptr;
            
            for (auto& pass : root->GetChildren())
            {
                ProcessSubPasses(pass, subPasses);
            }
            for (auto& pass : subPasses)
            {
                if (NameEndWith(pass->GetName(), "PassB_0"))
                {
                    printf("current pass [%s] is PassB_0 matched\n", pass->GetName().GetCStr());
                    if (IsDistProcessed(pass->GetName())) {
                        printf("pass [%s] has been modified!\n", pass->GetName().GetCStr());
                        continue;
                    }
                    modifyPass = pass;
                    break;
                }
            }
            if (modifyPass)
            {
                std::string orig = modifyPass->GetName().GetCStr();
                orig += "_rep";
                Name newName = Name(orig.c_str());
                Ptr<Pass> newPass = CreatePass(newName, modifyPass->GetName());
                struct PassDistNode node = {newPass, modifyPass, nullptr};
                AddDistNode(node);
                /*
                PassAttachmentBinding bindingModify = PassAttachmentBinding->GetInputBinding(0);
                for (auto& pass : subPasses)
                {
                    for (PassAttachmentBinding& binding : pass->m_attachmentBindings) {
                        if ((uint32_t)PassSlotType::Output & (uint32_t)binding.m_slotType)
                        {
                            printf("Pass [%s] output [%s] attached id [%s] matched\n",
                                binding.m_name.GetCStr(),
                                binding.m_unifiedScopeDesc.m_attachmentId);
                            if (binding.m_unifiedScopeDesc.m_attachmentId == modifyOutput->Get)
                            {
                                
                            std::string orig = pass->GetName().GetCStr();
                            orig += "_rep";
                            Name newName = Name(orig.c_str());
                            Ptr<Pass> newPass = CreatePass(newName, pass->GetName());
                            struct PassDistNode node = {newPass, pass};
                            AddDistNode(node);
                            }
                        }
                    }
                }
                */
            }
            subPasses.clear();
            UpdateDistPasses();
        }

        bool PassDistSystem::IsDistProcessed(Name name)
        {
            auto itr = m_node_to_add.find(name);
            return itr != m_node_to_add.end();
        }

        void PassDistSystem::AddDistNode(struct PassDistNode &node)
        {
            printf("add new pass [%s] modify [%s]\n", node.m_self->GetName().GetCStr(),
                node.m_modify->GetName().GetCStr());
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
            for (auto &itr : m_node_to_add)
            {
                printf("build new pass [%s] modify [%s] built %d\n", itr.second.m_self->GetName().GetCStr(),
                    itr.second.m_modify->GetName().GetCStr(), (int)itr.second.built);
                if (!itr.second.built)
                {
                    itr.second.m_modify->GetParent()->AddChild(itr.second.m_self);
                    itr.second.m_self->Build(false);
                    itr.second.built = true;
                }
            }
        }

    }   // namespace RPI
}   // namespace AZ
