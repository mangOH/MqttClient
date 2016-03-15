CC=gcc
CFLAGS=-c -Wall -Iinc -Iinc/mqtt
LDFLAGS=-lpthread
SOURCES=src/mqttspooler.c src/MQTTClient.c src/mqtt/MQTTConnectClient.c \
src/mqtt/MQTTConnectServer.c src/mqtt/MQTTUnsubscribeClient.c src/MQTTLinux.c \
src/mqtt/MQTTUnsubscribeServer.c src/mqtt/MQTTSerializePublish.c src/mqtt/MQTTSubscribeClient.c \
src/mqtt/MQTTDeserializePublish.c src/mqtt/MQTTSubscribeServer.c src/mqtt/MQTTPacket.c \
src/json/swir_json.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=mqttspooler

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS) 

.c.o:
	$(CC) $(CFLAGS) $< -o $@
