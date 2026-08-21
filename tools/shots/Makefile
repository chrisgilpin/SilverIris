# Visual harness. Links the same native objects as test_stage.
ROOT := $(abspath ../..)
BUILD := $(ROOT)/build/native
NATIVE := $(ROOT)/native

.PHONY: all shot run clean

all: shot

shot:
	$(MAKE) -C $(NATIVE) overlay $(BUILD)/src/glue/port_api.o
	$(MAKE) -C $(NATIVE) $(BUILD)/test_stage
	$(MAKE) -C $(NATIVE) shot-bin

run: shot
	./run.sh

clean:
	rm -f $(BUILD)/shot
