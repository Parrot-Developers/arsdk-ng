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

#ifndef _ARSDK_TEST_PROTOC_H_
#define _ARSDK_TEST_PROTOC_H_

#define DEVICE_NAME "UNITEST_DEVICE"
#define TEST_CONNECT_TIMEOUT 2000

struct test_dev;
struct test_ctrl;

/* DEVICE PART */
void test_create_dev(struct pomp_loop *loop, struct test_dev **dev);
void test_delete_dev(struct test_dev *dev);

/* CONTROLLER PART */
void test_create_ctrl(struct pomp_loop *loop, struct test_ctrl **ctrl);
void test_delete_ctrl(struct test_ctrl *ctrl);
struct arsdk_cmd_itf *test_ctrl_get_itf(struct test_ctrl *ctrl);

void test_dev_recv_cmd(const struct arsdk_cmd *cmd);
void test_start_send_msgs(struct test_ctrl *ctrl);

void test_connected(void);
void test_end(void);

#endif /* !_ARSDK_TEST_PROTOC_H_ */
