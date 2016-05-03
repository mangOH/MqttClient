TARGETS := wp85

export MANGOH_ROOT=$(LEGATO_ROOT)/../mangOH

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	export TARGET=$@ ; \
	mkapp -v -t $@ \
          mqttClient.adef

clean:
	rm -rf _build_* *.wp85 *.wp85.update
