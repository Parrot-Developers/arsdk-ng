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

#ifndef _ARSDK_PUD_ITF_PRIV_H_
#define _ARSDK_PUD_ITF_PRIV_H_

/**
 * Create a pud interface.
 * @param dev_info : device information.
 * @param ftp_itf : ftp interface.
 * @param ret_itf : will receive the pud interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_pud_itf_new(struct arsdk_device_info *dev_info,
		struct arsdk_ftp_itf *ftp_itf,
		struct arsdk_pud_itf **ret_itf);

/**
 * Destroy pud interface.
 * @param itf : pud interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_pud_itf_destroy(struct arsdk_pud_itf *itf);

/**
 * Stop pud interface.
 * @param itf : pud interface.
 * @return 0 in case of success, negative errno value in case of error.
 */
int arsdk_pud_itf_stop(struct arsdk_pud_itf *itf);

#endif /* !_ARSDK_PUD_ITF_PRIV_H_ */
