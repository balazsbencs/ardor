ARDOR_MANAGERD_VERSION = 1.0
ARDOR_MANAGERD_SITE = $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../../services/managerd
ARDOR_MANAGERD_SITE_METHOD = local
ARDOR_MANAGERD_GOMOD = ardor.local/managerd
ARDOR_MANAGERD_BUILD_TARGETS = cmd/ardor-managerd cmd/ardor-updater
ARDOR_MANAGERD_INSTALL_BINS = ardor-managerd ardor-updater
ARDOR_MANAGERD_LDFLAGS = \
	-X ardor.local/managerd/internal/buildinfo.Version=$(if $(ARDOR_RELEASE_VERSION),$(ARDOR_RELEASE_VERSION),0.0.0) \
	-X ardor.local/managerd/internal/buildinfo.Commit=$(if $(ARDOR_RELEASE_COMMIT),$(ARDOR_RELEASE_COMMIT),unknown)
ARDOR_MANAGERD_LICENSE = Proprietary

# Local Buildroot packages are rsynced without repository siblings. Stage the
# shared protocol module inside the package build directory, then vendor all Go
# dependencies after host-go is available so the target build remains offline.
define ARDOR_MANAGERD_VENDOR_MODULES
	mkdir -p $(@D)/protocol/cloud
	cp -a $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/../../protocol/cloud/. $(@D)/protocol/cloud/
	$(SED) 's,../../protocol/cloud,./protocol/cloud,' $(@D)/go.mod
	cd $(@D) && $(HOST_GO_COMMON_ENV) GOPROXY=direct $(GO_BIN) mod vendor -modcacherw
endef
ARDOR_MANAGERD_PRE_BUILD_HOOKS += ARDOR_MANAGERD_VENDOR_MODULES

define ARDOR_MANAGERD_INSTALL_INIT_SYSV
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-managerd/S97ardor-update-recovery \
		$(TARGET_DIR)/etc/init.d/S97ardor-update-recovery
	$(INSTALL) -D -m 0755 $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/ardor-managerd/S98ardor-managerd \
		$(TARGET_DIR)/etc/init.d/S98ardor-managerd
endef

$(eval $(golang-package))
