/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <cstdint>

#define PASS_NAME_MAX 128

namespace AZ
{
    namespace RPI
    {
        enum class DistMsgType : uint32_t {
            PassGraph = 0,
            PassData,
            Debug,
            Count
        };

        enum class PassCreateType : uint32_t {
            Template = 0,
            Request,
            Count
        };

        struct MsgHead {
            uint32_t msgLen;
            uint32_t msgType;
            uint64_t ticket;
        };

        struct MsgPassSlot {
            uint32_t slotType;
            uint32_t pad;
            char slotName[PASS_NAME_MAX];
        };

        struct MsgPassConn {
            char localSlot[PASS_NAME_MAX];
            char refPassName[PASS_NAME_MAX];
            char refAttName[PASS_NAME_MAX];
        };

        struct MsgPassAttImg {
            uint32_t bindFlags;
            uint32_t width;
            uint32_t height;
            uint32_t depth;
            uint32_t arraySize;
            uint32_t format;
            char name[PASS_NAME_MAX];
        };

        struct MsgPassAttBuf {
            uint64_t size;
            uint64_t align;
            uint32_t bindFlags;
            uint32_t pad;
            char name[PASS_NAME_MAX];
        };

        struct MsgPassCommInfo
        {
            uint16_t isCommPass = false;
            uint16_t commOper = 0;
            uint16_t splitCnt = 0;
            uint16_t splitIdx = 0;
        };

        struct MsgPassGraph {
            uint32_t passLen;
            uint32_t createType;
            uint8_t slotCnt;
            uint8_t connCnt;
            uint8_t imgCnt;
            uint8_t bufCnt;
            uint8_t commCnt;
            uint8_t reserv[3];
            char pipeline[PASS_NAME_MAX];
            char name[PASS_NAME_MAX];
            char passTemp[PASS_NAME_MAX];
            char passClass[PASS_NAME_MAX];
            void CalcBodyLen(void)
            {
                passLen = sizeof(MsgPassGraph)
                    + sizeof(MsgPassSlot) * slotCnt
                    + sizeof(MsgPassConn) * connCnt
                    + sizeof(MsgPassAttImg) * imgCnt
                    + sizeof(MsgPassAttBuf) * bufCnt
                    + sizeof(MsgPassCommInfo) * commCnt;
            }
        };

        struct MsgPassData {
            uint32_t dataLen;
            uint32_t nodeId;
        };

        struct MsgDebugInfo {
            uint32_t infoLen;
            uint32_t nodeId;
        };
    }
}