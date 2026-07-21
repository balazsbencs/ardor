ARDOR_PEDAL_VERSION = 1.0
ARDOR_PEDAL_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../..
ARDOR_PEDAL_SITE_METHOD = local
ARDOR_PEDAL_DEPENDENCIES = alsa-lib
# BUILD_SHARED_LIBS=OFF: Buildroot defaults cmake packages to shared libs,
# which builds liblvgl.so that never gets installed to the target.
ARDOR_PEDAL_CONF_OPTS = -DCMAKE_BUILD_TYPE=Release -DARDOR_UI_BACKEND=fbdev -DBUILD_SHARED_LIBS=OFF

define ARDOR_PEDAL_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-pedal/S99ardor-pedal \
		$(TARGET_DIR)/etc/init.d/S99ardor-pedal
endef

define ARDOR_PEDAL_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/pedal-poc $(TARGET_DIR)/usr/bin/ardor-pedal
	$(INSTALL) -D -m 0644 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/board/ardor-pedal/codec-zero.state \
		$(TARGET_DIR)/etc/ardor-codec-zero.state
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/opt/ardor-pedal
endef

$(eval $(cmake-package))
