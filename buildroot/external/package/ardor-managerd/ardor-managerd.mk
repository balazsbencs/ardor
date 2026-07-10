ARDOR_MANAGERD_VERSION = 1.0
ARDOR_MANAGERD_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_MANAGERD_SITE_METHOD = local
ARDOR_MANAGERD_SUBDIR = services/managerd
ARDOR_MANAGERD_GOMOD = ardor.local/managerd
ARDOR_MANAGERD_BUILD_TARGETS = ./cmd/ardor-managerd
ARDOR_MANAGERD_INSTALL_BINS = ardor-managerd
ARDOR_MANAGERD_LICENSE = Proprietary

define ARDOR_MANAGERD_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-managerd/S98ardor-managerd \
		$(TARGET_DIR)/etc/init.d/S98ardor-managerd
endef

$(eval $(golang-package))
