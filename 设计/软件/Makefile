.PHONY: all build-bpf build-go build-frontend clean run demo

CLANG  ?= clang
CFLAGS := -O2 -g -Wall -target bpf -D__TARGET_ARCH_x86
BPFDIR := bpf

BPF_OBJS := $(BPFDIR)/loop_monitor.bpf.o $(BPFDIR)/serial_monitor.bpf.o $(BPFDIR)/sched_monitor.bpf.o

all: build-bpf build-go

# --- BPF compilation ---
$(BPFDIR)/%.bpf.o: $(BPFDIR)/%.bpf.c $(BPFDIR)/common.h
	$(CLANG) $(CFLAGS) -I/usr/include -I./$(BPFDIR) -c $< -o $@
	@echo "[OK] $@"

build-bpf: $(BPF_OBJS)

# --- Go compilation ---
build-go:
	@mkdir -p bin
	cd cmd/collector && CGO_ENABLED=0 go build -buildvcs=false -o ../../bin/collector . 2>&1 || echo "Go build skipped (need cilium/ebpf)"
	@if [ -f bin/collector ]; then echo "[OK] bin/collector"; fi

# --- Run ---
run: build-bpf
	@echo "[*] Starting Robot Safety Monitor..."
	BPF_DIR=$(BPFDIR) sudo -E ./bin/collector

# --- Demo control node ---
demo:
	@echo "[*] Starting mock robot controller (100Hz, fault injection)..."
	python3 ros2/demo_control.py --fault 5

# --- Clean ---
clean:
	rm -f $(BPF_OBJS)
	rm -rf bin/

# --- Verify BPF ---
verify:
	@for obj in $(BPF_OBJS); do \
		echo "=== $$obj ==="; \
		llvm-objdump-18 -h $$obj 2>/dev/null | grep -E "kprobe|tracepoint|license|maps" || true; \
	done
