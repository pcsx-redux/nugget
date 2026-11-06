BUILD ?= Release

PREFIX ?= mipsel-none-elf
FORMAT ?= elf32-littlemips

ROOTDIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

CC  = $(PREFIX)-gcc
CXX = $(PREFIX)-g++
AR  = $(PREFIX)-gcc-ar

TYPE ?= cpe
LDSCRIPT ?= $(ROOTDIR)$(TYPE).ld
ifeq ($(strip $(OVERLAYSCRIPT)),)
OVERLAYSCRIPT := $(ROOTDIR)nooverlay.ld
endif

LDSCRIPTS = $(OVERLAYSCRIPT) $(LDSCRIPT) $(EXTRA_LDSCRIPT)

USE_FUNCTION_SECTIONS ?= true

ARCHFLAGS = -march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -mfp32 -mno-llsc
ARCHFLAGS += -fno-stack-protector -nostdlib -ffreestanding
ifeq ($(USE_FUNCTION_SECTIONS),true)
CPPFLAGS += -ffunction-sections
endif
CPPFLAGS += -fdata-sections -mno-gpopt -fomit-frame-pointer
CPPFLAGS += -fno-builtin -fno-strict-aliasing -Wno-attributes

# gcc 16 folds volatile loads into the following operation and can narrow a
# 32-bit hardware register read to lhu/lbu. Probed so older compilers still build.
# Asked of the compiler with no redirection so the probe also works under cmd.exe.
NOFUSEVOLATILE := $(if $(filter -ffuse-ops-with-volatile-access,$(shell $(CC) -Q --help=target --help=optimizers)),-fno-fuse-ops-with-volatile-access)
CPPFLAGS += $(NOFUSEVOLATILE)
LDFLAGS += $(NOFUSEVOLATILE)
CPPFLAGS += $(ARCHFLAGS)
CPPFLAGS += -I$(ROOTDIR)
CPPFLAGS += $(foreach f, $(DEFINES), -D$(f))

LDFLAGS += -Wl,-Map=$(BINDIR)$(TARGET).map -nostdlib $(foreach script, $(LDSCRIPTS), -T$(script)) -static -Wl,--gc-sections
LDFLAGS += $(ARCHFLAGS) -Wl,--oformat=$(FORMAT)

CPPFLAGS_Release += -Os
LDFLAGS_Release += -Os

CPPFLAGS_ReleaseFast += -O3
LDFLAGS_ReleaseFast += -O3

CPPFLAGS_LTO += -Os -flto -ffat-lto-objects
LDFLAGS_LTO += -Os -flto -ffat-lto-objects

CPPFLAGS_LTOFast += -O3 -flto -ffat-lto-objects
LDFLAGS_LTOFast += -O3 -flto -ffat-lto-objects

CPPFLAGS_Debug += -O0
CPPFLAGS_SmallDebug += -Og
# Optimized, with every function entry and exit calling the psyqo shadow
# call stack hooks (psyqo/shadow-stack.hh). Only links against psyqo.
# Inlined functions get the hooks too, so two headers are left out: the
# syscall wrappers, some of which never return and would leave their entry
# on the shadow stack, and the fixed-point operators, which made four of
# the five hook pairs per call of the scratchpad-stack example's recursion.
CPPFLAGS_FastDebug += -O2 -finstrument-functions
CPPFLAGS_FastDebug += -finstrument-functions-exclude-file-list=common/syscalls/,psyqo/fixed-point.hh
CPPFLAGS_FastDebug += -DPSYQO_SHADOW_STACK
LDFLAGS_FastDebug += -O2
CPPFLAGS_Coverage += -Og

LDFLAGS += -g
CPPFLAGS += -g

CPPFLAGS += $(CPPFLAGS_$(BUILD))
LDFLAGS += $(LDFLAGS_$(BUILD))

CXXFLAGS += -fno-exceptions -fno-rtti

OBJS += $(addsuffix .o, $(basename $(SRCS)))

# Two sources with the same basename would build the same object.
SRCS_STEMS := $(abspath $(basename $(SRCS)))
SRCS_DUPES := $(foreach s,$(sort $(SRCS_STEMS)),$(if $(word 2,$(filter $(s),$(SRCS_STEMS))),$(s)))
ifneq ($(strip $(SRCS_DUPES)),)
$(error These sources would all build the same object: $(foreach f,$(SRCS),$(if $(filter $(abspath $(basename $(f))),$(SRCS_DUPES)),$(f))))
endif

