include $(sort $(wildcard $(BR2_EXTERNAL_ARDOR_PEDAL_PATH)/package/*/*.mk))

# Buildroot 2025.02.15 applies its 32-bit ARM NEON check to AArch64 too,
# disabling the SIMD backend there. AArch64 has NEON without -mfpu=neon.
# Keep FFTW execution single-threaded, including combined-thread builds.
ifeq ($(BR2_PACKAGE_ARDOR_PEDAL),y)
FFTW_SINGLE_CFLAGS += -O3
FFTW_SINGLE_CONF_OPTS := $(filter-out --enable-threads --enable-openmp --with-combined-threads --without-combined-threads,$(FFTW_SINGLE_CONF_OPTS)) \
	--disable-threads --disable-openmp --without-combined-threads
ifeq ($(BR2_aarch64),y)
FFTW_SINGLE_CONF_OPTS := $(filter-out --disable-neon,$(FFTW_SINGLE_CONF_OPTS)) --enable-neon
endif
endif
