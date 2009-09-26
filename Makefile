all:
	gcc -O2 -Wall dlz_bdbhpt_driver.c -o dlzbdbhpt -ldb -lpcre
debug:
	gcc -g -O0 -Wall dlz_bdbhpt_driver.c -o dlzbdbhpt -ldb -lpcre
install:
	killall -9 dlzbdbhpt > /dev/null 2>&1 || true
	cp dlzbdbhpt /usr/sbin
	cp dlzbdbhpt /vservers/slave.oxnull.net/usr/sbin
	chmod 0700 /usr/sbin/dlzbdbhpt
