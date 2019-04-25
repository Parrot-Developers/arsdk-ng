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

#include "arsdk_priv.h"
#include "arsdk_net.h"
#include "arsdk_net_log.h"
#include <arsdk/arsdk_publisher_avahi.h>
#include <json-c/json.h>

#define AVAHI_SERVICES_DIR "/etc/avahi/services"
#define AVAHI_SERVICE_FILE "arsdk-ng.service"
#define AVAHI_SERVICE_PATH AVAHI_SERVICES_DIR "/" AVAHI_SERVICE_FILE

#define AVAHI_SERVICE_TYPE_FMT     "_arsdk-%04x._udp"
#define AVAHI_SERVICE_TXTDATA_FMT  "{\"device_id\":\"%s\"}"

/** */
#define AVAHI_SERVICE_XML_FMT \
	"<?xml version=\"1.0\" standalone='no'?><!--*-nxml-*-->\n" \
	"<!DOCTYPE service-group SYSTEM \"avahi-service.dtd\">\n" \
	"<service-group>\n" \
	"  <name replace-wildcards=\"yes\">%s</name>\n" \
	"  <service>\n" \
	"    <type>" AVAHI_SERVICE_TYPE_FMT "</type>\n" \
	"    <port>%u</port>\n" \
	"    <txt-record>" AVAHI_SERVICE_TXTDATA_FMT "</txt-record>\n" \
	"  </service>" \
	"</service-group>\n"

/** */
struct arsdk_publisher_avahi {
	struct arsdk_backend_net  *backend;
};

/**
 */
static int write_avahi_xml(const char *filepath,
		const struct arsdk_publisher_avahi_cfg *cfg)
{
	int res = 0;
	FILE *xmlfile = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->base.name != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg->base.id != NULL, -EINVAL);

	/* Create xml file
	 * TODO: make it a failure once a proper configuration allows to
	 * disable completely avahi publication (with or without the daemon) */
	xmlfile = fopen(filepath, "w");
	if (xmlfile == NULL) {
		res = -errno;
		ARSDK_LOGW("Failed to create '%s': err=%d(%s)", filepath,
				-res, strerror(-res));
		return 0;
	}

	/* Format it */
	fprintf(xmlfile, AVAHI_SERVICE_XML_FMT,
			cfg->base.name,
			cfg->base.type,
			cfg->port,
			cfg->base.id);
	fclose(xmlfile);
	return 0;
}

/**
 */
static int remove_avahi_xml(const char *filepath)
{
	int res = 0;
	if (unlink(filepath) == 0 || errno == ENOENT)
		return 0;
	res = -errno;
	ARSDK_LOGE("Failed to remove '%s': err=%d(%s)", filepath,
			errno, strerror(errno));
	return res;
}

/**
 */
int arsdk_publisher_avahi_new(
		struct arsdk_backend_net *backend,
		struct pomp_loop *loop,
		struct arsdk_publisher_avahi **ret_obj)
{
	struct arsdk_publisher_avahi *self = NULL;

	ARSDK_RETURN_ERR_IF_FAILED(ret_obj != NULL, -EINVAL);
	*ret_obj = NULL;
	ARSDK_RETURN_ERR_IF_FAILED(backend != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(loop != NULL, -EINVAL);

	/* Allocate structure */
	self = calloc(1, sizeof(*self));
	if (self == NULL)
		return -ENOMEM;

	/* Initialize structure */
	self->backend = backend;

	*ret_obj = self;
	return 0;
}

/**
 */
int arsdk_publisher_avahi_destroy(struct arsdk_publisher_avahi *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	free(self);

	return 0;
}

/**
 */
int arsdk_publisher_avahi_start(
		struct arsdk_publisher_avahi *self,
		const struct arsdk_publisher_avahi_cfg *cfg)
{
	int res = 0;

	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);
	ARSDK_RETURN_ERR_IF_FAILED(cfg != NULL, -EINVAL);

	res = write_avahi_xml(AVAHI_SERVICE_PATH, cfg);

	return res;
}

/**
 */
int arsdk_publisher_avahi_stop(struct arsdk_publisher_avahi *self)
{
	ARSDK_RETURN_ERR_IF_FAILED(self != NULL, -EINVAL);

	remove_avahi_xml(AVAHI_SERVICE_PATH);

	return 0;
}
