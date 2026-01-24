# SPDX-License-Identifier: GPL-2.0
HOSTRUSTFLAGS += -g

PROGS := dma-slave

all: $(PROGS)

dma-slave: dma-slave.rs
	$(RUSTC) $(RUSTFLAGS) $< -o $@

clean:
	rm -fr $(PROGS)
