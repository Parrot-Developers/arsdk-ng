/**
 * Copyright (c) 2019 Parrot Drones SAS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of the Parrot Drones SAS Company nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE PARROT DRONES SAS COMPANY BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ARSDK_TRANSPORT_IDS_H_
#define _ARSDK_TRANSPORT_IDS_H_

#define ARSDK_TRANSPORT_ID_MAX                    256

#define ARSDK_TRANSPORT_ID_INVALID                255

#define ARSDK_TRANSPORT_ID_PING                   0
#define ARSDK_TRANSPORT_ID_PONG                   1

#define ARSDK_TRANSPORT_ID_CMD_MIN                10

#define ARSDK_TRANSPORT_ID_C2D_CMD_NOACK          10
#define ARSDK_TRANSPORT_ID_C2D_CMD_WITHACK        11
#define ARSDK_TRANSPORT_ID_C2D_CMD_HIGHPRIO       12

#define ARSDK_TRANSPORT_ID_D2C_CMD_NOACK          127
#define ARSDK_TRANSPORT_ID_D2C_CMD_WITHACK        126
#define ARSDK_TRANSPORT_ID_D2C_CMD_LOWPRIO        125

#define ARSDK_TRANSPORT_ID_ACKOFF \
	(ARSDK_TRANSPORT_ID_MAX / 2)

#endif /* _ARSDK_TRANSPORT_IDS_H_ */
