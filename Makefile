TARGETS := ar7 wp7 ar86 wp85 localhost

.PHONY: all $(TARGETS)
all: $(TARGETS)

$(TARGETS):
	export TARGET=$@ ; \
	mkapp -v -t $@ \
		  -i $(LEGATO_ROOT)/interfaces/dataConnectionService \
		  -i $(LEGATO_ROOT)/interfaces/modemServices \
		  -i mqttClientComp/inc \
		  -i mqttClientComp/inc/mqtt \
		  mqttClient.adef

clean:
	rm -rf _build_* *.ar7 *.wp7 *.ar86 *.wp85 *.localhost
