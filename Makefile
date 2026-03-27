C_SOURCES = $(shell find . -name "*.c")

build:
	gcc ${C_SOURCES} -lSDL2main -lSDL2 -lSDL2_ttf -lm -I./include
	python3 assembler/assembler.py programs/bios programs/bin/bios.bin -bios
	python3 assembler/assembler.py programs/kernel programs/bin/kernel.bin