ifeq ($(TYPE), library)
all: dep $(BINDIR)lib$(TARGET).a
else
all: dep $(LIBRARIES) $(BINDIR)$(TARGET).$(TYPE) $(foreach ovl, $(OVERLAYSECTION), $(BINDIR)Overlay$(ovl))
endif

$(BINDIR)Overlay%: $(BINDIR)$(TARGET).elf
	$(PREFIX)-objcopy -j $(@:$(BINDIR)Overlay%=%) -O binary $< $(BINDIR)Overlay$(@:$(BINDIR)Overlay%=%)

$(BINDIR)$(TARGET).$(TYPE): $(BINDIR)$(TARGET).elf
	$(PREFIX)-objcopy $(addprefix -R , $(OVERLAYSECTION)) -O binary $< $@

$(BINDIR)$(TARGET).elf: $(OBJS) $(LIBRARIES) $(EXTRA_DEPS)
ifneq ($(strip $(BINDIR)),)
	mkdir -p $(BINDIR)
endif
	$(CC) -g -o $(BINDIR)$(TARGET).elf $(OBJS) $(LDFLAGS) $(LIBRARIES)

$(BINDIR)lib$(TARGET).a: $(OBJS) $(EXTRA_DEPS)
	$(AR) rcs $(BINDIR)lib$(TARGET).a $(OBJS)

%.o: %.s
	$(CC) $(ARCHFLAGS) $(ASFLAGS) -I$(ROOTDIR) -g -c -o $@ $<

# The source named in SRCS is the one an object is built from, even when
# another file with the same basename sits next to it. The recipes are
# make's built-in ones.
SRCS_C   := $(filter %.c,$(SRCS))
SRCS_CPP := $(filter %.cpp,$(SRCS))
SRCS_CC  := $(filter %.cc,$(SRCS))
SRCS_S   := $(filter %.s,$(SRCS))
SRCS_SPP := $(filter %.S,$(SRCS))

ifneq ($(SRCS_C),)
$(SRCS_C:.c=.o): %.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<
$(SRCS_C:.c=.dep): %.dep: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<
endif
ifneq ($(SRCS_CPP),)
$(SRCS_CPP:.cpp=.o): %.o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<
$(SRCS_CPP:.cpp=.dep): %.dep: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<
endif
ifneq ($(SRCS_CC),)
$(SRCS_CC:.cc=.o): %.o: %.cc
	$(COMPILE.cc) $(OUTPUT_OPTION) $<
$(SRCS_CC:.cc=.dep): %.dep: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<
endif
ifneq ($(SRCS_S),)
$(SRCS_S:.s=.o): %.o: %.s
	$(CC) $(ARCHFLAGS) $(ASFLAGS) -I$(ROOTDIR) -g -c -o $@ $<
$(SRCS_S:.s=.dep): %.dep: %.s
	touch $@
endif
ifneq ($(SRCS_SPP),)
$(SRCS_SPP:.S=.o): %.o: %.S
	$(COMPILE.S) $(OUTPUT_OPTION) $<
endif

%.dep: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<

%.dep: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<

%.dep: %.cc
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -M -MT $(addsuffix .o, $(basename $@)) -MF $@ $<

# A bit broken, but that'll do in most cases.
%.dep: %.s
	touch $@

DEPS := $(patsubst %.cpp, %.dep,$(filter %.cpp,$(SRCS)))
DEPS += $(patsubst %.cc,  %.dep,$(filter %.cc,$(SRCS)))
DEPS +=	$(patsubst %.c,   %.dep,$(filter %.c,$(SRCS)))
DEPS += $(patsubst %.s,   %.dep,$(filter %.s,$(SRCS)))

dep: $(DEPS)

clean::
	rm -f $(OBJS) $(BINDIR)*.a $(BINDIR)Overlay.* $(BINDIR)*.elf $(BINDIR)*.ps-exe $(BINDIR)*.map $(DEPS)

ifneq ($(MAKECMDGOALS), clean)
ifneq ($(MAKECMDGOALS), deepclean)
-include $(DEPS)
endif
endif

.PHONY: clean dep all
