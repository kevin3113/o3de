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
#include <AzCore/Debug/CStackTrace.h>

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

        void ProcessSubPasses(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>>& subPasses)
        {
            ParentPass *parent = pass->AsParent();
            if (parent == nullptr) {
                subPasses.emplace(pass->GetName(), pass);
                //printf("PassDistSystem add leaf pass [%s]\n", pass->GetName().GetCStr());
            } else {
                //printf("PassDistSystem walk parent pass [%s]\n", pass->GetName().GetCStr());
                for (auto& subPass : parent->GetChildren()) {
                    ProcessSubPasses(subPass, subPasses);
                }
            }
        }

        Ptr<Pass> PassDistSystem::CreateDistPass(Name name, Ptr<Pass> &modify)
        {
            PassAttachmentBinding inputBinding;
            inputBinding.m_name = "Input";
            inputBinding.m_slotType = PassSlotType::Input;

            PassAttachmentBinding outputBinding;
            outputBinding.m_name = "Output";
            //2 outputBinding.m_slotType = PassSlotType::Output;
            outputBinding.m_slotType = PassSlotType::InputOutput;

            PassConnection conn;
            PassRequest req;
            req.m_passName = name;
            req.m_templateName = "DistTemplate";;
            //2 conn.m_localSlot = "Input";
            conn.m_localSlot = "Output";
            Name lastPass;
            Name lastSlot;
            for (auto &mconn : modify->m_request.m_connections)
            {
                if (mconn.m_attachmentRef.m_attachment == Name("Output"))
                {
                    printf("Replace pass [%s] input to pass [%s] output\n",
                        name.GetCStr(), mconn.m_attachmentRef.m_pass.GetCStr());
                    conn.m_attachmentRef.m_pass = mconn.m_attachmentRef.m_pass;
                    conn.m_attachmentRef.m_attachment = "Output";
                    req.AddInputConnection(conn);
                    lastPass = mconn.m_attachmentRef.m_pass;
                    lastSlot = "Output";
                    break;
                }
            }
            /* 1
            pbd.m_name = "rep_0_output";
            pbd.m_bufferDescriptor.m_byteCount = 1024;
            req.m_bufferAttachmentOverrides.emplace_back(pbd);
            
            conn.m_localSlot = "Output";
            conn.m_attachmentRef.m_pass = "This";
            conn.m_attachmentRef.m_attachment = "rep_0_output";
            */
            /*2
            if (modify->GetInputBinding(0).GetAttachment()->GetAttachmentType() == RHI::AttachmentType::Buffer)
            {
                PassBufferAttachmentDesc pbd;
                pbd.m_name = modify->GetInputBinding(0).GetAttachment()->GetAttachmentId();
                pbd.m_bufferDescriptor = modify->GetInputBinding(0).GetAttachment()->m_descriptor.m_buffer;
                req.m_bufferAttachmentOverrides.emplace_back(pbd);
            }
            else
            {
                PassImageAttachmentDesc pid;
                pid.m_name = modify->GetInputBinding(0).GetAttachment()->GetAttachmentId();
                pid.m_imageDescriptor = modify->GetInputBinding(0).GetAttachment()->m_descriptor.m_image;
                req.m_imageAttachmentOverrides.emplace_back(pid);
            }
            conn.m_localSlot = "Output";
            conn.m_attachmentRef.m_pass = "This";
            conn.m_attachmentRef.m_attachment = modify->GetInputBinding(0).GetAttachment()->GetAttachmentId();
            req.AddInputConnection(conn);
            */
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            //2 add->AddAttachmentBinding(inputBinding);
            add->AddAttachmentBinding(outputBinding);
            
            return add;
        }
#if 0
        Ptr<Pass> PassDistSystem::CreateFullscreanShadowPrePass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            std::string tempName = std::string("DistTemp_") + std::to_string(m_templates.size());
            passTemplate->m_name = tempName.c_str();
            passTemplate->m_passClass = "ComputePass";

            // Slots
            passTemplate->m_slots.resize(node->m_attachmentBindings.size());
            for (uint32_t i = 0; i < node->GetInputCount(); i++)
            {
                PassSlot& slot = passTemplate->m_slots[i];
                std::string slotName = std::string("InputOutput_") + std::to_string(i);
                slot.m_name = slotName.c_str();
                slot.m_slotType = PassSlotType::InputOutput;
                PassConnection conn;
                conn.m_localSlot = slot.m_name;
                conn.m_attachmentRef.m_pass = node->GetInputBinding(i);
                conn.m_attachmentRef.m_attachment = begin[i]->
                //slot.m_scopeAttachmentUsage = RHI::ScopeAttachmentUsage::Shader;
                //slot.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Clear;
            }
            for (uint32_t i = 0; i < node->GetOutputCount(); i++)
            {
                PassSlot& slot = passTemplate->m_slots[i + begin.size()];
                std::string slotName = std::string("Output_") + std::to_string(i);
                slot.m_name = slotName.c_str();
                slot.m_slotType = PassSlotType::Output;
                //slot.m_scopeAttachmentUsage = RHI::ScopeAttachmentUsage::Shader;
                //slot.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Clear;
            }
            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }
