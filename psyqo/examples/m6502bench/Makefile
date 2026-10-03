TARGET = m6502bench
TYPE = ps-exe

SRCS = \
m6502bench.cpp \
m6502jit.cpp \

CPPFLAGS += -I../../..
CXXFLAGS = -std=c++20
CPPFLAGS += -O3 $(EXTRA)

# The benchmark is Klaus Dormann's 6502 functional test, which is GPL-3 and so
# is fetched at build time rather than kept in this tree.
FUNCTEST_URL = https://raw.githubusercontent.com/Klaus2m5/6502_65C02_functional_tests/master/bin_files/6502_functional_test.bin

include ../../psyqo.mk

6502_functional_test.bin:
	curl -sSLf -o $@ $(FUNCTEST_URL)

functest.h: 6502_functional_test.bin
	xxd -i -n c_functest $< | sed 's/^unsigned char/static const unsigned char/; /_len =/d' > $@

m6502bench.o m6502bench.dep: functest.h
