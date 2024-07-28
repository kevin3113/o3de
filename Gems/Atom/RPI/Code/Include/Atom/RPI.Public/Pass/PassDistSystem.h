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

#include <pthread.h>

namespace AZ
{
    namespace RHI
    {
        class FrameGraphBuilder;
    }

    namespace RPI
    {
        #define MAX_QUE_LEN 1024
        class WaitQueue {
        public:
            WaitQueue()
            {
                pthread_mutex_init(&m_mutex, NULL);
                pthread_cond_init(&m_cond, NULL);
            }
            void *P(bool noWait = false)
            {
                void *data;
                pthread_mutex_lock(&m_mutex);
                while (m_readItr >= m_writeItr) {
                    if (noWait)
                    {
                        pthread_mutex_unlock(&m_mutex);
                        return nullptr;
                    }
                    (void)pthread_cond_wait(&m_cond, &m_mutex);
                }   
                data = m_dataQue[m_readItr % MAX_QUE_LEN];
                //printf("P_oper buf %p read %ld/%ld que %p\n", data, m_readItr, m_writeItr, (void*)this);
                m_readItr++;
                pthread_mutex_unlock(&m_mutex);
                return data;
            }
            void V(void *data)
            {
                pthread_mutex_lock(&m_mutex);
                if (m_writeItr - m_writeItr > MAX_QUE_LEN / 8)
                {
                    printf("QUE_WARN: V_oper buf %p write %ld/%ld que %p\n", data, m_readItr, m_writeItr, (void*)this);
                }
                m_dataQue[m_writeItr % MAX_QUE_LEN] = data;
                //printf("V_oper buf %p write %ld/%ld que %p\n", data, m_readItr, m_writeItr, (void*)this);
                m_writeItr++;
                pthread_cond_signal(&m_cond);
                pthread_mutex_unlock(&m_mutex);
            }

        private:
            pthread_cond_t m_cond;
            pthread_mutex_t m_mutex;
            void *m_dataQue[MAX_QUE_LEN];
            uint64_t m_writeItr = 0;
            uint64_t m_readItr = 0;
        };

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

            void CommInit(bool isServer, const char *path);

            Ptr<Pass> GetBindingPass(ParentPass *pass, PassAttachmentBinding *connected);

            void ProcessSubPasses(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>>& subPasses);

            void ShowConnections(Ptr<Pass> &pass);

            void AddCommPassSlot(AZStd::shared_ptr<PassTemplate> &passTemplate, PassSlotType slotType, std::string suffix);

            AZStd::shared_ptr<PassTemplate> CreateCommPassTemplate(Name tempName, PassSlotType slotType, uint32_t count);

            Ptr<Pass> CreateFullscreenShadowPrePass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowAfterPass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowDistPass(Name name, Ptr<Pass>prePass, Ptr<Pass> node);

            Ptr<Pass> CreateDistImGui(Ptr<Pass> pass);

            Ptr<Pass> CreateDistCopyToSwapChain(Ptr<Pass> pass);

            uint32_t CreateFullscreenShadowDistPrePassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node);

            uint32_t CreateFullscreenShadowDistPassMsg(char *buf, uint32_t len, Name name, Name prePassName, Ptr<Pass> node);

            uint32_t CreateFullscreenShadowDistAfterPassMsg(char *buf, uint32_t len, Name name, Name prePassName);

            uint32_t ParsePassAttrsMsg(void *passMsgStart, PassSlotList &slots, PassConnectionList &conns,
                PassImageAttachmentDescList &imgs, PassBufferAttachmentDescList &bufs, MsgPassCommInfo &commInfo);

            Ptr<Pass> PassCreateFromTemplateMsg(char *buf, uint32_t len);

            Ptr<Pass> PassCreateFromRequestMsg(char *buf, uint32_t len);

            uint32_t ParsePassCreateMsg(char *buf, uint32_t len, Ptr<ParentPass> &root);

            void ModifyFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> &subPasses);

            void ProcessFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses);

            void CloneFullscreenShadow(Ptr<Pass> pass);

            bool IsDistProcessed(Name name);
            
            void AddDistNode(struct PassDistNode &node);

            void UpdateDistPasses(void);

            void ModifyDistPassGraph(Ptr<ParentPass> &root);

            void BuildDistPassGraph(Ptr<ParentPass> &root);

            int Connect(int sfd) override;

            int Send(int sfd) override;

            int SendQue(int sfd) override;

            int Recv(int sfd) override;

            void *DequePassMsg(bool noWait = false) override;

            void EnquePassMsg(void *data) override;

            void *DequeInputDataMsg(uint32_t queId, bool noWait = false) override;

            void EnqueInputDataMsg(uint32_t queId, void *data) override;

            void *DequeOutputDataMsg(uint32_t queId, bool noWait = false) override;

            void EnqueOutputDataMsg(uint32_t queId, void *data) override;

            int SendData(void *data[], uint32_t len[], uint32_t count, SplitInfo &splitInfo) override;

            int RecvData(void *data[], uint32_t len[], uint32_t size, uint32_t *count, SplitInfo &splitInfo) override;

            void ProcessDistChanges(Ptr<ParentPass> &root) override;

            RenderPipelinePtr CreateDistPipeline(const RenderPipelinePtr main) override;

            RenderPipelinePtr GetDistPipeline(void) override;

            void SetActivePipeline(Name name) override;

            Name GetActivePipeline(void) override;

            void Enable(void) override;

            void Disable(void) override;

            bool IsEnable(void) override;

            bool IsActive(void) override;

        private:
            // List of pass node to be add
            AZStd::unordered_map<Name, PassDistNode> m_distChangeList;

            AZStd::vector<AZStd::shared_ptr<PassTemplate>> m_templates;

            AZStd::vector<AZStd::shared_ptr<PassRequest>> m_requests;

            RenderPipelinePtr m_distPipeline = nullptr;

            Name m_activePipeline;

            bool m_state = false;

            bool m_hasDistPass = false;

            bool m_displayEnable = false;

            bool m_displayImGui = false;

            Name m_modify;

            bool m_isServer = false;

            Name m_commPath;

            int m_sfd = -1;

            // dist node only
            SplitInfo m_splitInfo;

            uint64_t m_ticket = 0;

            void *m_sendFailMsg = nullptr;

            WaitQueue m_passMsgQue;

            AZStd::vector<WaitQueue> m_dataInputQues;

            AZStd::vector<WaitQueue> m_dataOutputQues;

        };
    }   // namespace RPI
}   // namespace AZ
