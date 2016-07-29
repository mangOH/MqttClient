TARGETS := wp85

export MANGOH_ROOT=$(LEGATO_ROOT)/../mangOH

.PHONY: all $(TARGETS)
all: $(TARGETS) paho

$(TARGETS):
	export TARGET=$@ ; \
	mkapp -v -t $@ \
          mqttClient.adef

paho:
	$(MAKE) -C paho.mqtt.c

clean:
	rm -rf _build_* *.wp85 *.wp85.update
