
noinst_SCRIPTS = filter_stderr

EXTRA_DIST = $(noinst_SCRIPTS) \
	ldstrev.stderr.exp ldstrev.stdout.exp ldstrev.vgtest \
	lsw.stderr.exp lsw.stdout.exp lsw.vgtest \
	jm-int.stderr.exp jm-int.stdout.exp jm-int.vgtest \
	jm-fp.stderr.exp jm-fp.stdout.exp jm-fp.vgtest \
	jm-vmx.stderr.exp   jm-vmx.stdout.exp   jm-vmx.vgtest \
	mftocrf.stderr.exp mftocrf.stdout.exp mftocrf.vgtest \
	round.stderr.exp round.stdout.exp round.vgtest \
	test_fx.stderr.exp test_fx.stdout.exp test_fx.vgtest \
	test_gx.stderr.exp test_gx.stdout.exp test_gx.vgtest \
	testVMX.stderr.exp  testVMX.stdout.exp  testVMX.vgtest \
	twi.stderr.exp twi.stdout.exp twi.vgtest \
	xlc_dbl_u32.stderr.exp xlc_dbl_u32.stdout.exp xlc_dbl_u32.vgtest

check_PROGRAMS = \
	ldstrev lsw jm-insns mftocrf round test_fx test_gx testVMX \
	twi xlc_dbl_u32

AM_CFLAGS   = $(WERROR) -Winline -Wall -Wshadow -g -I$(top_srcdir)/include \
		@FLAG_M32@
AM_CXXFLAGS = $(AM_CFLAGS) @FLAG_M32@

jm_insns_CFLAGS = -Winline -Wall -O -mregnames \
			-DHAS_ALTIVEC -maltivec @FLAG_M32@
testVMX_CFLAGS  = -O -g -Wall -maltivec -mabi=altivec -DALTIVEC \
			-DGCC_COMPILER @FLAG_M32@
