all: silklock

silklock: silklock.cpp boxboss.cpp chairman.cpp
	g++ -g3 $^ -o $@ `pkg-config --cflags --libs gtk+-2.0 gthread-2.0`

clean:
	rm silklock
