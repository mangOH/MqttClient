TARGETS := wp85 wp750x wp76xx

export MANGOH_ROOT=$(shell pwd)/../..

.PHONY: all $(TARGETS)
all: $(TARGETS) paho

$(TARGETS):
	export TARGET=$@ ; \
	mkapp -v -t $@ \
        --interface-search=$(LEGATO_ROOT)/interfaces \
          mqttClient.adef

paho:
	CC=$(WP85_TOOLCHAIN_DIR)/arm-poky-linux-gnueabi-gcc $(MAKE) -C paho.mqtt.c

clean:
	rm -rf _build_* *.*.update
