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
#include <Atom/RPI.Reflect/Pass/CommPassData.h>
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
#include <mutex>

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
            char *value = getenv("SPLIT");
            if (value)
            {
                int count = atoi(value);
                if (count > 1)
                {
                    m_splitInfo.m_splitCnt = (uint16_t)count;
                    printf("PassDistSystem::Init update split count %u\n", m_splitInfo.m_splitCnt);
                }
            }
            m_dataInputQues.resize(m_splitInfo.m_splitCnt);
            m_dataOutputQues.resize(m_splitInfo.m_splitCnt);
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
        
        void *SeverSendThread(void *arg)
        {
            int cfd = *(int *)arg;
            uint32_t splitIdx = 0;
            void *msg;

            for (int loop = 0;;loop++)
            {
                if (!(loop & 1))
                //if (1)
                {
                    msg = PassDistSystemInterface::Get()->DequePassMsg();
                }
                else
                {
                    msg = PassDistSystemInterface::Get()->DequeInputDataMsg(splitIdx);
                }
                MsgHead *msgHead = (MsgHead *)msg;
                splitIdx = msgHead->splitInfo.m_splitIdx;
                printf("SeverSendThread msg_deque message len %u deque_oper buf %p ticket %lu index %u\n",
                    msgHead->msgLen, msg, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
                //DumpMsg("send-pass.txt", (char *)msg, msgHead->msgLen);
                uint32_t len = (uint32_t)write(cfd, msg, msgHead->msgLen);
                if (len <= 0)
                {
                    free(msg);
                    printf("SeverSendThread msg_write client error free_oper buf %p!\n", msg);
                    break;
                }
                free(msg);
                printf("SeverSendThread msg_write client len %u free_oper buf %p\n", len, msg);
            }
            close(cfd);
            printf("SeverSendThread msg_error socket send to client disconnected!\n");
            *(int *)arg = 0;
            return nullptr;
        }

        void *SeverRecvThread(void *arg)
        {
            int sfd = *(int *)arg;

            for (;;)
            {
                if (PassDistSystemInterface::Get()->Recv(sfd))
                {
                    break;
                }
            }
            close(sfd);
            printf("SeverRecvThread socket recv from client disconnected!\n");
            *(int *)arg = 0;
            return nullptr;
        }

        void *ServerDaemon(void *arg)
        {
            int sfd = *(int *)arg;
            int fdPool[16] = {0};
            for (;;)
            {
                printf("ServerDaemon Waiting to accept a connection...\n");
                int cfd = accept(sfd, NULL, NULL);
                printf("ServerDaemon Accepted socket fd = %d\n", cfd);
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
                    CreateThread(&SeverSendThread, &fdPool[i%16]);
                    CreateThread(&SeverRecvThread, &fdPool[i%16]);
                }
            }
            return nullptr;
        }

        void *ClientRecvThread(void *arg)
        {
            int sfd = *(int *)arg;
            for (;;)
            {
                if (!PassDistSystemInterface::Get()->Recv(sfd))
                {
                    PassDistSystemInterface::Get()->Send(sfd);
                    continue;
                }
                sleep(1);
                sfd = PassDistSystemInterface::Get()->Connect(sfd);
            }
            return nullptr;
        }

        void *ClientSendThread(void *arg)
        {
            int sfd = *(int *)arg;
            for (;;)
            {
                if (!PassDistSystemInterface::Get()->SendQue(sfd))
                {
                    continue;
                }
                sleep(1);
                sfd = PassDistSystemInterface::Get()->Connect(sfd);
            }
            return nullptr;
        }

        int RecvOneMsg(int sfd, char **msg)
        {
            MsgHead msgHead;
            ssize_t ret = read(sfd, &msgHead, sizeof(MsgHead));
            if (ret != sizeof(MsgHead))
            {
                printf("RecvOneMsg msg_error recv data error message head %d!\n", (int)ret);
                return -1;
            }
            printf("RecvOneMsg msg_read_head message type %u len %u ticket %ld index %u\n",
                msgHead.msgType, msgHead.msgLen, msgHead.ticket,
                msgHead.splitInfo.m_splitIdx);
            if (msgHead.msgType >= (uint32_t)DistMsgType::Count)
            {
                printf("RecvOneMsg msg_error recv invalid message type %d!\n", (int)msgHead.msgType);
                return -1;
            }
            char *buf = (char *)malloc(msgHead.msgLen);
            printf("RecvOneMsg msg_read_body message alloc_oper buf %p\n", buf);
            memcpy(buf, &msgHead, sizeof(MsgHead));
            uint32_t curLen = sizeof(MsgHead);
            while (curLen < msgHead.msgLen)
            {
                ssize_t numRead = read(sfd, buf + curLen, msgHead.msgLen - curLen);
                if (numRead <= 0)
                {
                    free(buf);
                    printf("RecvOneMsg msg_read data error message body free_oper buf %p!\n", buf);
                    return -1;
                }
                curLen += (uint32_t)numRead;
            }
            if (curLen != msgHead.msgLen)
            {
                free(buf);
                printf("RecvOneMsg msg_read data error message len %u free_oper buf %p!\n", curLen, buf);
                return -1;
            }
            *msg = buf;
            return 0;
        }

        void PassDistSystem::CommInit(bool isServer, const char *path)
        {
            struct sockaddr_un addr;

            m_sfd = socket(AF_UNIX, SOCK_STREAM, 0);
            printf("PassDistSystem::CommInit Server socket fd = %d\n", m_sfd);
            if (m_sfd == -1) {
                printf("PassDistSystem::CommInit socket error!\n");
                return;
            }

            if (isServer && remove(path) == -1 && errno != ENOENT)
            {
                printf("PassDistSystem::CommInit remove %s error!\n", path);
                return;
            }

            m_commPath = Name(path);

            if (isServer)
            {
                memset(&addr, 0, sizeof(struct sockaddr_un));
                addr.sun_family = AF_UNIX;
                strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
                if (bind(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1) {
                    printf("PassDistSystem::CommInit bind error %d\n", (int)errno);
                    return;
                }
                if (listen(m_sfd, 16) == -1) {
                    printf("PassDistSystem::CommInit listen error %d!\n", (int)errno);
                    return;
                }
                CreateThread(&ServerDaemon, (void *)&m_sfd);
                printf("PassDistSystem::CommInit Dist Daemon thread create ok!\n");
            }
            else
            {
                if (connect(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
                {
                    printf("PassDistSystem::CommInit connect error %d!\n", (int)errno);
                }
                CreateThread(&ClientRecvThread, (void *)&m_sfd);
                CreateThread(&ClientSendThread, (void *)&m_sfd);
                printf("PassDistSystem::CommInit Dist Daemon thread create ok!\n");
            }
        }

        int PassDistSystem::Connect(int sfd)
        {
            struct sockaddr_un addr;
            static std::mutex connMutex;
            const std::lock_guard<std::mutex> lock(connMutex);

            if (!Send(m_sfd))
            {
                printf("PassDistSystem::Connect already connected!\n");
                return m_sfd;
            }

            memset(&addr, 0, sizeof(struct sockaddr_un));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, m_commPath.GetCStr(), sizeof(addr.sun_path) - 1);

            if (!connect(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)))
            {
                printf("PassDistSystem::Connect connect success try 1!\n");
                return m_sfd;
            }

            printf("PassDistSystem::Connect connect error %d try 1!\n", (int)errno);

            close(m_sfd);
            m_sfd = socket(AF_UNIX, SOCK_STREAM, 0);
            printf("PassDistSystem::Connect socket fd = %d\n", m_sfd);
            if (m_sfd == -1) {
                printf("PassDistSystem::Connect socket error %d!\n", (int)errno);
                return -1;
            }

            if (connect(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
            {
                printf("PassDistSystem::Connect connect error %d try 2!\n", (int)errno);
                return -1;
            }
            printf("PassDistSystem::Connect connect successfully!\n");
            return m_sfd;
        }

        int PassDistSystem::Send(int sfd)
        {
            char buf[256];
            const char *msg = "client send ack ...............\n";
            MsgHead *msgHead = (MsgHead *)buf;
            msgHead->msgType = (uint32_t)DistMsgType::Debug;
            msgHead->ticket = m_ticket;
            msgHead->splitInfo.m_splitCnt = 0;
            msgHead->splitInfo.m_splitIdx = 0;
            MsgDebugInfo *debugInfo = (MsgDebugInfo *)(buf + sizeof(MsgHead));
            debugInfo->nodeId = 1;
            debugInfo->infoLen = strnlen(msg, 256) + 1 + sizeof(MsgDebugInfo);
            strncpy(buf + sizeof(MsgHead) + sizeof(MsgDebugInfo), msg, 256 - sizeof(MsgHead) - sizeof(MsgDebugInfo));
            msgHead->msgLen = debugInfo->infoLen + sizeof(MsgHead);
            printf("PassDistSystem::Send ack_reply msg %p type %u, len %u ticket %ld\n",
                buf, msgHead->msgType, msgHead->msgLen, msgHead->ticket);
            if (write(sfd, buf, msgHead->msgLen) != msgHead->msgLen)
            {
                printf("PassDistSystem::Send ack_error %p partial/failed write error!\n", buf);
                return -1;
            }
            return 0;
        }

        int PassDistSystem::SendQue(int sfd)
        {
            char *msg;
            if (m_isServer)
            {
                printf("PassDistSystem::SendQue error server use Send method!\n");
                return -1;
            }
            else
            {
                if (m_sendFailMsg)
                {
                    msg = (char *)m_sendFailMsg;
                }
                else
                {
                    msg = (char *)DequeOutputDataMsg(0);
                }
            }
            MsgHead *msgHead = (MsgHead *)msg;
            if (write(sfd, msg, msgHead->msgLen) != msgHead->msgLen)
            {
                printf("PassDistSystem::SendQue msg_error partial/failed write error %p len %u ticket %lu index %u!\n",
                    msg, msgHead->msgLen, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
                m_sendFailMsg = msg;
                return -1;
            }
            m_sendFailMsg = nullptr;
            printf("PassDistSystem::SendQue msg_write %p len %u ticket %ld index %u success!\n",
                msg, msgHead->msgLen, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
            free(msg);
            return 0;
        }

        int PassDistSystem::Recv(int sfd)
        {
            MsgHead *msgHead;
            char *msg;

            if (RecvOneMsg(sfd, &msg) < 0)
            {
                return -1;
            }
            msgHead = (MsgHead *)msg;
            if (msgHead->msgType == (uint32_t)DistMsgType::PassGraph)
            {
                printf("PassDistSystem::Recv msg_enque pass graph len %u enque_oper buf %p ticket %lu index %u\n",
                    msgHead->msgLen, msg, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
                EnquePassMsg((void *)msg);
                return 0;
            }

            if (msgHead->msgType == (uint32_t)DistMsgType::PassData)
            {
                printf("PassDistSystem::Recv msg_enque pass data len %u enque_oper buf %p ticket %lu index %u\n",
                    msgHead->msgLen, msg, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
                if (m_isServer)
                {
                    if (msgHead->ticket == m_ticket)
                    {
                        EnqueOutputDataMsg(msgHead->splitInfo.m_splitIdx, (void *)msg);
                    }
                    else
                    {
                        printf("PassDistSystem::Recv msg_error pass data ticket %ld != cur %ld\n", msgHead->ticket, m_ticket);
                        free(msg);
                    }
                }
                else
                {
                    EnqueInputDataMsg(0, (void *)msg);
                }
                return 0;
            }

            if (msgHead->msgType == (uint32_t)DistMsgType::Debug)
            {
                MsgDebugInfo *debugInfo = (MsgDebugInfo *)(msg + sizeof(MsgHead));
                printf("PassDistSystem::Recv ack_debug info len %u, node %u, info: %s\n", debugInfo->infoLen,
                    debugInfo->nodeId, msg + sizeof(MsgHead) + sizeof(MsgDebugInfo));
            }
            else
            {
                printf("PassDistSystem::Recv msg_error invalid message type %u\n", msgHead->msgType);
            }

            printf("PassDistSystem::Recv ack_end proc free_oper buf %p\n", msg);

            free(msg);

            return 0;
        }

        void ProcessSubPasses(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>>& subPasses)
        {
            ParentPass *parent = pass->AsParent();
            if (parent == nullptr) {
                subPasses.emplace(pass->GetName(), pass);
                //printf("PassDistSystem add leaf pass %s\n", pass->GetName().GetCStr());
            } else {
                subPasses.emplace(pass->GetName(), pass);
                //printf("PassDistSystem walk parent pass %s\n", pass->GetName().GetCStr());
                for (auto& subPass : parent->GetChildren()) {
                    ProcessSubPasses(subPass, subPasses);
                }
            }
        }

        void *PassDistSystem::DequePassMsg(bool noWait)
        {
            return m_passMsgQue.P(noWait);
        }

        void PassDistSystem::EnquePassMsg(void *data)
        {
            m_passMsgQue.V(data);
        }

        void *PassDistSystem::DequeInputDataMsg(uint32_t queId, bool noWait)
        {
            return m_dataInputQues[queId].P(noWait);
        }

        void PassDistSystem::EnqueInputDataMsg(uint32_t queId, void *data)
        {
            m_dataInputQues[queId].V(data);
        }

        void *PassDistSystem::DequeOutputDataMsg(uint32_t queId, bool noWait)
        {
            return m_dataOutputQues[queId].P(noWait);
        }

        void PassDistSystem::EnqueOutputDataMsg(uint32_t queId, void *data)
        {
            m_dataOutputQues[queId].V(data);
        }

        int PassDistSystem::SendData(void *data[], uint32_t len[], uint32_t count, SplitInfo &splitInfo)
        {
            uint32_t totalLen = sizeof(MsgHead);
            for (uint32_t msgCnt = 0; msgCnt < count; msgCnt++)
            {
                totalLen += len[msgCnt] + sizeof(MsgPassData);
            }
            char *msg = (char *)malloc(totalLen);
            MsgHead *msgHead = (MsgHead *)msg;
            msgHead->msgType = (uint32_t)DistMsgType::PassData;
            msgHead->msgLen = totalLen;
            msgHead->splitInfo.m_splitCnt = splitInfo.m_splitCnt;
            msgHead->splitInfo.m_splitIdx = splitInfo.m_splitIdx;
            msgHead->ticket = m_ticket;
            uint32_t curPos = sizeof(MsgHead);
            for (uint32_t msgCnt = 0; msgCnt < count; msgCnt++)
            {
                MsgPassData *passData = (MsgPassData *)(msg + curPos);
                passData->dataLen = len[msgCnt] + sizeof(MsgPassData);
                passData->nodeId = 0;
                curPos += sizeof(MsgPassData);
                memcpy(msg + curPos, data[msgCnt], len[msgCnt]);
                curPos += len[msgCnt];
                printf("PassDistSystem::SendData msg_pack data %p len %u number %u\n", data[msgCnt], len[msgCnt], msgCnt);
            }
            if (curPos != totalLen)
            {
                printf("PassDistSystem::SendData error curPos %u != total len %u\n", curPos, totalLen);
            }
            printf("PassDistSystem::SendData server %d put msg_enque %p len %u count %u ticket %ld index %u\n", 
                (int)m_isServer, msg, totalLen, count, msgHead->ticket, splitInfo.m_splitIdx);
            if (m_isServer)
            {
                EnqueInputDataMsg(splitInfo.m_splitIdx, (void *)msg);
            }
            else
            {
                EnqueOutputDataMsg(0, (void *)msg);
            }
            return 0;
        }

        int PassDistSystem::RecvData(void *data[], uint32_t len[], uint32_t size, uint32_t *count, SplitInfo &splitInfo)
        {
            char *msg;
            printf("PassDistSystem::RecvData msg_wait for data count %u index %u\n", size, splitInfo.m_splitIdx);
            if (m_isServer)
            {
                msg = (char *)DequeOutputDataMsg(splitInfo.m_splitIdx);
            }
            else
            {
                msg = (char *)DequeInputDataMsg(0);
            }
            MsgHead *msgHead = (MsgHead *)msg;
            uint32_t curPos = sizeof(MsgHead);
            uint32_t msgCnt = 0;
            while (curPos < msgHead->msgLen && msgCnt < size)
            {
                MsgPassData *passData = (MsgPassData *)(msg + curPos);
                curPos += sizeof(MsgPassData);
                len[msgCnt] = passData->dataLen - sizeof(MsgPassData);
                data[msgCnt] = malloc(len[msgCnt]);
                memcpy(data[msgCnt], msg + curPos, len[msgCnt]);
                printf("PassDistSystem::RecvData msg_extract server %d get msg %p len %u\n",
                    (int)m_isServer, data[msgCnt], len[msgCnt]);
                curPos += len[msgCnt];
                msgCnt++;
            }
            splitInfo.m_splitCnt = msgHead->splitInfo.m_splitCnt;
            splitInfo.m_splitIdx = msgHead->splitInfo.m_splitIdx;
            *count = msgCnt;
            printf("PassDistSystem::RecvData msg_deque %p size %u ticket %lu index %u total recv data count %u\n",
                msg, size, msgHead->ticket, splitInfo.m_splitIdx, msgCnt);
            if (curPos != msgHead->msgLen)
            {
                printf("PassDistSystem::RecvData msg_error cur msg pos %u != msg len %u\n",
                    curPos, msgHead->msgLen);
            }
            free(msg);
            return 0;
        }

        void PassDistSystem::AddCommPassSlot(AZStd::shared_ptr<PassTemplate> &passTemplate, PassSlotType slotType, std::string suffix)
        {
            PassSlot slot;
            if (slotType == PassSlotType::InputOutput)
            {
                slot.m_name = std::string(std::string("InputOutput") + suffix).c_str();
                slot.m_slotType = PassSlotType::InputOutput;
                slot.m_scopeAttachmentUsage = RHI::ScopeAttachmentUsage::Copy;
                slot.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Load;
                passTemplate->m_slots.emplace_back(slot);
            }
            else if (slotType == PassSlotType::Input)
            {
                slot.m_name = std::string(std::string("Input") + suffix).c_str();
                slot.m_slotType = PassSlotType::Input;
                slot.m_scopeAttachmentUsage = RHI::ScopeAttachmentUsage::Copy;
                slot.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Load;
                passTemplate->m_slots.emplace_back(slot);
            }
            else
            {
                slot.m_name = std::string(std::string("Output") + suffix).c_str();
                slot.m_slotType = PassSlotType::Output;
                slot.m_scopeAttachmentUsage = RHI::ScopeAttachmentUsage::Copy;
                slot.m_loadStoreAction.m_loadAction = RHI::AttachmentLoadAction::Clear;
                passTemplate->m_slots.emplace_back(slot);
            }
        }

        AZStd::shared_ptr<PassTemplate> PassDistSystem::CreateCommPassTemplate(Name tempName, PassSlotType slotType, uint32_t count)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = tempName;
            passTemplate->m_passClass = "CommPass";
            if (count == 1)
            {
                AddCommPassSlot(passTemplate, slotType, "");
            }
            else
            {
                for (uint32_t index = 0; index < count; index++)
                {
                    AddCommPassSlot(passTemplate, slotType, std::to_string(index));
                }
            }
            return passTemplate;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowPrePass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate = CreateCommPassTemplate(Name("FullscreenShadowPassPreTemplate"),
                PassSlotType::InputOutput, 3);

            PassSlot slot;
            PassConnection conn;

            conn.m_localSlot = "InputOutput0";
            conn.m_attachmentRef.m_pass = "Cascades";
            conn.m_attachmentRef.m_attachment = "Shadowmap";
            passTemplate->m_connections.emplace_back(conn);

            conn.m_localSlot = "InputOutput1";
            conn.m_attachmentRef.m_pass = "PipelineGlobal";
            conn.m_attachmentRef.m_attachment = "DepthMSAA";
            passTemplate->m_connections.emplace_back(conn);

            conn.m_localSlot = "InputOutput2";
            conn.m_attachmentRef.m_pass = "PipelineGlobal";
            conn.m_attachmentRef.m_attachment = "DepthLinear";
            passTemplate->m_connections.emplace_back(conn);

            auto passData = AZStd::make_shared<CommPassData>();
            passData->m_submit = false;
            passData->m_cloneInput = false;
            passData->m_splitInfo.m_splitCnt = m_splitInfo.m_splitCnt;
            passData->m_splitInfo.m_splitIdx = 1; // first dist index
            passData->m_commOper = CommOper::CopyInput;
            passTemplate->m_passData = passData;

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowAfterPass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate = CreateCommPassTemplate(Name("FullscreenShadowPassAfterTemplate"),
                PassSlotType::InputOutput, 1);

            PassConnection conn;
            conn.m_localSlot = "InputOutput";
            conn.m_attachmentRef.m_pass = node->GetName();
            conn.m_attachmentRef.m_attachment = "Output";
            passTemplate->m_connections.emplace_back(conn);

            auto passData = AZStd::make_shared<CommPassData>();
            passData->m_submit = false;
            passData->m_cloneInput = false;
            passData->m_splitInfo.m_splitCnt = m_splitInfo.m_splitCnt;
            passData->m_splitInfo.m_splitIdx = 1; // first dist index
            passData->m_commOper = CommOper::MergeOutput;
            passTemplate->m_passData = passData;

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistPrePassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node)
        {
            char *pos = buf;
            printf("CreateFullscreenShadowDistPrePassMsg tlv start %p\n", (void *)pos);
            MsgPassGraph *passHead = (MsgPassGraph *)pos;
            pos += sizeof(MsgPassGraph);
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

            MsgPassCommInfo *commInfo = (MsgPassCommInfo *)pos;
            commInfo->isCommPass = 1;
            commInfo->commOper = (uint16_t)CommOper::PrepareInput;
            passHead->commCnt = 1;
            pos += sizeof(MsgPassCommInfo) * passHead->commCnt;

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
            uint32_t totalLen = (uint32_t)(pos - buf);
            return totalLen;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistPassMsg(char *buf, uint32_t len, Name name, Name prePassName, Ptr<Pass> node)
        {
            char *pos = buf;

            MsgPassGraph *passHead = (MsgPassGraph *)pos;
            pos += sizeof(MsgPassGraph);
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
            MsgPassCommInfo *commInfo = (MsgPassCommInfo *)pos;
            commInfo->isCommPass = 0;
            commInfo->commOper = (uint16_t)CommOper::None;
            passHead->commCnt = 1;
            pos += sizeof(MsgPassCommInfo) * passHead->commCnt;
            passHead->CalcBodyLen();
            uint32_t totalLen = (uint32_t)(pos - buf);

            return totalLen;
        }

        uint32_t PassDistSystem::CreateFullscreenShadowDistAfterPassMsg(char *buf, uint32_t len, Name name, Name prePassName)
        {
            char *pos = buf;

            MsgPassGraph *passHead = (MsgPassGraph *)pos;
            pos += sizeof(MsgPassGraph);
            passHead->createType = (uint32_t)PassCreateType::Template;
            strncpy(passHead->name, name.GetCStr(), sizeof(passHead->name));
            strncpy(passHead->passTemp, "FullscreenShadowPassDistAfterTemplate", sizeof(passHead->passTemp));
            strncpy(passHead->passClass, "CommPass", sizeof(passHead->passClass));

            MsgPassSlot *slot = (MsgPassSlot *)pos;
            passHead->slotCnt = 1;
            pos += sizeof(MsgPassSlot) * passHead->slotCnt;
            slot->slotType = (uint32_t)PassSlotType::InputOutput;
            strncpy(slot->slotName, "Output", sizeof(slot->slotName));

            MsgPassConn *conn = (MsgPassConn *)pos;
            passHead->connCnt = 1;
            pos += sizeof(MsgPassConn) * passHead->connCnt;
            strncpy(conn->localSlot, "Output", sizeof(conn->localSlot));
            strncpy(conn->refPassName, prePassName.GetCStr(), sizeof(conn->refPassName));
            strncpy(conn->refAttName, "Output", sizeof(conn->refAttName));

            passHead->imgCnt = 0;
            passHead->bufCnt = 0;
            MsgPassCommInfo *commInfo = (MsgPassCommInfo *)pos;
            commInfo->isCommPass = 1;
            commInfo->commOper = (uint16_t)CommOper::CopyOutput;
            passHead->commCnt = 1;
            pos += sizeof(MsgPassCommInfo) * passHead->commCnt;
            passHead->CalcBodyLen();
            uint32_t totalLen = (uint32_t)(pos - buf);

            return totalLen;
        }

        uint32_t PassDistSystem::ParsePassAttrsMsg(void *passMsgStart, PassSlotList &slots, PassConnectionList &conns,
            PassImageAttachmentDescList &imgs, PassBufferAttachmentDescList &bufs, MsgPassCommInfo &commInfo)
        {
            MsgPassGraph *passHead = (MsgPassGraph *)passMsgStart;
            char *pos = (char *)passHead + sizeof(MsgPassGraph);
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

            MsgPassCommInfo *commInfoPtr = (MsgPassCommInfo *)pos;
            for (int i = 0; i < passHead->commCnt; i++)
            {
                commInfo = *commInfoPtr;
                pos += sizeof(MsgPassCommInfo);
            }
            return 0;
        }

        Ptr<Pass> PassDistSystem::PassCreateFromTemplateMsg(char *buf, uint32_t len)
        {
            char *pos = buf;
            MsgPassGraph *passHead = (MsgPassGraph *)pos;
            pos += sizeof(MsgPassGraph);
            AZStd::shared_ptr<PassTemplate> passTemplate = AZStd::make_shared<PassTemplate>();
            Name passName = Name(passHead->name);
            passTemplate->m_name = Name(passHead->passTemp);
            passTemplate->m_passClass = Name(passHead->passClass);
            MsgPassCommInfo commInfo;

            ParsePassAttrsMsg((void *)passHead, passTemplate->m_slots, passTemplate->m_connections,
                passTemplate->m_imageAttachments, passTemplate->m_bufferAttachments, commInfo);

            printf("Recv Pass %p, [%s] create from template [%s] class [%s] comm pass %u oper %u\n",
                (void *)passHead, passHead->name, passHead->passTemp, passHead->passClass,
                commInfo.isCommPass, commInfo.commOper);

            if (commInfo.isCommPass)
            {
                auto passData = AZStd::make_shared<CommPassData>();
                passData->m_cloneInput = false;
                passData->m_submit = false;
                passData->m_splitInfo.m_splitCnt = m_splitInfo.m_splitCnt;
                passData->m_splitInfo.m_splitIdx = m_splitInfo.m_splitIdx;
                passData->m_commOper = (CommOper)commInfo.commOper;
                passTemplate->m_passData = passData;
            }

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, passName);
            return add;
            //return nullptr;
        }

        Ptr<Pass> PassDistSystem::PassCreateFromRequestMsg(char *buf, uint32_t len)
        {
            char *pos = buf;
            MsgPassGraph *passHead = (MsgPassGraph *)pos;
            pos += sizeof(MsgPassGraph);
            AZStd::shared_ptr<PassRequest> req = AZStd::make_shared<PassRequest>();
            m_requests.emplace_back(req);
            PassSlotList slots;
            req->m_passName = Name(passHead->name);
            req->m_templateName = Name(passHead->passTemp);
            MsgPassCommInfo commInfo;

            ParsePassAttrsMsg((void *)passHead, slots, req->m_connections,
                req->m_imageAttachmentOverrides, req->m_bufferAttachmentOverrides, commInfo);

            printf("Recv Pass [%s] create from request template [%s] comm pass %u oper %u\n",
                passHead->name, passHead->passTemp, commInfo.isCommPass, commInfo.commOper);

            if (commInfo.isCommPass)
            {
                auto passData = AZStd::make_shared<CommPassData>();
                passData->m_cloneInput = false;
                passData->m_submit = false;
                passData->m_splitInfo.m_splitCnt = m_splitInfo.m_splitCnt;
                passData->m_splitInfo.m_splitIdx = m_splitInfo.m_splitIdx;
                passData->m_commOper = (CommOper)commInfo.commOper;
                req->m_passData = passData;
            }

            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(req.get());
            return add;
            //return nullptr;
        }

        uint32_t PassDistSystem::ParsePassCreateMsg(char *buf, uint32_t len, Ptr<ParentPass> &root)
        {
            char *pos = buf;
            uint32_t cur = 0;
            printf("ParsePassCreateMsg %p\n", (void *)buf);
            do
            {
                MsgPassGraph *passHead = (MsgPassGraph *)pos;
                Ptr<Pass> pass = nullptr;
                if (passHead->createType == (uint32_t)PassCreateType::Template)
                {
                    pass = PassCreateFromTemplateMsg(pos, passHead->passLen);
                }
                else if (passHead->createType == (uint32_t)PassCreateType::Request)
                {
                    pass = PassCreateFromRequestMsg(pos, passHead->passLen);
                }
                else
                {
                    printf("error pass create type %u\n", passHead->createType);
                }
                if (pass)
                {
                    root->AddChild(pass);
                    pass->Build(false);
                }

                cur += passHead->passLen;
                pos += passHead->passLen;
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

        bool NameStartWith(const Name name, const char *start)
        {
            char *subStr = strstr((char *)name.GetCStr(), start);
            if (!subStr || subStr != (char *)name.GetCStr()) {
                return false;
            }
            return true;
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

        void PassDistSystem::ModifyFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> &subPasses)
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
            {
                PassConnection conn;
                conn.m_localSlot = "DirectionalShadowmaps";
                conn.m_attachmentRef.m_pass = prePass->GetName();
                conn.m_attachmentRef.m_attachment = "InputOutput0";
                pass->m_request.m_connections.emplace_back(conn);

                conn.m_localSlot = "Depth";
                conn.m_attachmentRef.m_pass = prePass->GetName();
                conn.m_attachmentRef.m_attachment = "InputOutput1";
                pass->m_request.m_connections.emplace_back(conn);

                conn.m_localSlot = "DepthLinear";
                conn.m_attachmentRef.m_pass = prePass->GetName();
                conn.m_attachmentRef.m_attachment = "InputOutput2";
                pass->m_request.m_connections.emplace_back(conn);
            }
            for (auto &conn : pass->m_request.m_connections)
            {
                printf("After modify pass [%s] slot [%s] connect name [%s] slot [%s]\n",
                    pass->GetName().GetCStr(), conn.m_localSlot.GetCStr(),
                    conn.m_attachmentRef.m_pass.GetCStr(),
                    conn.m_attachmentRef.m_attachment.GetCStr());
            }
            for (auto& itr : subPasses)
            {
                auto curPass = itr.second;
                //printf("modifing pass %s\n", curPass->GetName().GetCStr());
                for (PassConnection &conn : curPass->m_request.m_connections)
                {
                    //printf("modifing pass %s slot %s connect pass %s\n", curPass->GetName().GetCStr(),
                    //    conn.m_localSlot.GetCStr(), conn.m_attachmentRef.m_pass.GetCStr());
                    if (conn.m_attachmentRef.m_pass == pass->GetName())
                    {
                        conn.m_attachmentRef.m_pass = afterPass->GetName();
                        conn.m_attachmentRef.m_attachment = "InputOutput";
                        follows.emplace_back(pass);
                        printf("modifing pass %s slot %s connect pass to %s\n", curPass->GetName().GetCStr(),
                            conn.m_localSlot.GetCStr(), conn.m_attachmentRef.m_pass.GetCStr());
                    }
                }
            }
            struct PassDistNode node = {prePass, pass, afterPass, follows};
            AddDistNode(node);
        }

        void PassDistSystem::CloneFullscreenShadow(Ptr<Pass> pass)
        {
            std::string orig = pass->GetName().GetCStr();
            std::string  preName = orig + "_DistPre";
            std::string  distName = orig + "_Dist";
            std::string  afterName = orig + "_DistAfter";
            char *buf = (char *)malloc(10240);
            printf("CloneFullscreenShadow alloc_oper buf %p\n", buf);
            //memset(buf, 0xff, 10240);
            MsgHead *msgHead = (MsgHead *)buf;
            int cur = sizeof(MsgHead);
            msgHead->msgType = (uint32_t)DistMsgType::PassGraph;
            msgHead->ticket = m_ticket;
            msgHead->splitInfo.m_splitCnt = m_splitInfo.m_splitCnt;
            msgHead->splitInfo.m_splitIdx = 1;
            cur += CreateFullscreenShadowDistPrePassMsg(buf + cur, 10240 - cur, Name(preName.c_str()), pass);
            printf("CreateFullscreenShadowDistPrePassMsg encode len %d\n", cur);
            cur += CreateFullscreenShadowDistPassMsg(buf + cur, 10240 - cur, Name(distName.c_str()), Name(preName.c_str()), pass);
            printf("CreateFullscreenShadowDistPassMsg encode len %d\n", cur);
            cur += CreateFullscreenShadowDistAfterPassMsg(buf + cur, 10240 - cur, Name(afterName.c_str()), Name(distName.c_str()));
            printf("CreateFullscreenShadowDistAfterPassMsg encode len %d\n", cur);
            msgHead->msgLen = cur;
            for (uint32_t idx = m_splitInfo.m_splitCnt - 1; idx > 1; idx--)
            {
                char *msg = (char *)malloc(msgHead->msgLen);
                memcpy(msg, buf, msgHead->msgLen);
                MsgHead *head = (MsgHead *)msg;
                head->splitInfo.m_splitIdx = idx;
                printf("CloneFullscreenShadow msg_enque pass message enque_oper buf %p len %u ticket %lu index %u\n",
                    msg, head->msgLen, head->ticket, head->splitInfo.m_splitIdx);
                EnquePassMsg((void *)msg);
            }
            printf("CloneFullscreenShadow msg_enque pass message enque_oper buf %p len %u ticket %lu index %u\n",
                buf, cur, msgHead->ticket, msgHead->splitInfo.m_splitIdx);
            EnquePassMsg((void *)buf);
        }

        void PassDistSystem::ProcessFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses)
        {
            if (!IsDistProcessed(pass->GetName()))
            {
                printf("pass [%s] to be modified!\n", pass->GetName().GetCStr());
                ModifyFullscreenShadow(pass, subPasses);;
            }
            else
            {
                printf("pass [%s] has been modified!\n", pass->GetName().GetCStr());
            }
            CloneFullscreenShadow(pass);
        }

        void PassDistSystem::ModifyDistPassGraph(Ptr<ParentPass> &root)
        {
            m_ticket = RPISystemInterface::Get()->GetCurrentTick();

            printf("ModifyDistPassGraph pipeline [%s] root pass [%s] ticket %ld\n",
                root->GetRenderPipeline()->GetId().GetCStr(),
                root->GetName().GetCStr(), m_ticket);

            AZStd::unordered_map<Name, Ptr<Pass>> subPasses;
            AZStd::vector<std::function<void()>> procs;
            
            for (auto& pass : root->GetChildren())
            {
                ProcessSubPasses(pass, subPasses);
            }
            for (auto& itr : subPasses)
            {
                auto pass = itr.second;
                if (NameEndWith(pass->GetName(), "FullscreenShadowPass"))
                {
                    printf("current pass [%s] is matched\n", pass->GetName().GetCStr());
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

        void PassDistSystem::BuildDistPassGraph(Ptr<ParentPass> &root)
        {
            root->RemoveChildren();
            m_templates.clear();
            m_requests.clear();
            m_splitInfo.m_splitCnt = 0;
            m_splitInfo.m_splitIdx = 0;
            void *msg = DequePassMsg(true);
            if (msg)
            {
                MsgHead *msgHead = (MsgHead *)msg;
                m_splitInfo.m_splitCnt = msgHead->splitInfo.m_splitCnt;
                m_splitInfo.m_splitIdx = msgHead->splitInfo.m_splitIdx;
                m_ticket = msgHead->ticket;
                printf("BuildDistPassGraph deque pass message len %u deque_oper buf %p\n", msgHead->msgLen, msg);
                //DumpMsg("proc.txt", (char *)msg, msgHead->msgLen);

                ParsePassCreateMsg((char *)msgHead + sizeof(MsgHead), msgHead->msgLen - sizeof(MsgHead), root);

                printf("BuildDistPassGraph after proc pass message len %u free_oper buf %p\n", msgHead->msgLen, msg);
                free(msg);
                printf("### Test pipeline started server %d pass number 3!\n", (int)m_isServer);
            }
            else
            {
                printf("BuildDistPassGraph no pass message found!\n");
            }
        }

        void PassDistSystem::ProcessDistChanges(Ptr<ParentPass> &root)
        {
            if (!m_state)
            {
                printf("PassDistSystem is disabled!\n");
                return;
            }
            if (m_isServer)
            {
                if (NameStartWith(root->GetRenderPipeline()->GetId(), "MainPipeline"))
                {
                    printf("PassDistSystem Server ModifyDistPassGraph pipeline [%s]\n",
                        root->GetRenderPipeline()->GetId().GetCStr());
                    ModifyDistPassGraph(root);
                }
                else
                {
                    printf("PassDistSystem Server ModifyDistPassGraph pipeline [%s] no need modify!\n",
                        root->GetRenderPipeline()->GetId().GetCStr());
                }
            }
            else
            {
                printf("PassDistSystem client BuildDistPassGraph pipeline [%s]\n",
                    root->GetRenderPipeline()->GetId().GetCStr());
                if (root->GetRenderPipeline()->GetId() != GetActivePipeline())
                {
                    printf("PassDistSystem client not activate pipeline [%s]\n",
                        GetActivePipeline().GetCStr());
                    return;
                }
                BuildDistPassGraph(root);
            }
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
#if 0
        Ptr<Pass> PassDistSystem::CreateFullscreenShadowAfterPass(Name name, Ptr<Pass> node)
        {
            PassRequest req;
            req.m_passName = name;
            req.m_templateName = "CopyPassTemplate";

            PassConnection conn;
            conn.m_localSlot = "Input";
            conn.m_attachmentRef.m_pass = node->GetName();
            conn.m_attachmentRef.m_attachment = "Output";
            req.m_connections.emplace_back(conn);

            auto passData = AZStd::make_shared<CopyPassData>();
            passData->m_cloneInput = true;
            req.m_passData = passData;

            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
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

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistAfterPass(Name name, Ptr<Pass> prePass, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassDistAfterTemplate";
            passTemplate->m_passClass = "CommPass";
            //passTemplate->m_passClass = "CopyPass";

            PassSlot slot;
            PassConnection conn;

            slot.m_name = "ImgInput";
            slot.m_slotType = PassSlotType::Input;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = prePass->GetName();
            conn.m_attachmentRef.m_attachment = "Output";
            passTemplate->m_connections.emplace_back(conn);

            PassBufferAttachmentDesc pbd;
            pbd.m_name = "BufferHostVisible";
            auto& refAttachment = node->GetOutputBinding(0).GetAttachment()->m_sizeSource->GetAttachment();
            RHI::ImageDescriptor imgDesc = refAttachment->m_descriptor.m_image;
            pbd.m_bufferDescriptor.m_byteCount = RHI::GetFormatSize(imgDesc.m_format) *
                imgDesc.m_size.m_width * imgDesc.m_size.m_height * imgDesc.m_size.m_depth;
            printf("image size is %d %d %d, format %d size %d\n",
                (int)imgDesc.m_size.m_width, (int)imgDesc.m_size.m_height, (int)imgDesc.m_size.m_depth,
                (int)imgDesc.m_format, (int)RHI::GetFormatSize(imgDesc.m_format));
            pbd.m_bufferDescriptor.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::DynamicInputAssembly;
            passTemplate->m_bufferAttachments.emplace_back(pbd);

            slot.m_name = "BufOutput";
            slot.m_slotType = PassSlotType::Output;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = "This";
            conn.m_attachmentRef.m_attachment = pbd.m_name;
            passTemplate->m_connections.emplace_back(conn);

            //auto passData = AZStd::make_shared<CopyPassData>();
            //passData->m_cloneInput = false;

            auto passData = AZStd::make_shared<CommPassData>();
            passData->m_submit = false;
            passData->m_cloneInput = false;
            passData->m_commOper = CommOper::None;
            passTemplate->m_passData = passData;

            m_templates.emplace_back(passTemplate);
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromTemplate(passTemplate, name);
            return add;
        }

        Ptr<Pass> PassDistSystem::CreateFullscreenShadowDistAfterOutPass(Name name, Ptr<Pass> node)
        {
            AZStd::shared_ptr<PassTemplate> passTemplate;
            passTemplate = AZStd::make_shared<PassTemplate>();
            passTemplate->m_name = "FullscreenShadowPassDistAfterOutTemplate";
            passTemplate->m_passClass = "ComputePass";

            PassSlot slot;
            PassConnection conn;

            slot.m_name = "AfterOutput";
            slot.m_slotType = PassSlotType::InputOutput;
            conn.m_localSlot = slot.m_name;
            passTemplate->m_slots.emplace_back(slot);
            conn.m_attachmentRef.m_pass = node->GetName();
            conn.m_attachmentRef.m_attachment = "BufOutput";
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
                    printf("m_sizeSource ref to [%s] size 0x %x_%x_%x format %d\n", refAttachment->GetAttachmentId().GetCStr(),
                        refAttachment->m_descriptor.m_image.m_size.m_width,
                        refAttachment->m_descriptor.m_image.m_size.m_height,
                        refAttachment->m_descriptor.m_image.m_size.m_depth,
                        (int)refAttachment->m_descriptor.m_image.m_format);
                    outDesc.m_imageDescriptor = refAttachment->m_descriptor.m_image;
                }
            }

            req.m_imageAttachmentOverrides.emplace_back(outDesc);

            printf("create pass [%s] m_imageAttachmentOverrides m_name [%s]\n",
                name.GetCStr(), node->GetOutputBinding(0).GetAttachment()->m_name.GetCStr());
            
            Ptr<Pass> add = PassSystemInterface::Get()->CreatePassFromRequest(&req);
            return add;
        }
#endif
    }   // namespace RPI
}   // namespace AZ
