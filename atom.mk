
LOCAL_PATH := $(call my-dir)

###############################################################################
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libarsdk

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/libarsdk/include

# Public API headers - top level headers first
# This header list is currently used to generate a python binding
LOCAL_EXPORT_CUSTOM_VARIABLES := LIBARSDK_HEADERS=$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_desc.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_cmd_itf.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_mngr.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_backend.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_backend_net.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_backend_mux.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_publisher_avahi.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_publisher_net.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_publisher_mux.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_peer.h;

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/libarsdk/src

LOCAL_CFLAGS := -DARSDK_API_EXPORTS -fvisibility=hidden

LOCAL_SRC_FILES := \
	libarsdk/src/arsdk_backend.c \
	libarsdk/src/cmd_itf/arsdk_cmd_itf.c \
	libarsdk/src/cmd_itf/arsdk_cmd_itf1.c \
	libarsdk/src/cmd_itf/arsdk_cmd_itf2.c \
	libarsdk/src/cmd_itf/arsdk_cmd_itf3.c \
	libarsdk/src/arsdk_decoder.c \
	libarsdk/src/arsdk_mngr.c \
	libarsdk/src/arsdk_encoder.c \
	libarsdk/src/arsdk_log.c \
	libarsdk/src/arsdk_peer.c \
	libarsdk/src/arsdk_transport.c

LOCAL_SRC_FILES += \
	libarsdk/src/net/arsdk_backend_net.c \
	libarsdk/src/net/arsdk_publisher_avahi.c \
	libarsdk/src/net/arsdk_publisher_net.c \
	libarsdk/src/net/arsdk_transport_net.c

LOCAL_SRC_FILES += \
	libarsdk/src/mux/arsdk_backend_mux.c \
	libarsdk/src/mux/arsdk_publisher_mux.c \
	libarsdk/src/mux/arsdk_transport_mux.c

LOCAL_LIBRARIES += libpomp \
	json \
	libfutils

LOCAL_WHOLE_STATIC_LIBRARIES += libarsdkgen

LOCAL_CONDITIONAL_LIBRARIES += \
	OPTIONAL:libulog \
	OPTIONAL:libmux

ifeq ("$(TARGET_OS)","windows")
  LOCAL_LDLIBS += -lws2_32
endif

include $(BUILD_LIBRARY)

###############################################################################
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libarsdkgen

LOCAL_EXPORT_C_INCLUDES := \
	$(call local-get-build-dir)/gen

LOCAL_DEPENDS_HEADERS := libarsdk

LOCAL_LIBRARIES := libpomp

LOCAL_CFLAGS := -DARSDK_API_EXPORTS -fvisibility=hidden

LOCAL_GENERATED_SRC_FILES := \
	gen/arsdk_cmd_desc.c \
	gen/arsdk_cmd_dec.c \
	gen/arsdk_cmd_enc.c

LOCAL_CUSTOM_MACROS := \
	arsdkgen-macro:$(LOCAL_PATH)/tools/libarsdkgen.py,$(call local-get-build-dir)/gen

include $(BUILD_LIBRARY)


###############################################################################
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := libarsdkctrl

LOCAL_EXPORT_C_INCLUDES := \
	$(LOCAL_PATH)/libarsdkctrl/include

# Public API headers - top level headers first
# This header list is currently used to generate a python binding
LOCAL_EXPORT_CUSTOM_VARIABLES := LIBARSDKCTRL_HEADERS=$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdkctrl.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_ctrl.h:$\
	$(LOCAL_PATH)/libarsdk/include/arsdk/arsdk_backend.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdkctrl_backend.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdkctrl_backend_net.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdkctrl_backend_mux.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_discovery_avahi.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_discovery_net.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_discovery_mux.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_ftp_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_media_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_updater_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_blackbox_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_crashml_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_pud_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_ephemeris_itf.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/arsdk_device.h:$\
	$(LOCAL_PATH)/libarsdkctrl/include/arsdkctrl/internal/arsdk_discovery_internal.h;

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/libarsdk/include \
	$(LOCAL_PATH)/libarsdk/src \
	$(LOCAL_PATH)/libarsdkctrl/src

LOCAL_CFLAGS := -DARSDK_API_EXPORTS -fvisibility=hidden

