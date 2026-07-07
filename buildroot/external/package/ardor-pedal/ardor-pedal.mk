ARDOR_PEDAL_VERSION = 1.0
ARDOR_PEDAL_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_PEDAL_SITE_METHOD = local
ARDOR_PEDAL_DEPENDENCIES =
ARDOR_PEDAL_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release

define ARDOR_PEDAL_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/S99ardor-pedal \
		$(TARGET_DIR)/etc/init.d/S99ardor-pedal
endef

define ARDOR_PEDAL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pedal-poc $(TARGET_DIR)/usr/bin/ardor-pedal
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal
endef

$(eval $(cmake-package))
