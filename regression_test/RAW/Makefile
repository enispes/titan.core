###############################################################################
# Copyright (c) 2000-2014 Ericsson Telecom AB
# All rights reserved. This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v1.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v10.html
###############################################################################
TOPDIR := ..
include $(TOPDIR)/Makefile.regression

RDIRS = Examples HN25015 HQ26535 HQ49956 HS16977

all dep clean run distclean:
	for dir in $(RDIRS); do (cd $$dir && $(MAKE) $@) || exit; done

