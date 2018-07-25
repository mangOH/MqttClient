TARGETS := wp85 wp750x wp76xx wp77xx

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	$(eval MANGOH_WP_CHIPSET_9X15 := $(if $(filter wp85 wp750x, $@), 1, 0))
	$(eval MANGOH_WP_CHIPSET_9X07 := $(if $(filter wp76xx wp77xx, $@), 1, 0))
	MANGOH_WP_CHIPSET_9X15=$(MANGOH_WP_CHIPSET_9X15) \
	MANGOH_WP_CHIPSET_9X07=$(MANGOH_WP_CHIPSET_9X07) \
	mkapp -v -t $@ mqttClient.adef

clean:
	rm -rf _build_* *.*.update