LOCAL_SRC_FILES := \
	libarsdkctrl/src/arsdkctrl_log.c \
	libarsdkctrl/src/arsdk_discovery.c \
	libarsdkctrl/src/arsdk_ctrl.c \
	libarsdkctrl/src/arsdkctrl_backend.c \
	libarsdkctrl/src/arsdk_media_itf.c \
	libarsdkctrl/src/arsdk_updater_itf.c \
	libarsdkctrl/src/arsdk_blackbox_itf.c \
	libarsdkctrl/src/arsdk_crashml_itf.c \
	libarsdkctrl/src/arsdk_flight_log_itf.c \
	libarsdkctrl/src/arsdk_pud_itf.c \
	libarsdkctrl/src/arsdk_ephemeris_itf.c \
	libarsdkctrl/src/arsdk_md5.c \
	libarsdkctrl/src/updater/arsdk_updater_transport.c \
	libarsdkctrl/src/updater/arsdk_updater_transport_ftp.c \
	libarsdkctrl/src/updater/arsdk_updater_transport_mux.c

ifeq ($(CONFIG_ALCHEMY_BUILD_LIBMUX_LEGACY_CONFIG),y)
LOCAL_SRC_FILES += \
	libarsdkctrl/src/arsdk_ftp_itf_mux_legacy.c \
	libarsdkctrl/src/arsdk_device_mux_legacy.c
LOCAL_CFLAGS += -DLIBMUX_LEGACY=1
else
LOCAL_SRC_FILES += \
	libarsdkctrl/src/arsdk_ftp_itf.c \
	libarsdkctrl/src/arsdk_device.c
endif

LOCAL_SRC_FILES += \
	libarsdkctrl/src/net/arsdkctrl_backend_net.c \
	libarsdkctrl/src/net/arsdk_avahi_loop.c \
	libarsdkctrl/src/net/arsdk_discovery_avahi.c \
	libarsdkctrl/src/net/arsdk_discovery_net.c

LOCAL_SRC_FILES += \
	libarsdkctrl/src/mux/arsdkctrl_backend_mux.c \
	libarsdkctrl/src/mux/arsdk_discovery_mux.c

LOCAL_SRC_FILES += \
	libarsdkctrl/src/ftp/arsdk_ftp.c \
	libarsdkctrl/src/ftp/arsdk_ftp_conn.c \
	libarsdkctrl/src/ftp/arsdk_ftp_seq.c \
	libarsdkctrl/src/ftp/arsdk_ftp_cmd.c

LOCAL_LIBRARIES += libarsdk \
	libulog \
	libpomp \
	json \
	libfutils \
	libmux

LOCAL_CONDITIONAL_LIBRARIES += \
	OPTIONAL:avahi-client \
	OPTIONAL:libpuf

ifeq ("$(TARGET_OS)","windows")
  LOCAL_LDLIBS += -lws2_32
endif

include $(BUILD_LIBRARY)

###############################################################################
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := arsdk-ng-controller
LOCAL_SRC_FILES := examples/controller.c
LOCAL_LIBRARIES := libpomp libulog libmux libarsdk libarsdkctrl
LOCAL_CONDITIONAL_LIBRARIES := \
	OPTIONAL:libpuf
include $(BUILD_EXECUTABLE)

###############################################################################
###############################################################################

include $(CLEAR_VARS)

LOCAL_MODULE := arsdk-ng-device
LOCAL_SRC_FILES := examples/device.c
LOCAL_LIBRARIES := libpomp libulog libmux libarsdk
include $(BUILD_EXECUTABLE)

###############################################################################
###############################################################################

ifdef TARGET_TEST

include $(CLEAR_VARS)
LOCAL_MODULE := tst-arsdk
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/src \
	$(LOCAL_PATH)/tests

LIBARSDKCTRL_GEN_DIR := $(call local-get-build-dir)/gen

LOCAL_EXPORT_C_INCLUDES := \
	$(call local-get-build-dir)/gen

LOCAL_GENERATED_SRC_FILES := gen/arsdk_test_protoc_gen.c

LOCAL_SRC_FILES := \
	tests/arsdk_test.c \
	tests/env/arsdk_test_env.c \
	tests/env/arsdk_test_env_dev.c \
	tests/env/arsdk_test_env_mux_tip.c \
	tests/env/arsdk_test_env_ctrl.c \
	tests/arsdk_test_cmd_itf.c \
	tests/arsdk_test_enc_dec.c

LOCAL_LIBRARIES := libarsdk \
		   libarsdkctrl \
		   libpomp \
		   avahi-client \
		   libmux \
		   libcunit

LOCAL_CUSTOM_MACROS := \
	arsdkgen-macro:$(LOCAL_PATH)/tools/arsdktestgen.py,$(call local-get-build-dir)/gen

LOCAL_CONDITIONAL_LIBRARIES := OPTIONAL:libulog

ifeq ("$(TARGET_OS)","windows")
  LOCAL_LDLIBS += -lws2_32
endif

include $(BUILD_EXECUTABLE)

endif