#endif

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowPrePass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassPreTemplate";
            passTemplate->m_passClass = "ComputePass";

            PassSlot slot;
            PassConnection conn;

            slot.m_name = "DirectionalShadowmaps";
            slot.m_slotType = PassSlotType::InputOutput;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = "Cascades";
            conn.m_attachmentRef.m_attachment = "Shadowmap";
            passTemplate->m_connections.emplace_back(conn);

            slot.m_name = "Depth";
            slot.m_slotType = PassSlotType::InputOutput;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_localSlot = slot.m_name;
            conn.m_attachmentRef.m_pass = "PipelineGlobal";
            conn.m_attachmentRef.m_attachment = "DepthMSAA";
            passTemplate->m_connections.emplace_back(conn);

            slot.m_name = "DepthLinear";
            slot.m_slotType = PassSlotType::InputOutput;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_localSlot = slot.m_name;
            conn.m_attachmentRef.m_pass = "PipelineGlobal";
            conn.m_attachmentRef.m_attachment = "DepthLinear";
            passTemplate->m_connections.emplace_back(conn);

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistPrePass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassDistPreTemplate";
            passTemplate->m_passClass = "ComputePass";

            PassSlot slot;
            PassConnection conn;

            for (uint32_t i = 0; i < node->GetInputCount(); i++)
            {
                PassAttachmentBinding binding = node->GetInputBinding(i);
                if (binding.m_name == Name("DirectionalShadowmaps")
                    || binding.m_name == Name("Depth")
                    || binding.m_name == Name("DepthLinear"))
                {
                    if (binding.GetAttachment()->GetAttachmentType() == RHI::AttachmentType::Buffer)
                    {
                        PassBufferAttachmentDesc pbd;
                        pbd.m_name = binding.GetAttachment()->m_name;
                        pbd.m_bufferDescriptor = binding.GetAttachment()->m_descriptor.m_buffer;
                        passTemplate->m_bufferAttachments.emplace_back(pbd);
                    }
                    else
                    {
                        PassImageAttachmentDesc pid;
                        pid.m_name = binding.GetAttachment()->m_name;
                        pid.m_imageDescriptor = binding.GetAttachment()->m_descriptor.m_image;
                        passTemplate->m_imageAttachments.emplace_back(pid);
                    }
                    slot.m_name = binding.m_name;
                    slot.m_slotType = PassSlotType::InputOutput;
                    conn.m_localSlot = slot.m_name;
                    passTemplate->m_slots.emplace_back(slot);
                    conn.m_attachmentRef.m_pass = "This";
                    conn.m_attachmentRef.m_attachment = binding.GetAttachment()->m_name;
                    passTemplate->m_connections.emplace_back(conn);
                }
            }

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowAfterPass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassAfterTemplate";
            passTemplate->m_passClass = "ComputePass";

            PassSlot slot;
            PassConnection conn;

            slot.m_name = "Output";
            slot.m_slotType = PassSlotType::InputOutput;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = "FullscreenShadowPass";
            conn.m_attachmentRef.m_attachment = "Output";
            passTemplate->m_connections.emplace_back(conn);

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistAfterPass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassDistAfterTemplate";
            passTemplate->m_passClass = "ComputePass";

            PassSlot slot;
            PassConnection conn;

            slot.m_name = "Output";
            slot.m_slotType = PassSlotType::InputOutput;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = node->GetName();
            conn.m_attachmentRef.m_attachment = "Output";
            passTemplate->m_connections.emplace_back(conn);

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistPass(Name name, Ptr<Pass> node)
        {
            Name prePass = node->GetName();
            PassConnection conn;
            PassRequest req;
            req.m_passName = name;
            req.m_templateName = "FullscreenShadowTemplate";

            conn.m_localSlot = "DirectionalShadowmaps";
            conn.m_attachmentRef.m_pass = prePass;
            conn.m_attachmentRef.m_attachment = "DirectionalShadowmaps";
            req.m_connections.emplace_back(conn);

            conn.m_localSlot = "Depth";
            conn.m_attachmentRef.m_pass = prePass;
            conn.m_attachmentRef.m_attachment = "Depth";
            req.m_connections.emplace_back(conn);

            conn.m_localSlot = "DepthLinear";
            conn.m_attachmentRef.m_pass = node->GetName();
            conn.m_attachmentRef.m_attachment = "DepthLinear";
            req.m_connections.emplace_back(conn);
            
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            return add;
        }

        const char *GetSlotIntfName(PassSlot slot)
        {
            return slot.m_name.GetCStr();
        }

        const char *GetSlotTypeName(PassSlotType slotType )
        {
            if (slotType == PassSlotType::Input)
            {
                return "Input";
            }
            else if (slotType == PassSlotType::Output)
            {
                return "Output";
            }
            else if (slotType == PassSlotType::InputOutput)
            {
                return "InputOutput";
            }
            else
            {
                return "Unknow";
            }
        }

        void PassDistSystem::ShowConnections(Ptr<Pass> &pass)
        {
            if (pass->m_template != nullptr)
            {
                printf("Pass [%s] template is [%s] pass class [%s]\n",
                    pass->GetName().GetCStr(), pass->m_template->m_name.GetCStr(),
                    pass->m_template->m_passClass.GetCStr());
                for (auto &slot : pass->m_template->m_slots)
                {
                    printf("Pass from template [%s] slot [%s] type [%s]\n",
                        pass->GetName().GetCStr(), GetSlotIntfName(slot),
                        GetSlotTypeName(slot.m_slotType));
                }
                for (auto &conn : pass->m_template->m_connections)
                {
                    printf("Pass from template [%s] slot [%s] connect to pass [%s] slot [%s]\n",
                        pass->GetName().GetCStr(), conn.m_localSlot.GetCStr(),
                        conn.m_attachmentRef.m_pass.GetCStr(),
                        conn.m_attachmentRef.m_attachment.GetCStr());
                }
            }
            printf("Pass from request template name [%s]\n", pass->m_request.m_templateName.GetCStr());
            for (auto &conn : pass->m_request.m_connections)
            {
                printf("Pass from request [%s] slot [%s] connect to pass [%s] slot [%s]\n",
                    pass->GetName().GetCStr(), conn.m_localSlot.GetCStr(),
                    conn.m_attachmentRef.m_pass.GetCStr(),
                    conn.m_attachmentRef.m_attachment.GetCStr());
            }
            for (PassAttachmentBinding binding : pass->m_attachmentBindings)
            {
                printf("Pass runtime [%s] slot [%s] type [%s] attached id [%s] name [%s] is pass [%s] amatched\n",
                    pass->GetName().GetCStr(), binding.m_name.GetCStr(),
                    GetSlotTypeName(binding.m_slotType),
                    //binding.m_unifiedScopeDesc.m_attachmentId.GetCStr());
                    binding.GetAttachment()->GetAttachmentId().GetCStr(),
                    binding.GetAttachment()->m_name.GetCStr(),
                    binding.GetAttachment()->m_ownerPass ? binding.GetAttachment()->m_ownerPass->GetName().GetCStr() : "nullptr"
                    );
            }
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

        void PassDistSystem::ProcessPassB(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses)
        {
            ShowConnections(pass);
            std::string orig = pass->GetName().GetCStr();
            orig += "_rep";
            Name newName = Name(orig.c_str());
            Ptr<Pass> newPass = CreateDistPass(newName, pass);
            AZStd::vector<Ptr<Pass>> follows;

            PassAttachmentBinding &modifyOutput = pass->GetOutputBinding(0);
            printf("Pass [%s] output [%s] attached id [%s]\n",
                pass->GetName().GetCStr(),
                modifyOutput.m_name.GetCStr(),
                modifyOutput.m_unifiedScopeDesc.m_attachmentId.GetCStr());
            for (auto& itr : subPasses)
            {
                auto curPass = itr.second;
                PassAttachmentBinding &curInput = curPass->GetInputBinding(0);
                if (curInput.m_unifiedScopeDesc.m_attachmentId == modifyOutput.m_unifiedScopeDesc.m_attachmentId)
                {
                    printf("Pass [%s] input [%s] attached id [%s] matched\n",
                        curPass->GetName().GetCStr(),
                        curInput.m_name.GetCStr(),
                        curInput.m_unifiedScopeDesc.m_attachmentId.GetCStr());
                    bool match = false;
                    for (auto &conn : curPass->m_request.m_connections)
                    {
                        if (conn.m_attachmentRef.m_pass == pass->GetName())
                        {
                            conn.m_attachmentRef.m_pass = newPass->GetName();
                            conn.m_attachmentRef.m_attachment = "Output";
                            match = true;
                        }
                    }
                    if (match)
                    {
                        follows.emplace_back(curPass);
                    }
                    for (auto &conn : curPass->m_request.m_connections)
                    {
                        printf("After modify pass [%s] slot [%s] connect name [%s] slot [%s]\n",
                            curPass->GetName().GetCStr(), conn.m_localSlot.GetCStr(),
                            conn.m_attachmentRef.m_pass.GetCStr(),
                            conn.m_attachmentRef.m_attachment.GetCStr());
                    }
                }
            }
            pass->m_request.m_connections.clear();
            struct PassDistNode node = {newPass, pass, Ptr<Pass>(nullptr), follows};
            AddDistNode(node);
        }

        void PassDistSystem::ProcessFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses)
        {
            ShowConnections(pass);
            std::string orig = pass->GetName().GetCStr();
            std::string  preName = orig + "_Pre";
            std::string  aterName = orig + "_After";
            Name newName = Name(preName.c_str());
            Ptr<Pass> prePass = CreateFullscreenShadowPrePass(newName, pass);
            newName = Name(aterName.c_str());
            Ptr<Pass> afterPass = CreateFullscreenShadowAfterPass(newName, pass);
            AZStd::vector<Ptr<Pass>> follows;

            pass->m_request.m_connections.clear();
            //pass->m_template->m_connections.clear();
            for (auto &conn : prePass->m_template->m_connections)
            {
                PassConnection mconn;
                mconn.m_localSlot = conn.m_localSlot;
                mconn.m_attachmentRef.m_pass = newName;
                mconn.m_attachmentRef.m_attachment = conn.m_localSlot;
                pass->m_request.m_connections.emplace_back(mconn);
            }

            for (auto &conn : pass->m_request.m_connections)
            {
                printf("After modify pass [%s] slot [%s] connect name [%s] slot [%s]\n",
                    pass->GetName().GetCStr(), conn.m_localSlot.GetCStr(),
                    conn.m_attachmentRef.m_pass.GetCStr(),
                    conn.m_attachmentRef.m_attachment.GetCStr());
            }
            struct PassDistNode node = {prePass, pass, afterPass, follows};
            AddDistNode(node);
            CloneFullscreenShadow(pass);
        }

        void PassDistSystem::CloneFullscreenShadow(Ptr<Pass> pass)
        {
            RenderPipelinePtr pipeline = GetDistPipeline(1);
            if (pipeline == nullptr)
            {
                printf("CloneFullscreenShadow pipeline nullptr\n");
                return;
            }
            const Ptr<ParentPass>& root = pipeline->GetRootPass();
            if (root->GetChildren().size() > 1)
            {
                printf("CloneFullscreenShadow child pass not 0\n");
                return;
            }
            std::string orig = pass->GetName().GetCStr();
            std::string  preName = orig + "_DistPre";
            std::string  distName = orig + "_Dist";
            std::string  afterName = orig + "_DistAfter";
            Name newName = Name(preName.c_str());
            Ptr<Pass> prePass = CreateFullscreenShadowDistPrePass(Name(preName.c_str()), pass);
            Ptr<Pass> distPass = CreateFullscreenShadowDistPass(Name(distName.c_str()), prePass);
            Ptr<Pass> afterPass = CreateFullscreenShadowDistAfterPass(Name(afterName.c_str()), distPass);
            root->AddChild(prePass);
            root->AddChild(distPass);
            root->AddChild(afterPass);
        }

        void PassDistSystem::ProcessDistChanges(Ptr<ParentPass> &root)
        {
            if (!m_state)
            {
                printf("PassDistSystem is disabled!\n");
                return;
            }
            printf("PassDistSystem pipeline [%s] root pass [%s]\n",
                root->GetRenderPipeline()->GetId().GetCStr(),
                root->GetName().GetCStr());

            AZStd::unordered_map<Name, Ptr<Pass>> subPasses;
            AZStd::vector<std::function<void()>> procs;
            
            for (auto& pass : root->GetChildren())
            {
                ProcessSubPasses(pass, subPasses);
            }
            for (auto& itr : subPasses)
            {
                auto pass = itr.second;
                if (NameEndWith(pass->GetName(), "PassB_0"))
                {
                    printf("current pass [%s] is matched\n", pass->GetName().GetCStr());
                    if (IsDistProcessed(pass->GetName())) {
                        printf("pass [%s] has been modified!\n", pass->GetName().GetCStr());
                        continue;
                    }
                    procs.emplace_back([pass, subPasses, this]() {
                        this->ProcessPassB(pass, subPasses);
                    });
                }
                else if (NameEndWith(pass->GetName(), "FullscreenShadowPass"))
                {
                    printf("current pass [%s] is matched\n", pass->GetName().GetCStr());
                    if (IsDistProcessed(pass->GetName())) {
                        printf("pass [%s] has been modified!\n", pass->GetName().GetCStr());
                        continue;
                    }
                    procs.emplace_back([pass, subPasses, this]() {
                        this->ProcessFullscreenShadow(pass, subPasses);
                    });
                }
            }
            for (auto call : procs)
            {
                call();
            }
            subPasses.clear();
            UpdateDistPasses();
        }

        bool PassDistSystem::IsDistProcessed(Name name)
        {
            auto itr = m_distChangeList.find(name);
            return itr != m_distChangeList.end();
        }

        void PassDistSystem::AddDistNode(struct PassDistNode &node)
        {
            printf("add new pass [%s] modify [%s]\n", node.m_self->GetName().GetCStr(),
                node.m_modify->GetName().GetCStr());
            auto itr = m_distChangeList.find(node.m_modify->GetName());
            if (itr != m_distChangeList.end()) {
                printf("Pass [%s] has been modified by [%s]!\n",
                    node.m_modify->GetName().GetCStr(),
                    node.m_self->GetName().GetCStr());
            }
            m_distChangeList.emplace(node.m_modify->GetName(), node);
        }

        void PassDistSystem::UpdateDistPasses(void)
        {
            if (!m_state)
            {
                printf("PassDistSystem is disabled!\n");
                return;
            }
            for (auto &itr : m_distChangeList)
            {
                printf("build new pass [%s] modify [%s] built %d\n", itr.second.m_self->GetName().GetCStr(),
                    itr.second.m_modify->GetName().GetCStr(), (int)itr.second.built);
                if (!itr.second.built)
                {
                    printf("Add Pass [%s] to parent pass [%s]\n",
                        itr.second.m_self->GetName().GetCStr(),
                        itr.second.m_modify->GetParent()->GetName().GetCStr());
                    //itr.second.m_modify->GetParent()->AddChild(itr.second.m_self);
                    itr.second.m_modify->GetParent()->InsertChild(itr.second.m_self, itr.second.m_modify->m_parentChildIndex);
                    if (itr.second.m_after)
                    {
                        itr.second.m_modify->GetParent()->InsertChild(itr.second.m_after, itr.second.m_modify->m_parentChildIndex + 1);
                    }
                    itr.second.m_modify->Build(false);
                    itr.second.m_self->Build(false);
                    if (itr.second.m_after)
                    {
                        itr.second.m_after->Build(false);
                    }
                    for (auto &pass : itr.second.m_follows)
                    {
                        pass->Build(false);
                    }
                    itr.second.built = true;
                }
            }
        }

        RenderPipelinePtr PassDistSystem::CreateDistPipeline(int device, AZStd::string name)
        {
            const RenderPipelineDescriptor desc { .m_name = name };
            RenderPipelinePtr pipeline = RenderPipeline::CreateRenderPipeline(desc);
            m_devPipelines.emplace(device, pipeline);
            printf("PassDistSystem add pipeline [%s] on device %d\n",
                name.c_str(), device);
            return pipeline;
        }

        RenderPipelinePtr PassDistSystem::GetDistPipeline(int device)
        {
            auto itr = m_devPipelines.find(device);
            if (itr != m_devPipelines.end())
            {
                return itr->second;
            }
            return nullptr;
        }

        void PassDistSystem::SetCurDevice(int deviceId)
        {
            printf("PassDistSystem set current schedule device %d\n", deviceId);
            m_curDevice = deviceId;
        }

        int PassDistSystem::GetCurDevice(void)
        {
            printf("PassDistSystem get current schedule device %d\n", m_curDevice);
            return m_curDevice;
        }

        void PassDistSystem::Enable(void)
        {
            printf("PassDistSystem set enabled!\n");
            m_state = true;
        }

        void PassDistSystem::Disable(void)
        {
            printf("PassDistSystem set disabled!\n");
            m_state = false;
        }

        bool PassDistSystem::IsEnable(void)
        {
            return m_state;
        }

    }   // namespace RPI
}   // namespace AZ
