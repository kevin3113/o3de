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
        enum class DistTlvType : uint32_t {
            Pass = 0,
            Count
        };

        enum class PassCreateType : uint32_t {
            Template = 0,
            Request,
            Count
        };

        struct MsgHead {
            uint32_t msgLen;
            uint32_t taskId;
        };

        struct MsgTlvInfo {
            uint32_t type;
            uint32_t len;
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

        struct MsgPass {
            uint32_t createType;
            uint32_t bodyLen;
            uint16_t slotCnt;
            uint16_t connCnt;
            uint16_t imgCnt;
            uint16_t bufCnt;
            char pipeline[PASS_NAME_MAX];
            char name[PASS_NAME_MAX];
            char passTemp[PASS_NAME_MAX];
            char passClass[PASS_NAME_MAX];
            void CalcBodyLen(void)
            {
                bodyLen = sizeof(MsgPassSlot) * slotCnt
                    + sizeof(MsgPassConn) * connCnt
                    + sizeof(MsgPassAttImg) * imgCnt
                    + sizeof(MsgPassAttBuf) * bufCnt;
            }
        };
    }
}