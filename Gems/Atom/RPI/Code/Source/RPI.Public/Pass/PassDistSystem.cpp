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
#include <Atom/RPI.Public/Pass/PassDistUtil.h>
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
#include <Atom/RPI.Public/RPISystem.h>
#include <AzCore/Debug/CStackTrace.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace AZ
{
    namespace RPI
    {
        static thread_local bool isActive = false;

        PassDistSystemInterface* PassDistSystemInterface::Get()
        {
            return Interface<PassDistSystemInterface>::Get();
        }

        bool PassDistSystem::IsActive()
        {
            if (m_isServer)
            {
                return true;
            }
            return isActive;
        }

        void PassDistSystem::Active()
        {
            isActive = true;
        }

        void PassDistSystem::Inactive()
        {
            isActive = false;
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

        void DumpMsg(const char *name, char *buf, uint32_t len)
        {
            uint32_t cur;
            FILE *fp = nullptr;
            if (name)
            {
                fp = fopen(name, "w+"); 
            }
            printf("Dump message length %u:\n", len);
            for (cur = 0; cur < len; cur += 4)
            {
                if ((cur % 32) == 0)
                {
                    printf("%p:", (void *)(buf + cur));
                    if (fp)
                        fprintf(fp, "%p:", (void *)(buf + cur));
                }
                printf(" %08x", *(unsigned int *)(&buf[cur]));
                if (fp)
                    fprintf(fp, " %08x", *(unsigned int *)(&buf[cur]));
                if ((cur % 32) == 28)
                {
                    printf("\n");
                    if (fp)
                        fprintf(fp, "\n");
                }
            }
            printf("\n");
            if (fp)
            {
                fprintf(fp, "\n");
                fclose(fp);
            }
        }

        void CreateThread(void *(*func)(void *), void *arg)
        {
            pthread_attr_t attr;
            int ret;
            ret = pthread_attr_init(&attr);
            if (ret != 0)
            {
                printf("pthread attr init ret %d\n", ret);
                return;
            }
            pthread_t daemonThr;
            ret = pthread_create(&daemonThr, &attr, func, arg);
            if (ret != 0)
            {
                printf("pthread create ret %d\n", ret);
            }
            (void)pthread_attr_destroy(&attr);
            printf("thread 0x%lx create ok!\n", daemonThr);
        }
        
        void *SeverThread(void *arg)
        {
            #define BUF_SIZE 256
            int cfd = *(int *)arg;
            ssize_t numRead;
            char buf[BUF_SIZE];

            for (;;)
            {
                void *msg = PassDistSystemInterface::Get()->DequePassMsg();
                MsgHead *msgHead = (MsgHead *)msg;
                printf("Deque pass message len %u\n", msgHead->msgLen);
                DumpMsg("send.txt", (char *)msg, msgHead->msgLen);
                //const char *msg = "Send pass to clients!\n";
                if (write(cfd, msg, msgHead->msgLen) <= 0)
                {
                    printf("send to client error!\n");
                    break;
                } 
                if (read(cfd, buf, BUF_SIZE) <= 0)
                {
                    printf("recv from client error!\n");
                    break;
                }
                printf("server recv: %s\n", buf);
            }
            close(cfd);
            printf("socket client disconnected!\n");
            *(int *)arg = 0;
            return nullptr;
        }

        void *ServerDaemon(void *arg)
        {
            int sfd = *(int *)arg;
            int fdPool[16] = {0};
            for (;;)
            {
                printf("Waiting to accept a connection...\n");
                int cfd = accept(sfd, NULL, NULL);
                printf("Accepted socket fd = %d\n", cfd);
                if (cfd >= 0)
                {
                    uint32_t i = 0;
                    for (;;)
                    {
                        if (fdPool[i%16] == 0)
                            break;
                        i++;
                    }
                    fdPool[i%16] = cfd;
                    CreateThread(&SeverThread, &fdPool[i%16]);
                }
            }
            return nullptr;
        }

        void *ClientDaemon(void *arg)
        {
            for (;;)
            {
                if (!PassDistSystemInterface::Get()->Recv())
                {
                    PassDistSystemInterface::Get()->Active();
                    //RPISystemInterface::Get()->RenderTick();
                    PassDistSystemInterface::Get()->Send();
                    PassDistSystemInterface::Get()->Inactive();
                    continue;
                }
                sleep(1);
                PassDistSystemInterface::Get()->Connect();
            }
            return nullptr;
        }

        void PassDistSystem::CommInit(bool isServer, const char *path)
        {
            struct sockaddr_un addr;

            m_sfd = socket(AF_UNIX, SOCK_STREAM, 0); 
            printf("Server socket fd = %d\n", m_sfd);

            if (m_sfd == -1) {
                printf("socket error!\n");
                return;
            }

            if (isServer && remove(path) == -1 && errno != ENOENT) {
                printf("remove-%s error!\n", path);
                return;
            }

            m_commPath = Name(path);

            if (isServer)
            {
                memset(&addr, 0, sizeof(struct sockaddr_un));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
                if (bind(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
                    printf("bind error\n");
                    return;
                }
                if (listen(m_sfd, 16) == -1) {
                    printf("listen error!\n");
                    return;
                }
                CreateThread(&ServerDaemon, (void *)&m_sfd);
                printf("Dist Daemon thread create ok!\n");
            }
            else
            {
                CreateThread(&ClientDaemon, (void *)&m_sfd);
                printf("Dist Daemon thread create ok!\n");
            }
        }

        int PassDistSystem::Connect(void)
        {
            struct sockaddr_un addr;

            memset(&addr, 0, sizeof(struct sockaddr_un));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_commPath.GetCStr(), sizeof(addr.sun_path) - 1);

            if (connect(m_sfd, (struct sockaddr *) &addr,
                        sizeof(struct sockaddr_un)) == -1) {
                printf("connect error!\n");
                return -1;
            }
            return 0;
        }

        int PassDistSystem::Send(void)
        {
            char buf[256] = {"client send msg ...............\n"};
            if (write(m_sfd, buf, sizeof(buf)) != sizeof(buf)) {
                printf("partial/failed write error!\n");
                return -1;
            }
            printf("send: %s\n", buf);
            return 0;
        }

        int PassDistSystem::Recv(void)
        {
            char buf[10240];

            int len = read(m_sfd, buf, sizeof(MsgHead));
            if (len != sizeof(MsgHead))
            {
                printf("recv error len %d\n", len);
                return -1;
            }
            MsgHead *msgHead = (MsgHead *)buf;
            printf("recv pass message task id %u len %u\n", msgHead->taskId, msgHead->msgLen);
            do
            {
                len += read(m_sfd, buf + len, sizeof(buf) - len);
            } while (len < msgHead->msgLen);

            DumpMsg("recv.txt", buf, msgHead->msgLen);

            ParsePassCreateMsg((char *)msgHead + sizeof(MsgHead), msgHead->msgLen - sizeof(MsgHead));
            return 0;
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

        void *PassDistSystem::DequePassMsg(void)
        {
            return m_msgQue.P();
        }

        void PassDistSystem::EnquePassMsg(void *data)
        {
            m_msgQue.V(data);
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

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistPass(Name name, Ptr<Pass>prePass, Ptr<Pass> node)
        {
            Name prePassName = prePass->GetName();
            PassConnection conn;
            PassRequest req;
            req.m_passName = name;
            req.m_templateName = "FullscreenShadowTemplate";

            conn.m_localSlot = "DirectionalShadowmaps";
            conn.m_attachmentRef.m_pass = prePassName;
            conn.m_attachmentRef.m_attachment = "DirectionalShadowmaps";
            req.m_connections.emplace_back(conn);

            conn.m_localSlot = "Depth";
            conn.m_attachmentRef.m_pass = prePassName;
            conn.m_attachmentRef.m_attachment = "Depth";
            req.m_connections.emplace_back(conn);

            conn.m_localSlot = "DepthLinear";
            conn.m_attachmentRef.m_pass = prePassName;
            conn.m_attachmentRef.m_attachment = "DepthLinear";
            req.m_connections.emplace_back(conn);

            PassImageAttachmentDesc outDesc;
            outDesc.m_name = node->GetOutputBinding(0).GetAttachment()->m_name;
            outDesc.m_imageDescriptor = node->GetOutputBinding(0).GetAttachment()->m_descriptor.m_image;

            if (node->GetOutputBinding(0).GetAttachment()->m_sizeSource)
            {
                auto& refAttachment = node->GetOutputBinding(0).GetAttachment()->m_sizeSource->GetAttachment();
                printf("m_sizeSource atta id [%s] size from ref %p\n", node->GetOutputBinding(0).GetAttachment()->GetAttachmentId().GetCStr(),
                    refAttachment.get());
                if (refAttachment && refAttachment->m_descriptor.m_type == RHI::AttachmentType::Image)
                {
                    printf("m_sizeSource ref to [%s] size 0x %x_%x_%x\n", refAttachment->GetAttachmentId().GetCStr(),
                        refAttachment->m_descriptor.m_image.m_size.m_width,
                        refAttachment->m_descriptor.m_image.m_size.m_height,
                        refAttachment->m_descriptor.m_image.m_size.m_depth);
                    outDesc.m_imageDescriptor = refAttachment->m_descriptor.m_image;
                }
            }

            req.m_imageAttachmentOverrides.emplace_back(outDesc);

            printf("create pass [%s] m_imageAttachmentOverrides m_name [%s]\n",
                name.GetCStr(), node->GetOutputBinding(0).GetAttachment()->m_name.GetCStr());
            
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            return add;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistPrePassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node)
        {
            char *pos = buf;
            printf("CreateFullscreenShadowDistPrePassMsg tlv start %p\n", (void *)pos);
            MsgTlvInfo *passTlv = (MsgTlvInfo *)pos;
            passTlv->type = (uint32_t)DistTlvType::Pass;
            pos += sizeof(MsgTlvInfo);
            MsgPass *passHead = (MsgPass *)pos;
            pos += sizeof(MsgPass);
            printf("CreateFullscreenShadowDistPrePassMsg pass msg  start %p\n", (void *)passHead);
            passHead->createType = (uint32_t)PassCreateType::Template;
            strncpy(passHead->name, name.GetCStr(), sizeof(passHead->name));
            strncpy(passHead->passTemp, "FullscreenShadowPassDistPreTemplate", sizeof(passHead->passTemp));
            strncpy(passHead->passClass, "ComputePass", sizeof(passHead->passClass));

            MsgPassSlot *slot = (MsgPassSlot *)pos;
            passHead->slotCnt = 3;
            pos += sizeof(MsgPassSlot) * passHead->slotCnt;

            MsgPassConn *conn = (MsgPassConn *)pos;
            passHead->connCnt = 3;
            pos += sizeof(MsgPassConn) * passHead->connCnt;

            MsgPassAttImg *img = (MsgPassAttImg *)pos;
            passHead->imgCnt = 3;
            pos += sizeof(MsgPassAttImg) * passHead->imgCnt;
            passHead->bufCnt = 0;

            for (uint32_t i = 0; i < node->GetInputCount(); i++)
            {
                PassAttachmentBinding binding = node->GetInputBinding(i);
                if (binding.m_name == Name("DirectionalShadowmaps")
                    || binding.m_name == Name("Depth")
                    || binding.m_name == Name("DepthLinear"))
                {
                    printf("CreateFullscreenShadowDistPrePassMsg process binding [%s]\n", binding.m_name.GetCStr());
                    slot->slotType = (uint32_t)PassSlotType::InputOutput;
                    strncpy(slot->slotName, binding.m_name.GetCStr(), sizeof(slot->slotName));
                    printf("slot %p name [%s]\n", (void *)slot, slot->slotName);
                    slot++;

                    strncpy(conn->localSlot, binding.m_name.GetCStr(), sizeof(conn->localSlot));
                    strncpy(conn->refPassName, "This", sizeof(conn->refPassName));
                    strncpy(conn->refAttName, binding.GetAttachment()->m_name.GetCStr(), sizeof(conn->refAttName));
                    printf("conn %p local slot name [%s]\n", (void *)conn, conn->localSlot);
                    conn++;

                    strncpy(img->name, binding.GetAttachment()->m_name.GetCStr(), sizeof(img->name));
                    RHI::ImageDescriptor imgDesc = binding.GetAttachment()->m_descriptor.m_image;
                    img->bindFlags = (uint32_t)imgDesc.m_bindFlags;
                    img->width = (uint32_t)imgDesc.m_size.m_width;
                    img->height = (uint32_t)imgDesc.m_size.m_height;
                    img->depth = (uint32_t)imgDesc.m_size.m_depth;
                    img->arraySize = (uint32_t)imgDesc.m_arraySize;
                    img->format = (uint32_t)imgDesc.m_format;
                    printf("img %p name [%s]\n", (void *)img, img->name);
                    img++;
                }
            }

            passHead->CalcBodyLen();
            passTlv->len = passHead->bodyLen + sizeof(MsgPass);
            uint32_t totalLen = (uint32_t)(pos - buf);
            return totalLen;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistPassMsg(char *buf, uint32_t len, Name name, Ptr<Pass>prePass, Ptr<Pass> node)
        {
            char *pos = buf;
            Name prePassName = prePass->GetName();

            MsgTlvInfo *passTlv = (MsgTlvInfo *)pos;
            passTlv->type = (uint32_t)DistTlvType::Pass;
            pos += sizeof(MsgTlvInfo);
            MsgPass *passHead = (MsgPass *)pos;
            pos += sizeof(MsgPass);
            passHead->createType = (uint32_t)PassCreateType::Request;
            strncpy(passHead->name, name.GetCStr(), sizeof(passHead->name));
            strncpy(passHead->passTemp, "FullscreenShadowTemplate", sizeof(passHead->passTemp));
            strncpy(passHead->passClass, "", sizeof(passHead->passClass));

            passHead->slotCnt = 0;
            MsgPassConn *conn = (MsgPassConn *)pos;
            passHead->connCnt = 3;
            pos += sizeof(MsgPassConn) * passHead->connCnt;
            strncpy(conn->localSlot, "DirectionalShadowmaps", sizeof(conn->localSlot));
            strncpy(conn->refPassName, prePassName.GetCStr(), sizeof(conn->refPassName));
            strncpy(conn->refAttName, "DirectionalShadowmaps", sizeof(conn->refAttName));
            conn++;
            strncpy(conn->localSlot, "Depth", sizeof(conn->localSlot));
            strncpy(conn->refPassName, prePassName.GetCStr(), sizeof(conn->refPassName));
            strncpy(conn->refAttName, "Depth", sizeof(conn->refAttName));
            conn++;
            strncpy(conn->localSlot, "DepthLinear", sizeof(conn->localSlot));
            strncpy(conn->refPassName, prePassName.GetCStr(), sizeof(conn->refPassName));
            strncpy(conn->refAttName, "DepthLinear", sizeof(conn->refAttName));
            
            MsgPassAttImg *img = (MsgPassAttImg *)pos;
            passHead->imgCnt = 1;
            pos += sizeof(MsgPassAttImg) * passHead->imgCnt;
            strncpy(img->name, node->GetOutputBinding(0).GetAttachment()->m_name.GetCStr(), sizeof(img->name));
            RHI::ImageDescriptor imgDesc = node->GetOutputBinding(0).GetAttachment()->m_descriptor.m_image;
            img->bindFlags = (uint32_t)imgDesc.m_bindFlags;
            img->width = (uint32_t)imgDesc.m_size.m_width;
            img->height = (uint32_t)imgDesc.m_size.m_height;
            img->depth = (uint32_t)imgDesc.m_size.m_depth;
            img->arraySize = (uint32_t)imgDesc.m_arraySize;
            img->format = (uint32_t)imgDesc.m_format;

            if (node->GetOutputBinding(0).GetAttachment()->m_sizeSource)
            {
                auto& refAttachment = node->GetOutputBinding(0).GetAttachment()->m_sizeSource->GetAttachment();
                printf("m_sizeSource atta id [%s] size from ref %p\n", node->GetOutputBinding(0).GetAttachment()->GetAttachmentId().GetCStr(),
                    refAttachment.get());
                if (refAttachment && refAttachment->m_descriptor.m_type == RHI::AttachmentType::Image)
                {
                    printf("m_sizeSource ref to [%s] size 0x %x_%x_%x\n", refAttachment->GetAttachmentId().GetCStr(),
                        refAttachment->m_descriptor.m_image.m_size.m_width,
                        refAttachment->m_descriptor.m_image.m_size.m_height,
                        refAttachment->m_descriptor.m_image.m_size.m_depth);
                    imgDesc = refAttachment->m_descriptor.m_image;
                    img->bindFlags = (uint32_t)imgDesc.m_bindFlags;
                    img->width = (uint32_t)imgDesc.m_size.m_width;
                    img->height = (uint32_t)imgDesc.m_size.m_height;
                    img->depth = (uint32_t)imgDesc.m_size.m_depth;
                    img->arraySize = (uint32_t)imgDesc.m_arraySize;
                    img->format = (uint32_t)imgDesc.m_format;
                }
            }

            passHead->bufCnt = 0;
            passHead->CalcBodyLen();
            passTlv->len = passHead->bodyLen + sizeof(MsgPass);
            uint32_t totalLen = (uint32_t)(pos - buf);

            return totalLen;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistAfterPassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node)
        {
            char *pos = buf;

            MsgTlvInfo *passTlv = (MsgTlvInfo *)pos;
            passTlv->type = (uint32_t)DistTlvType::Pass;
            pos += sizeof(MsgTlvInfo);
            MsgPass *passHead = (MsgPass *)pos;
            pos += sizeof(MsgPass);
            passHead->createType = (uint32_t)PassCreateType::Template;
            strncpy(passHead->name, name.GetCStr(), sizeof(passHead->name));
            strncpy(passHead->passTemp, "FullscreenShadowPassDistAfterTemplate", sizeof(passHead->passTemp));
            strncpy(passHead->passClass, "ComputePass", sizeof(passHead->passClass));

            MsgPassSlot *slot = (MsgPassSlot *)pos;
            passHead->slotCnt = 1;
            pos += sizeof(MsgPassSlot) * passHead->slotCnt;
            slot->slotType = (uint32_t)PassSlotType::InputOutput;
            strncpy(slot->slotName, "Output", sizeof(slot->slotName));

            MsgPassConn *conn = (MsgPassConn *)pos;
            passHead->connCnt = 1;
            pos += sizeof(MsgPassConn) * passHead->connCnt;
            strncpy(conn->localSlot, "Output", sizeof(conn->localSlot));
            strncpy(conn->refPassName, node->GetName().GetCStr(), sizeof(conn->refPassName));
            strncpy(conn->refAttName, "Output", sizeof(conn->refAttName));

            passHead->imgCnt = 0;
            passHead->bufCnt = 0;
            passHead->CalcBodyLen();
            passTlv->len = passHead->bodyLen + sizeof(MsgPass);
            uint32_t totalLen = (uint32_t)(pos - buf);

            return totalLen;
        }

        uint32_t PassDistSystem::ParsePassAttrsMsg(void *passMsgStart, PassSlotList &slots, PassConnectionList &conns,
            PassImageAttachmentDescList &imgs, PassBufferAttachmentDescList &bufs)
        {
            MsgPass *passHead = (MsgPass *)passMsgStart;
            char *pos = (char *)passHead + sizeof(MsgPass);
            MsgPassSlot *slotInfo = (MsgPassSlot *)pos;
            printf("ParsePassAttrsMsg slot %u conn %u img %u buf %u\n",
                passHead->slotCnt, passHead->connCnt, passHead->imgCnt, passHead->bufCnt);
            for (int i = 0; i < passHead->slotCnt; i++)
            {
                PassSlot slot;
                printf("Parse slot info %p\n", (void *)slotInfo);
                slot.m_name = Name(slotInfo->slotName);
                slot.m_slotType = (PassSlotType)slotInfo->slotType;
                slots.emplace_back(slot);
                printf("Recv Pass slot [%s] type %u\n", slotInfo->slotName, slotInfo->slotType);
                slotInfo++;
                pos += sizeof(MsgPassSlot);
            }

            MsgPassConn *connInfo = (MsgPassConn *)pos;
            for (int i = 0; i < passHead->connCnt; i++)
            {
                PassConnection conn;
                printf("Parse conn info %p\n", (void *)connInfo);
                conn.m_localSlot = Name(connInfo->localSlot);
                conn.m_attachmentRef.m_pass = Name(connInfo->refPassName);
                conn.m_attachmentRef.m_attachment = Name(connInfo->refAttName);
                conns.emplace_back(conn);
                printf("Recv Pass conn to local slot [%s] ref pass [%s] atta [%s]\n",
                    connInfo->localSlot, connInfo->refPassName, connInfo->refAttName);
                connInfo++;
                pos += sizeof(MsgPassConn);
            }

            MsgPassAttImg *imgInfo = (MsgPassAttImg *)pos;
            for (int i = 0; i < passHead->imgCnt; i++)
            {
                PassImageAttachmentDesc imgDesc;
                printf("Parse img info %p\n", (void *)imgInfo);
                imgDesc.m_name = Name(imgInfo->name);
                imgDesc.m_imageDescriptor.m_bindFlags = (RHI::ImageBindFlags)imgInfo->bindFlags;
                imgDesc.m_imageDescriptor.m_size.m_width = imgInfo->width;
                imgDesc.m_imageDescriptor.m_size.m_height = imgInfo->height;
                imgDesc.m_imageDescriptor.m_size.m_depth = imgInfo->depth;
                imgDesc.m_imageDescriptor.m_arraySize = (uint16_t)imgInfo->arraySize;
                imgDesc.m_imageDescriptor.m_format = (RHI::Format)imgInfo->format;
                imgs.emplace_back(imgDesc);
                printf("Recv Pass atta image [%s] size 0x %x_%x_%x\n",
                    imgInfo->name, imgInfo->width, imgInfo->height, imgInfo->depth);
                imgInfo++;
                pos += sizeof(MsgPassAttImg);
            }

            MsgPassAttBuf *bufInfo = (MsgPassAttBuf *)pos;
            for (int i = 0; i < passHead->bufCnt; i++)
            {
                PassBufferAttachmentDesc bufDesc;
                printf("Parse buf info %p\n", (void *)bufInfo);
                bufDesc.m_name = Name(bufInfo->name);
                bufDesc.m_bufferDescriptor.m_byteCount = bufInfo->size;
                bufDesc.m_bufferDescriptor.m_alignment = bufInfo->align;
                bufDesc.m_bufferDescriptor.m_bindFlags = (RHI::BufferBindFlags)bufInfo->bindFlags;
                bufs.emplace_back(bufDesc);
                printf("Recv Pass atta buffer [%s] size 0x%lx\n",
                    bufInfo->name, bufInfo->size);
                bufInfo++;
                pos += sizeof(MsgPassAttBuf);
            }
            return 0;
        }

        Ptr<Pass> PassDistSystem::PassCreateFromTemplateMsg(char *buf, uint32_t len)
        {
            char *pos = buf;
            MsgPass *passHead = (MsgPass *)pos;
            pos += sizeof(MsgPass);
            AZStd::shared_ptr<PassTemplate> passTemplate = AZStd::make_shared<PassTemplate>();
            Name passName = Name(passHead->name);
            passTemplate->m_name = Name(passHead->passTemp);
            passTemplate->m_passClass = Name(passHead->passClass);

            printf("Recv Pass %p, [%s] create from template [%s] class [%s]\n",
                (void *)passHead, passHead->name, passHead->passTemp, passHead->passClass);

            ParsePassAttrsMsg((void *)passHead, passTemplate->m_slots, passTemplate->m_connections,
                passTemplate->m_imageAttachments, passTemplate->m_bufferAttachments);

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, passName);
            RenderPipelinePtr pipeline = GetDistPipeline(1);
            if (pipeline == nullptr)
            {
                const RenderPipelineDescriptor desc {.m_name = AZStd::string("Test_0")};
                pipeline = PassDistSystemInterface::Get()->CreateDistPipeline(1, desc);
            }
            pipeline->GetRootPass()->AddChild(add);
            add->Build(false);
            return add;
            //return nullptr;
        }

        Ptr<Pass> PassDistSystem::PassCreateFromRequestMsg(char *buf, uint32_t len)
        {
            char *pos = buf;
            MsgPass *passHead = (MsgPass *)pos;
            pos += sizeof(MsgPass);

            PassRequest req;
            PassSlotList slots;
            req.m_passName = Name(passHead->name);
            req.m_templateName = Name(passHead->passTemp);

            printf("Recv Pass [%s] create from request template [%s]\n",
                passHead->name, passHead->passTemp);

            ParsePassAttrsMsg((void *)passHead, slots, req.m_connections,
                req.m_imageAttachmentOverrides, req.m_bufferAttachmentOverrides);

            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            RenderPipelinePtr pipeline = GetDistPipeline(1);
            if (pipeline == nullptr)
            {
                const RenderPipelineDescriptor desc {.m_name = AZStd::string("Test_0")};
                pipeline = PassDistSystemInterface::Get()->CreateDistPipeline(1, desc);
            }
            pipeline->GetRootPass()->AddChild(add);
            add->Build(false);
            return add;
            //return nullptr;
        }

        uint32_t PassDistSystem::ParsePassCreateMsg(char *buf, uint32_t len)
        {
            char *pos = buf;
            uint32_t cur = 0;
            printf("ParsePassCreateMsg %p\n", (void *)buf);
            do
            {
                MsgTlvInfo *tlv = (MsgTlvInfo *)pos;
                pos += sizeof(MsgTlvInfo);
                cur += sizeof(MsgTlvInfo);
                if (tlv->type != (uint32_t)DistTlvType::Pass)
                {
                    printf("error tlv type %u\n", tlv->type);
                    cur += tlv->len;
                    pos += tlv->len;
                    continue;
                }
                MsgPass *passHead = (MsgPass *)pos;
                if (passHead->createType == (uint32_t)PassCreateType::Template)
                {
                    Ptr<Pass> pass = PassCreateFromTemplateMsg(pos, passHead->bodyLen);
                }
                else if (passHead->createType == (uint32_t)PassCreateType::Request)
                {
                    Ptr<Pass> pass = PassCreateFromRequestMsg(pos, passHead->bodyLen);
                }
                else
                {
                    printf("error pass create type %u\n", passHead->createType);
                }
                cur += tlv->len;
                pos += tlv->len;
            } while(cur < len);
            return cur;
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
                mconn.m_attachmentRef.m_pass = prePass->GetName();
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
            Ptr<Pass> distPass = CreateFullscreenShadowDistPass(Name(distName.c_str()), prePass, pass);
            Ptr<Pass> afterPass = CreateFullscreenShadowDistAfterPass(Name(afterName.c_str()), distPass);
            root->AddChild(prePass);
            root->AddChild(distPass);
            root->AddChild(afterPass);

            char *buf = (char *)malloc(10240);
            //memset(buf, 0xff, 10240);
            MsgHead *msgHead = (MsgHead *)buf;
            int cur = sizeof(MsgHead);
            msgHead->taskId = 0;
            cur += CreateFullscreenShadowDistPrePassMsg(buf + cur, 10240 - cur, Name(preName.c_str()), pass);
            printf("CreateFullscreenShadowDistPrePassMsg encode len %d\n", cur);
            cur += CreateFullscreenShadowDistPassMsg(buf + cur, 10240 - cur, Name(distName.c_str()), prePass, pass);
            printf("CreateFullscreenShadowDistPassMsg encode len %d\n", cur);
            cur += CreateFullscreenShadowDistAfterPassMsg(buf + cur, 10240 - cur, Name(afterName.c_str()), distPass);
            printf("CreateFullscreenShadowDistAfterPassMsg encode len %d\n", cur);
            msgHead->msgLen = cur;
            EnquePassMsg((void *)buf);
            printf("enque pass message len %u\n", cur);
            DumpMsg("pack.txt", buf, msgHead->msgLen);

            //ParsePassCreateMsg((char *)msgHead + sizeof(MsgHead), msgHead->msgLen - sizeof(MsgHead));
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

        RenderPipelinePtr PassDistSystem::CreateDistPipeline(int device, const RenderPipelineDescriptor &desc)
        {
            RenderPipelinePtr pipeline = RenderPipeline::CreateRenderPipeline(desc);
            m_devPipelines.emplace(device, pipeline);
            printf("PassDistSystem add pipeline [%s] on device %d\n",
                desc.m_name.c_str(), device);
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

        void PassDistSystem::SetActivePipeline(Name name)
        {
            printf("PassDistSystem set current active pipeline [%s]\n", name.GetCStr());
            m_activePipeline = name;
            if (name.IsEmpty())
            {
                m_isServer = true;
            }
            char *path = getenv("IPC_PATH");
            if (!path)
            {
                path = (char *)"/tmp/cross_gpu_ipc";
            }
            CommInit(m_isServer, (const char *)path);
        }

        Name PassDistSystem::GetActivePipeline(void)
        {
            printf("PassDistSystem get current active pipeline %s\n", m_activePipeline.GetCStr());
            return m_activePipeline;
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
