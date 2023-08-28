CXX=gcc
CXXFLAGS=-g -Wall -pedantic `pkg-config --cflags libdrm`
src = $(wildcard *.c)
obj = $(src:.c=.o)
OUTPUT_PATH=.

LDFLAGS=`pkg-config --libs libdrm`

drm_screen_shot: $(obj)
	$(CXX) $(obj) -o "$(OUTPUT_PATH)/$@" $(LDFLAGS)

%.o: %.c
	$(CXX) -c $(CXXFLAGS) -o "$@" "$<"

.PHONY: clean

clean:
	rm -f $(obj) drm_screen_shot
