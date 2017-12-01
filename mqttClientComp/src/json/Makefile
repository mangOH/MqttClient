CFLAGS+=-c -Wall -I../../inc
LDFLAGS+=
SOURCES=swir_json.c test_swir_json.c

OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=test_swir_json

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJECTS)
	rm -f $(EXECUTABLE)
