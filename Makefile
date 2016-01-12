UNAME = $(shell uname -r)
PWD = $(shell pwd)

all:
	$(MAKE) -C /lib/modules/$(UNAME)/build M=$(PWD)/drivers/net/hamradio modules

clean:
	$(MAKE) -C /lib/modules/$(UNAME)/build M=$(PWD)/drivers/net/hamradio clean

install:
	$(MAKE) -C /lib/modules/$(UNAME)/build M=$(PWD)/drivers/net/hamradio modules_install

.PHONY: all clean install
