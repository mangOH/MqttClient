TARGETS := wp85 wp750x wp76xx

export MANGOH_ROOT=$(LEGATO_ROOT)/../mangOH

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	export TARGET=$@ ; \
	mkapp -v -t $@ \
          --interface-search=$(LEGATO_ROOT)/interfaces/modemServices \
          mqttClient.adef

clean:
	rm -rf _build_* *.*.update
