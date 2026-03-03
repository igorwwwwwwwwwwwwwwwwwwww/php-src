rp2350: $(SAPI_RP2350_PATH)

$(SAPI_RP2350_PATH): $(PHP_GLOBAL_OBJS) $(PHP_BINARY_OBJS) $(PHP_RP2350_OBJS)
	$(BUILD_RP2350)

install-rp2350: $(SAPI_RP2350_PATH)
	@echo "Installing PHP RP2350 binary:     $(INSTALL_ROOT)$(bindir)/"
	@$(mkinstalldirs) $(INSTALL_ROOT)$(bindir)
	@$(LIBTOOL) --mode=install $(INSTALL) -m 0755 $(SAPI_RP2350_PATH) $(INSTALL_ROOT)$(bindir)/$(program_prefix)php-rp2350$(program_suffix)$(EXEEXT)
