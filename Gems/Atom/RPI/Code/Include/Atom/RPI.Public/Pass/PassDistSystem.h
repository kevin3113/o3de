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
                m_readItr++;
                pthread_mutex_unlock(&m_mutex);
                return data;
            }
            void V(void *data)
            {
                pthread_mutex_lock(&m_mutex);
                m_dataQue[m_writeItr % MAX_QUE_LEN] = data;
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

            void ShowConnections(Ptr<Pass> &pass);

            Ptr<Pass> CreateDistPass(Name name, Ptr<Pass> &modify);

            Ptr<Pass> CreateFullscreenShadowPrePass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowDistPrePass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowAfterPass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowDistAfterPass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowDistAfterOutPass(Name name, Ptr<Pass> node);

            Ptr<Pass> CreateFullscreenShadowDistPass(Name name, Ptr<Pass>prePass, Ptr<Pass> node);

            uint32_t CreateFullscreenShadowDistPrePassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node);

            uint32_t CreateFullscreenShadowDistPassMsg(char *buf, uint32_t len, Name name, Ptr<Pass>prePass, Ptr<Pass> node);

            uint32_t CreateFullscreenShadowDistAfterPassMsg(char *buf, uint32_t len, Name name, Ptr<Pass> node);

            uint32_t ParsePassAttrsMsg(void *passMsgStart, PassSlotList &slots, PassConnectionList &conns,
                PassImageAttachmentDescList &imgs, PassBufferAttachmentDescList &bufs);

            Ptr<Pass> PassCreateFromTemplateMsg(char *buf, uint32_t len);

            Ptr<Pass> PassCreateFromRequestMsg(char *buf, uint32_t len);

            uint32_t ParsePassCreateMsg(char *buf, uint32_t len, Ptr<ParentPass> &root);

            void ProcessPassB(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses);

            void ProcessFullscreenShadow(Ptr<Pass> pass, AZStd::unordered_map<Name, Ptr<Pass>> subPasses);

            void CloneFullscreenShadow(Ptr<Pass> pass);

            bool IsDistProcessed(Name name);
            
            void AddDistNode(struct PassDistNode &node);

            void UpdateDistPasses(void);

            bool IsActive(void);

            void Active(void);

            void Inactive(void);

            void ModifyDistPassGraph(Ptr<ParentPass> &root);

            void BuildDistPassGraph(Ptr<ParentPass> &root);

            int Connect(void) override;

            int Send(void) override;

            int Recv(void) override;

            void *DequePassMsg(bool noWait = false) override;

            void EnquePassMsg(void *data) override;

            void ProcessDistChanges(Ptr<ParentPass> &root) override;

            RenderPipelinePtr CreateDistPipeline(int device, const RenderPipelineDescriptor &desc) override;

            RenderPipelinePtr GetDistPipeline(int device) override;

            void SetActivePipeline(Name name) override;

            Name GetActivePipeline(void) override;

            void FrameEnd(void) override;

            void Enable(void) override;

            void Disable(void) override;

            bool IsEnable(void) override;

        private:
            // List of pass node to be add
            AZStd::unordered_map<Name, PassDistNode> m_distChangeList;

            AZStd::vector<AZStd::shared_ptr<PassTemplate>> m_templates;

            AZStd::unordered_map<int, RenderPipelinePtr> m_devPipelines;

            Name m_activePipeline;

            bool m_state = false;

            Name m_modify;

            int m_sfd = -1;

            bool m_isServer = false;

            Name m_commPath;

            WaitQueue m_msgQue;

        };
    }   // namespace RPI
}   // namespace AZ
