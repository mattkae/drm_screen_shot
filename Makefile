CXX=gcc
CXXFLAGS=-g -Wall -pedantic `pkg-config --cflags libdrm` `pkg-config --cflags glib-2.0` `pkg-config --cflags gbm` `pkg-config --cflags libjpeg`
src = $(wildcard *.c)
obj = $(src:.c=.o)
OUTPUT_PATH=.

LDFLAGS=`pkg-config --libs libdrm` `pkg-config --libs glib-2.0` `pkg-config --libs gbm` `pkg-config --libs libjpeg`

program: $(obj)
	$(CXX) $(obj) -o "$(OUTPUT_PATH)/$@" $(LDFLAGS)

%.o: %.c
	$(CXX) -c $(CXXFLAGS) -o "$@" "$<"

.PHONY: clean

clean:
	rm -f $(obj) program
