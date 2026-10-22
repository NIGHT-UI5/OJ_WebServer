.PHONY: all start multi stop status logs clean

all:
	$(MAKE) -C compile_server compile_server
	$(MAKE) -C oj_server oj_server

start:
	./scripts/oj.sh start

multi:
	./scripts/oj.sh start --multi

stop:
	./scripts/oj.sh stop

status:
	./scripts/oj.sh status

logs:
	./scripts/oj.sh logs

clean:
	$(MAKE) -C compile_server clean
	$(MAKE) -C oj_server clean
