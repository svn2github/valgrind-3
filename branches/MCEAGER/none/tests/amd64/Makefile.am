
noinst_SCRIPTS = filter_cpuid filter_stderr filter_int gen_insn_test.pl

CLEANFILES = $(addsuffix .c,$(INSN_TESTS))

INSN_TESTS = insn_basic insn_mmx insn_sse insn_sse2 insn_fpu
if BUILD_SSE3_TESTS
 INSN_TESTS += insn_sse3
endif
if BUILD_SSSE3_TESTS
 INSN_TESTS += insn_ssse3
endif

# Explicitly include insn_sse3 even if ! BUILD_SSE3_TESTS, 
# to avoid packaging screwups if 'make dist' is run on a machine
# which failed the BUILD_SSE3_TESTS test in configure.in.
EXTRA_DIST = $(noinst_SCRIPTS) \
	bug127521-64.vgtest bug127521-64.stdout.exp bug127521-64.stderr.exp \
	bug132813-amd64.vgtest bug132813-amd64.stdout.exp \
	bug132813-amd64.stderr.exp \
	bug137714-amd64.vgtest bug137714-amd64.stdout.exp \
	bug137714-amd64.stderr.exp \
	bug132918.vgtest bug132918.stderr.exp bug132918.stdout.exp \
	clc.vgtest clc.stdout.exp clc.stderr.exp \
	faultstatus.disabled faultstatus.stderr.exp \
	fcmovnu.vgtest fcmovnu.stderr.exp fcmovnu.stdout.exp \
	fxtract.vgtest fxtract.stderr.exp fxtract.stdout.exp \
	$(addsuffix .stderr.exp,$(INSN_TESTS)) \
	$(addsuffix .stdout.exp,$(INSN_TESTS)) \
	$(addsuffix .vgtest,$(INSN_TESTS)) \
	insn_sse3.stdout.exp insn_sse3.stderr.exp insn_sse3.vgtest \
	insn_ssse3.stdout.exp insn_ssse3.stderr.exp insn_ssse3.vgtest \
	jrcxz.stderr.exp jrcxz.stdout.exp jrcxz.vgtest \
	looper.stderr.exp looper.stdout.exp looper.vgtest \
	nibz_bennee_mmap.stderr.exp nibz_bennee_mmap.stdout.exp \
	nibz_bennee_mmap.vgtest \
	rcl-amd64.vgtest rcl-amd64.stdout.exp rcl-amd64.stderr.exp \
	redundantRexW.vgtest redundantRexW.stdout.exp \
	redundantRexW.stderr.exp \
	smc1.stderr.exp smc1.stdout.exp smc1.vgtest \
	shrld.stderr.exp shrld.stdout.exp shrld.vgtest \
	ssse3_misaligned.stderr.exp ssse3_misaligned.stdout.exp \
	ssse3_misaligned.vgtest \
	ssse3_misaligned.c \
	slahf-amd64.stderr.exp slahf-amd64.stdout.exp \
	slahf-amd64.vgtest

check_PROGRAMS = \
	bug127521-64 bug132813-amd64 bug137714-amd64 bug132918 \
	clc \
	faultstatus fcmovnu fxtract $(INSN_TESTS) looper jrcxz \
	rcl-amd64 \
	redundantRexW \
	smc1 shrld \
	nibz_bennee_mmap \
	slahf-amd64
if BUILD_SSSE3_TESTS
 check_PROGRAMS += ssse3_misaligned
endif

AM_CFLAGS   = $(WERROR) -Winline -Wall -Wshadow -g -I$(top_srcdir)/include
		@FLAG_M64@
AM_CXXFLAGS = $(AM_CFLAGS)

# generic C ones
bug132918_LDADD		= -lm
insn_basic_SOURCES	= insn_basic.def
insn_basic_LDADD	= -lm
insn_mmx_SOURCES	= insn_mmx.def
insn_mmx_LDADD		= -lm
insn_sse_SOURCES	= insn_sse.def
insn_sse_LDADD		= -lm
insn_sse2_SOURCES	= insn_sse2.def
insn_sse2_LDADD		= -lm
insn_sse3_SOURCES	= insn_sse3.def
insn_sse3_LDADD		= -lm
insn_ssse3_SOURCES	= insn_ssse3.def
insn_ssse3_LDADD	= -lm
insn_fpu_SOURCES	= insn_fpu.def
insn_fpu_LDADD		= -lm
fxtract_LDADD		= -lm

.def.c: $(srcdir)/gen_insn_test.pl
	$(PERL) $(srcdir)/gen_insn_test.pl < $< > $@
